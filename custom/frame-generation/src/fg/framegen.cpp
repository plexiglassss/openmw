#include "framegen.hpp"
#include "frame_contents.hpp"

#include "../config.hpp"
#include "../hooks.hpp"
#include "../log.hpp"
#include "../platform.hpp"
#include "overlay.hpp"
#include "shaders.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <thread>

#define GL_TEXTURE_MAX_LEVEL_ 0x813D

namespace
{
    constexpr int kNumGameTex = 4;
    constexpr int kMaxLevels = 8;

    struct Submission
    {
        int idx = -1;
        GLsync fence = nullptr;
        uint64_t timeNs = 0;
        uint32_t gen = 0;
    };

    struct Program
    {
        GLuint id = 0;
        GLint locA = -1, locB = -1, locC = -1, locD = -1;
    };

    struct State
    {
        // -- game-thread owned --
        bool triedInit = false;
        bool hardFailed = false; // do not retry init (e.g. no compute support)
        int width = 0, height = 0;
        int refreshHz = 0; // display refresh for auto-pause, 0 = unknown
        double gameFpsLimit = 0.0; // OpenMW's own framerate limit, 0 = none
        GLuint gameFbo = 0;
        GLuint gameTex[kNumGameTex] = {};
        int gameCur = 0;
        bool hotkeyWasDown = false;
        uint64_t suppressedReadbackFrames = 0;

        // -- shared --
        std::atomic<bool> suppressNextSwap{ false };
        std::atomic<bool> active{ false }; // init succeeded, presenter launched
        std::atomic<bool> redirect{ false }; // FBO-0 remap live
        std::atomic<bool> presenterReady{ false };
        std::atomic<bool> presenterFailed{ false };
        std::atomic<bool> quit{ false };
        std::atomic<bool> fgEnabled{ true };
        std::atomic<uint32_t> gen{ 1 };
        std::atomic<uint64_t> dropCount{ 0 };
        std::atomic<uint64_t> totalDropCount{ 0 };
        std::mutex mtx;
        std::condition_variable cvSubmit;
        bool submitSignal = false;
        std::thread presenterThread;
        Submission pending;
        int presCur = -1, presPrev = -1;
        // Presenter -> game synchronization, complementary to Submission::fence.
        // Protected by mtx; a slot cannot be written again until its GPU reads finish.
        GLsync reuseFence[kNumGameTex] = {};
    };

    State S;

    // ---------------------------------------------------------------- helpers

    void bindFramebufferRaw(GLenum target, GLuint fb)
    {
        glBindFramebuffer_(target, fb);
    }

    GLuint makeTexture2D(GLenum internalFormat, int w, int h, int levels, GLenum minFilter, GLenum magFilter)
    {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexStorage2D_(GL_TEXTURE_2D, levels, internalFormat, w, h);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)magFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL_, levels - 1);
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    }

    GLuint compileStage(GLenum type, const char* src, const char* name)
    {
        GLuint shader = glCreateShader_(type);
        glShaderSource_(shader, 1, &src, nullptr);
        glCompileShader_(shader);
        GLint status = 0;
        glGetShaderiv_(shader, GL_COMPILE_STATUS, &status);
        if (!status)
        {
            char info[2048];
            glGetShaderInfoLog_(shader, sizeof(info), nullptr, info);
            Log("fg: shader '%s' compile failed:\n%s", name, info);
            glDeleteShader_(shader);
            return 0;
        }
        return shader;
    }

    bool linkProgram(Program& prog, GLuint s1, GLuint s2, const char* name)
    {
        if (!s1 || (s2 == (GLuint)-1))
            return false;
        prog.id = glCreateProgram_();
        glAttachShader_(prog.id, s1);
        if (s2)
            glAttachShader_(prog.id, s2);
        glLinkProgram_(prog.id);
        glDeleteShader_(s1);
        if (s2)
            glDeleteShader_(s2);
        GLint status = 0;
        glGetProgramiv_(prog.id, GL_LINK_STATUS, &status);
        if (!status)
        {
            char info[2048];
            glGetProgramInfoLog_(prog.id, sizeof(info), nullptr, info);
            Log("fg: program '%s' link failed:\n%s", name, info);
            glDeleteProgram_(prog.id);
            prog.id = 0;
            return false;
        }
        return true;
    }

    bool compileProgram(Program& prog, const char* src, const char* name)
    {
        GLuint shader = compileStage(GL_COMPUTE_SHADER, src, name);
        return shader && linkProgram(prog, shader, 0, name);
    }

    // ------------------------------------------------------- presenter thread

    struct Presenter
    {
        uint32_t myGen = 0;
        int w = 0, h = 0;
        int halfW = 0, halfH = 0;
        int levels = 0;
        int flowW[kMaxLevels] = {}, flowH[kMaxLevels] = {};

        GLuint lumaTex[2] = {}; // [0]=prev frame, [1]=cur frame
        GLuint flowTex[kMaxLevels] = {};
        GLuint flowSmooth = 0;
        GLuint interpTex = 0;
        GLuint ssbo = 0;
        GLuint vao = 0;

        Program pLumaInit, pLumaDown, pMatch, pSmooth, pReduce, pInterp, pPresent;

        // pacing/stats
        double ema = 0.0;
        uint64_t lastSubmitNs = 0;
        bool prevValid = false;
        uint64_t statFramesIn = 0, statFramesOut = 0, statInterp = 0;
        uint64_t statWindowStart = 0;
        uint64_t totalFramesIn = 0, totalFramesOut = 0, totalInterp = 0;
        uint64_t dropBaseline = 0;
        // short-window fps for the overlay readout
        uint64_t dispIn = 0, dispOut = 0, dispWindowStart = 0;
        float dispInFps = 0.f, dispOutFps = 0.f;
        int appliedVsync = -1;
        int appliedMaxLevels = 0;

        // Auto-pause: with display-synced presents (driver vsync, G-Sync, DWM)
        // two presents per game frame cap the presenter cycle at 2 vblanks,
        // which drags a game that natively reaches the refresh rate down to
        // refresh/2. When the in-rate pins there, probe with interpolation off;
        // if the game speeds up it was display-limited and interpolation stays
        // paused until the game drops below refresh/2 on its own.
        enum class AutoState { Normal, Probing, Paused };
        AutoState autoState = AutoState::Normal;
        uint64_t probeEndNs = 0;
        uint64_t nextProbeNs = 0;
        float probeBaseFps = 0.f;
        double probeBackoffSec = 15.0;
        double presentBlockMsEma = 0.0;
        bool fgWasEnabled = true;

        bool compilePrograms()
        {
            using namespace fg::shaders;
            if (!compileProgram(pLumaInit, kLumaInit, "luma_init")
                || !compileProgram(pLumaDown, kLumaDown, "luma_down")
                || !compileProgram(pMatch, kMatch, "match")
                || !compileProgram(pSmooth, kSmooth, "smooth")
                || !compileProgram(pReduce, kReduce, "reduce")
                || !compileProgram(pInterp, kInterp, "interp"))
                return false;

            GLuint vs = compileStage(GL_VERTEX_SHADER, kPresentVS, "present_vs");
            GLuint fs = compileStage(GL_FRAGMENT_SHADER, kPresentFS, "present_fs");
            if (!vs || !fs || !linkProgram(pPresent, vs, fs, "present"))
                return false;

            pLumaInit.locA = glGetUniformLocation_(pLumaInit.id, "dstSize");
            pLumaDown.locA = glGetUniformLocation_(pLumaDown.id, "dstSize");
            pLumaDown.locB = glGetUniformLocation_(pLumaDown.id, "srcLevel");
            pMatch.locA = glGetUniformLocation_(pMatch.id, "flowSize");
            pMatch.locB = glGetUniformLocation_(pMatch.id, "level");
            pMatch.locC = glGetUniformLocation_(pMatch.id, "hasCoarse");
            pMatch.locD = glGetUniformLocation_(pMatch.id, "radius");
            pSmooth.locA = glGetUniformLocation_(pSmooth.id, "flowSize");
            pReduce.locA = glGetUniformLocation_(pReduce.id, "level");
            pInterp.locA = glGetUniformLocation_(pInterp.id, "fullSize");
            pInterp.locB = glGetUniformLocation_(pInterp.id, "sceneChangeThreshold");
            return true;
        }

        void destroySized()
        {
            if (lumaTex[0])
                glDeleteTextures(2, lumaTex);
            lumaTex[0] = lumaTex[1] = 0;
            for (int i = 0; i < kMaxLevels; ++i)
            {
                if (flowTex[i])
                    glDeleteTextures(1, &flowTex[i]);
                flowTex[i] = 0;
            }
            if (flowSmooth)
                glDeleteTextures(1, &flowSmooth);
            flowSmooth = 0;
            if (interpTex)
                glDeleteTextures(1, &interpTex);
            interpTex = 0;
        }

        bool createSized(int width, int height)
        {
            destroySized();
            w = width;
            h = height;
            halfW = (w + 1) / 2;
            halfH = (h + 1) / 2;

            levels = 1;
            while (levels < g_cfg.maxLevels && levels < kMaxLevels
                && (halfW >> levels) >= 16 && (halfH >> levels) >= 16)
                ++levels;

            lumaTex[0] = makeTexture2D(GL_R16F, halfW, halfH, levels, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST);
            lumaTex[1] = makeTexture2D(GL_R16F, halfW, halfH, levels, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST);
            for (int l = 0; l < levels; ++l)
            {
                int lw = halfW >> l ? halfW >> l : 1;
                int lh = halfH >> l ? halfH >> l : 1;
                flowW[l] = (lw + 3) / 4;
                flowH[l] = (lh + 3) / 4;
                flowTex[l] = makeTexture2D(GL_RG16F, flowW[l], flowH[l], 1, GL_LINEAR, GL_LINEAR);
            }
            flowSmooth = makeTexture2D(GL_RG16F, flowW[0], flowH[0], 1, GL_LINEAR, GL_LINEAR);
            interpTex = makeTexture2D(GL_RGBA8, w, h, 1, GL_LINEAR, GL_LINEAR);

            Log("fg: presenter resources %dx%d, pyramid %dx%d x%d levels, flow %dx%d",
                w, h, halfW, halfH, levels, flowW[0], flowH[0]);
            return true;
        }

        void bindSampler(int unit, GLuint tex)
        {
            glActiveTexture_(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, tex);
        }

        void dispatch2D(int sizeX, int sizeY, int localX = 16, int localY = 16)
        {
            glDispatchCompute_((sizeX + localX - 1) / localX, (sizeY + localY - 1) / localY, 1);
            glMemoryBarrier_(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        }

        void buildLumaPyramid(GLuint srcColorTex, GLuint dstLuma)
        {
            glUseProgram_(pLumaInit.id);
            bindSampler(0, srcColorTex);
            glBindImageTexture_(0, dstLuma, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
            glUniform2i_(pLumaInit.locA, halfW, halfH);
            dispatch2D(halfW, halfH);

            glUseProgram_(pLumaDown.id);
            bindSampler(0, dstLuma);
            for (int l = 1; l < levels; ++l)
            {
                int lw = halfW >> l ? halfW >> l : 1;
                int lh = halfH >> l ? halfH >> l : 1;
                glBindImageTexture_(0, dstLuma, l, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
                glUniform1i_(pLumaDown.locB, l - 1);
                glUniform2i_(pLumaDown.locA, lw, lh);
                dispatch2D(lw, lh);
            }
        }

        // prev/cur are the shared full-res game textures.
        void runPipeline(GLuint prevTex, GLuint curTex)
        {
            buildLumaPyramid(prevTex, lumaTex[0]);
            buildLumaPyramid(curTex, lumaTex[1]);

            // scene-change metric on coarsest level
            glUseProgram_(pReduce.id);
            bindSampler(0, lumaTex[0]);
            bindSampler(1, lumaTex[1]);
            glUniform1i_(pReduce.locA, levels - 1);
            glBindBufferBase_(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
            glDispatchCompute_(1, 1, 1);
            glMemoryBarrier_(GL_SHADER_STORAGE_BARRIER_BIT);

            // coarse-to-fine block matching
            glUseProgram_(pMatch.id);
            bindSampler(0, lumaTex[0]);
            bindSampler(1, lumaTex[1]);
            glUniform1i_(pMatch.locD, g_cfg.searchRadius);
            for (int l = levels - 1; l >= 0; --l)
            {
                bindSampler(2, l + 1 < levels ? flowTex[l + 1] : 0);
                glBindImageTexture_(0, flowTex[l], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
                glUniform1i_(pMatch.locB, l);
                glUniform1i_(pMatch.locC, l + 1 < levels ? 1 : 0);
                glUniform2i_(pMatch.locA, flowW[l], flowH[l]);
                dispatch2D(flowW[l], flowH[l], 8, 8);
            }

            glUseProgram_(pSmooth.id);
            bindSampler(0, flowTex[0]);
            glBindImageTexture_(0, flowSmooth, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
            glUniform2i_(pSmooth.locA, flowW[0], flowH[0]);
            dispatch2D(flowW[0], flowH[0]);

            glUseProgram_(pInterp.id);
            bindSampler(0, curTex);
            bindSampler(1, prevTex);
            bindSampler(2, flowSmooth);
            glBindImageTexture_(0, interpTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
            glBindBufferBase_(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
            glUniform2i_(pInterp.locA, w, h);
            glUniform1f_(pInterp.locB, g_cfg.sceneChangeThreshold);
            glDispatchCompute_((w + 15) / 16, (h + 15) / 16, 1);
            glMemoryBarrier_(GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        }

        // Draw, not blit: the default framebuffer may be multisampled
        // (antialiasing > 0) and non-MSAA -> MSAA blits are invalid.
        void presentTexture(GLuint tex)
        {
            bindFramebufferRaw(GL_DRAW_FRAMEBUFFER, 0);
            real_glDrawBuffer(GL_BACK);
            glViewport(0, 0, w, h);
            glUseProgram_(pPresent.id);
            bindSampler(0, tex);
            glBindVertexArray_(vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            const fg::overlay::Diagnostics diagnostics{
                totalFramesIn,
                totalInterp,
                totalFramesOut,
                S.totalDropCount.load(std::memory_order_relaxed) - dropBaseline
            };
            fg::overlay::Render(w, h, dispInFps, dispOutFps, diagnostics, S.fgEnabled,
                autoState == AutoState::Paused);
            uint64_t t0 = plat::nowNs();
            plat::presenterSwapBuffers();
            double blockMs = double(plat::nowNs() - t0) / 1e6;
            presentBlockMsEma = presentBlockMsEma * 0.85 + blockMs * 0.15;
            ++statFramesOut;
            ++dispOut;
            ++totalFramesOut;
        }

        void updateDisplayFps()
        {
            uint64_t now = plat::nowNs();
            if (!dispWindowStart)
                dispWindowStart = now;
            double sec = double(now - dispWindowStart) / 1e9;
            if (sec >= 0.5)
            {
                dispInFps = float(dispIn / sec);
                dispOutFps = float(dispOut / sec);
                dispIn = dispOut = 0;
                dispWindowStart = now;
            }
        }

        void updateAutoPause()
        {
            if (!g_cfg.autoPause || S.refreshHz <= 0)
            {
                autoState = AutoState::Normal;
                return;
            }
            if (!S.fgEnabled.load())
            {
                autoState = AutoState::Normal;
                fgWasEnabled = false;
                return;
            }
            uint64_t now = plat::nowNs();
            if (!fgWasEnabled)
            {
                // FG re-enabled: fresh evaluation once the fps window settles
                fgWasEnabled = true;
                autoState = AutoState::Normal;
                probeBackoffSec = 15.0;
                nextProbeNs = now + 3000000000ull;
            }
            const float halfRefresh = S.refreshHz * 0.5f;
            const double vblankMs = 1000.0 / S.refreshHz;
            switch (autoState)
            {
                case AutoState::Normal:
                    if (dispInFps > halfRefresh * 0.90f && dispInFps < halfRefresh * 1.10f
                        && presentBlockMsEma > vblankMs * 0.30
                        && (S.gameFpsLimit <= 0.0 || S.gameFpsLimit > halfRefresh * 1.15f)
                        && now >= nextProbeNs)
                    {
                        autoState = AutoState::Probing;
                        probeBaseFps = dispInFps;
                        // 1.2 s: the final 0.5 s fps window is entirely probe-time
                        probeEndNs = now + 1200000000ull;
                        Log("fg: auto: in=%.0f fps ~ half of %d Hz with blocking presents, probing native rate",
                            (double)dispInFps, S.refreshHz);
                    }
                    break;
                case AutoState::Probing:
                    if (now >= probeEndNs)
                    {
                        if (dispInFps > probeBaseFps * 1.12f)
                        {
                            autoState = AutoState::Paused;
                            Log("fg: auto: display-limited (%.0f -> %.0f fps with interpolation off), interpolation paused",
                                (double)probeBaseFps, (double)dispInFps);
                        }
                        else
                        {
                            autoState = AutoState::Normal;
                            nextProbeNs = now + (uint64_t)(probeBackoffSec * 1e9);
                            probeBackoffSec = probeBackoffSec * 2.0 < 300.0 ? probeBackoffSec * 2.0 : 300.0;
                            Log("fg: auto: probe negative (%.0f fps), interpolation stays on", (double)dispInFps);
                        }
                    }
                    break;
                case AutoState::Paused:
                    if (dispInFps > 0.f && dispInFps < halfRefresh * 0.92f)
                    {
                        autoState = AutoState::Normal;
                        nextProbeNs = now + 5000000000ull;
                        Log("fg: auto: game at %.0f fps fits under %d Hz again, interpolation resumed",
                            (double)dispInFps, S.refreshHz);
                    }
                    break;
            }
        }

        void waitUntilNs(uint64_t target)
        {
            for (;;)
            {
                uint64_t now = plat::nowNs();
                if (now >= target)
                    return;
                double remainMs = double(target - now) / 1e6;
                if (remainMs > 2.0)
                    plat::sleepMs(remainMs - 1.5);
                else
                    plat::cpuRelax();
            }
        }

        void maybeLogStats()
        {
            uint64_t now = plat::nowNs();
            if (!statWindowStart)
                statWindowStart = now;
            double sec = double(now - statWindowStart) / 1e9;
            if (sec < (double)g_cfg.statsIntervalSec)
                return;
            Log("fg: in=%.1f fps out=%.1f fps generated=%llu dropped=%llu enabled=%d auto_paused=%d",
                statFramesIn / sec, statFramesOut / sec,
                (unsigned long long)statInterp,
                (unsigned long long)S.dropCount.exchange(0),
                S.fgEnabled.load() ? 1 : 0,
                autoState == AutoState::Paused ? 1 : 0);
            statFramesIn = statFramesOut = statInterp = 0;
            statWindowStart = now;
        }

        void run()
        {
            if (!plat::presenterMakeCurrent())
            {
                Log("fg: presenter make-current failed");
                S.presenterFailed = true;
                return;
            }
            plat::presenterSetSwapInterval(g_cfg.presentVsync);

            if (!compilePrograms())
            {
                S.presenterFailed = true;
                return;
            }
            glGenVertexArrays_(1, &vao);
            glGenBuffers_(1, &ssbo);
            glBindBuffer_(GL_SHADER_STORAGE_BUFFER, ssbo);
            const float zero = 0.f;
            glBufferData_(GL_SHADER_STORAGE_BUFFER, sizeof(float), &zero, GL_DYNAMIC_READ);
            glBindBuffer_(GL_SHADER_STORAGE_BUFFER, 0);

            fg::overlay::InitGL(); // non-fatal if it fails
            appliedVsync = g_cfg.presentVsync;
            appliedMaxLevels = g_cfg.maxLevels;
            dropBaseline = S.totalDropCount.load(std::memory_order_relaxed);

            S.presenterReady = true;
            Log("fg: presenter ready");

            while (!S.quit.load())
            {
                fg::overlay::PollHotkeys();
                if (appliedVsync != g_cfg.presentVsync)
                {
                    appliedVsync = g_cfg.presentVsync;
                    if (plat::presenterSetSwapInterval(appliedVsync))
                        Log("fg: present vsync -> %d", appliedVsync);
                }

                {
                    std::unique_lock<std::mutex> lock(S.mtx);
                    if (!S.cvSubmit.wait_for(lock, std::chrono::milliseconds(50), [] { return S.submitSignal; }))
                        continue;
                    S.submitSignal = false;
                }
                if (S.quit.load())
                    break;

                // The CPU releasing a slot does not mean the presenter's GPU
                // commands have finished sampling it. Flush a fence on this
                // context before publishing the old previous slot as reusable.
                GLsync releasedFence = nullptr;
                if (S.presPrev >= 0)
                {
                    releasedFence = glFenceSync_(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                    if (releasedFence)
                        glFlush();
                    else
                        glFinish(); // allocation failure: preserve ordering
                }
                Submission sub;
                int prevIdx;
                {
                    std::lock_guard<std::mutex> lock(S.mtx);
                    sub = S.pending;
                    S.pending = Submission{};
                    if (sub.idx >= 0)
                    {
                        if (S.presPrev >= 0)
                        {
                            S.reuseFence[S.presPrev] = releasedFence;
                            releasedFence = nullptr;
                        }
                        S.presPrev = S.presCur;
                        S.presCur = sub.idx;
                    }
                    prevIdx = S.presPrev;
                }
                if (releasedFence)
                    glDeleteSync_(releasedFence);
                if (sub.idx < 0)
                    continue;
                ++statFramesIn;
                ++dispIn;
                ++totalFramesIn;
                updateDisplayFps();
                updateAutoPause();

                if (sub.gen != myGen || appliedMaxLevels != g_cfg.maxLevels)
                {
                    createSized(S.width, S.height);
                    myGen = sub.gen;
                    appliedMaxLevels = g_cfg.maxLevels;
                    prevValid = false;
                    ema = 0.0;
                }

                if (sub.fence)
                {
                    glWaitSync_(sub.fence, 0, GL_TIMEOUT_IGNORED);
                    glDeleteSync_(sub.fence);
                }

                const bool hadPreviousSubmit = lastSubmitNs != 0;
                double dt = hadPreviousSubmit ? double(sub.timeNs - lastSubmitNs) / 1e9 : 0.0;
                lastSubmitNs = sub.timeNs;

                // A save/load, cell transition, debugger stall, minimize/restore,
                // or any other long gap breaks temporal continuity.  Previously
                // such a gap was deliberately excluded from the cadence EMA but
                // prevValid/ema were left untouched, so the first frame after the
                // stall could still be interpolated against a stale pre-stall
                // frame.  That is especially visible as UI flashing around saves
                // and loads.  Treat the same 250 ms cutoff used by the cadence
                // filter as a hard history boundary instead.
                const bool timelineDiscontinuity = hadPreviousSubmit && dt >= 0.25;
                if (timelineDiscontinuity)
                {
                    prevValid = false;
                    ema = 0.0;
                    Log("fg: timeline discontinuity %.1f ms, interpolation history reset", dt * 1000.0);
                }
                else if (dt > 0.0005)
                {
                    ema = ema <= 0.0 ? dt : ema * 0.9 + dt * 0.1;
                }

                bool doInterp = S.fgEnabled.load() && autoState == AutoState::Normal
                    && prevValid && prevIdx >= 0 && ema > 0.004 && ema < 0.25;

                if (doInterp)
                {
                    runPipeline(S.gameTex[prevIdx], S.gameTex[sub.idx]);
                    presentTexture(interpTex);
                    ++statInterp;
                    ++totalInterp;
                    // Pace the real frame half a game-frame after the moment the
                    // interpolated one actually went out; anchoring to the submit
                    // time made the two display gaps alternate short/long by the
                    // pipeline latency (visible jitter at an fps cap).
                    waitUntilNs(plat::nowNs() + (uint64_t)(ema * 0.5 * 1e9));
                }
                presentTexture(S.gameTex[sub.idx]);
                prevValid = true;
                maybeLogStats();
            }

            fg::overlay::ShutdownGL();
            destroySized();
            if (vao)
                glDeleteVertexArrays_(1, &vao);
            if (ssbo)
                glDeleteBuffers_(1, &ssbo);
            for (Program* p : { &pLumaInit, &pLumaDown, &pMatch, &pSmooth, &pReduce, &pInterp, &pPresent })
                if (p->id)
                    glDeleteProgram_(p->id);
            plat::presenterDoneCurrent();
        }
    };

    Presenter g_presenter;

    void presenterMain()
    {
        plat::highResTimerBegin();
        g_presenter = Presenter{};
        g_presenter.run();
        plat::highResTimerEnd();
        if (!S.quit.load())
        {
            // Unexpected exit: fall back to passthrough so the game stays visible.
            Log("fg: presenter exited unexpectedly, disabling frame generation");
            S.redirect = false;
            S.presenterReady = false;
            S.active = false;
        }
    }

    // ------------------------------------------------------ game-thread logic

    void attachGameTexture(int idx)
    {
        GLint prevDraw = 0, prevRead = 0;
        real_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevDraw);
        real_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
        bindFramebufferRaw(GL_FRAMEBUFFER, S.gameFbo);
        glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, S.gameTex[idx], 0);
        bindFramebufferRaw(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
        bindFramebufferRaw(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    }

    bool initOnGameThread()
    {
        int captured = plat::captureGameContext();
        if (captured <= 0)
        {
            if (captured < 0)
                S.hardFailed = true;
            else
                Log("fg: init skipped, no window/context yet");
            return false;
        }

        if (!plat::getDrawableSize(S.width, S.height) || S.width <= 0 || S.height <= 0)
        {
            Log("fg: init skipped, zero-size client area");
            return false; // retryable
        }

        if (!GLLoad(plat::getProcAddress))
        {
            Log("fg: required GL 4.3 functions missing, frame generation unavailable");
            S.hardFailed = true;
            return false;
        }
        const GLubyte* ver = glGetString(GL_VERSION);
        const GLubyte* renderer = glGetString(GL_RENDERER);
        Log("fg: GL_VERSION='%s' GL_RENDERER='%s'", (const char*)ver, (const char*)renderer);

        // glXGetProcAddress never returns null, so verify the context version
        // actually covers the compute pipeline (GL 4.3).
        GLint major = 0, minor = 0;
        real_glGetIntegerv(GL_MAJOR_VERSION, &major);
        real_glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major * 10 + minor < 43)
        {
            Log("fg: OpenGL 4.3 required, context is %d.%d — frame generation unavailable", major, minor);
            S.hardFailed = true;
            return false;
        }

        if (!real_glBindFramebuffer && !real_glBindFramebufferEXT)
        {
            // The engine resolved glBindFramebuffer before our hook existed —
            // redirect impossible.
            Log("fg: glBindFramebuffer was never resolved through our hook, cannot redirect");
            S.hardFailed = true;
            return false;
        }

        for (int i = 0; i < kNumGameTex; ++i)
            S.gameTex[i] = makeTexture2D(GL_RGBA8, S.width, S.height, 1, GL_LINEAR, GL_LINEAR);

        glGenFramebuffers_(1, &S.gameFbo);
        GLint prevDraw = 0, prevRead = 0;
        real_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevDraw);
        real_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
        bindFramebufferRaw(GL_FRAMEBUFFER, S.gameFbo);
        glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, S.gameTex[0], 0);
        GLenum status = glCheckFramebufferStatus_(GL_FRAMEBUFFER);
        bindFramebufferRaw(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
        bindFramebufferRaw(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            Log("fg: replacement FBO incomplete (0x%X)", status);
            S.hardFailed = true;
            return false;
        }

        if (!plat::createPresenterContext())
        {
            Log("fg: presenter context creation failed");
            S.hardFailed = true;
            return false;
        }

        S.refreshHz = g_cfg.displayHz > 0 ? g_cfg.displayHz : plat::displayRefreshHz();
        S.gameFpsLimit = plat::gameFramerateLimit();
        Log("fg: display refresh %d Hz%s, openmw framerate limit %.0f",
            S.refreshHz, g_cfg.displayHz > 0 ? " (from ini)" : "", S.gameFpsLimit);

        S.quit = false;
        S.submitSignal = false;
        S.presCur = S.presPrev = -1;
        S.pending = Submission{};
        S.suppressNextSwap = false;
        S.presenterThread = std::thread(presenterMain);

        fg::overlay::InstallInputHook();

        S.active = true;
        Log("fg: initialized %dx%d, waiting for presenter", S.width, S.height);
        return true;
    }

    void teardownOnGameThread()
    {
        fg::overlay::RemoveInputHook();
        S.redirect = false;
        S.quit = true;
        {
            std::lock_guard<std::mutex> lock(S.mtx);
            S.submitSignal = true;
        }
        S.cvSubmit.notify_all();
        if (S.presenterThread.joinable())
            S.presenterThread.join();
        if (S.pending.fence)
        {
            glDeleteSync_(S.pending.fence);
            S.pending = Submission{};
        }
        if (S.gameFbo)
        {
            glDeleteFramebuffers_(1, &S.gameFbo);
            S.gameFbo = 0;
        }
        for (int i = 0; i < kNumGameTex; ++i)
        {
            if (S.reuseFence[i])
                glDeleteSync_(S.reuseFence[i]);
            S.reuseFence[i] = nullptr;
            if (S.gameTex[i])
                glDeleteTextures(1, &S.gameTex[i]);
            S.gameTex[i] = 0;
        }
        plat::destroyPresenterContext();
        plat::forgetGameContext();
        S.active = false;
        S.presenterReady = false;
        S.presenterFailed = false;
        S.quit = false;
        S.presCur = S.presPrev = -1;
        S.gameCur = 0;
        S.suppressNextSwap = false;
        S.triedInit = false;
        S.gen.fetch_add(1);
    }

    void pollHotkey()
    {
        // inert while the menu is open (key may be mid-rebind) or unfocused
        bool down = plat::keyDown(g_cfg.hotkey);
        if (fg::overlay::MenuOpen() || !plat::gameWindowFocused())
        {
            S.hotkeyWasDown = down;
            return;
        }
        if (down && !S.hotkeyWasDown)
        {
            bool enabled = !S.fgEnabled.load();
            S.fgEnabled = enabled;
            Log("fg: frame generation %s (hotkey)", enabled ? "ENABLED" : "DISABLED");
        }
        S.hotkeyWasDown = down;
    }
}

namespace fg
{
    bool OnSwapShared()
    {
        if (g_cfg.mode == 0 || S.hardFailed)
            return false;

        if (!S.triedInit)
        {
            S.triedInit = true;
            if (!initOnGameThread() && !S.hardFailed)
                S.triedInit = false; // transient failure, retry next frame
            return false;
        }

        if (!S.active.load() || !plat::isGameSwapTarget())
            return false;

        if (S.presenterFailed.load())
        {
            Log("fg: presenter failed, tearing down");
            teardownOnGameThread();
            S.hardFailed = true;
            return false;
        }

        pollHotkey();

        if (!S.presenterReady.load())
            return false;

        // Resize (or minimize) -> full reinit at the next valid-size frame.
        int cw = 0, ch = 0;
        plat::getDrawableSize(cw, ch);
        if (cw != S.width || ch != S.height)
        {
            Log("fg: client size changed %dx%d -> %dx%d, reinitializing", S.width, S.height, cw, ch);
            teardownOnGameThread();
            return false;
        }

        if (!S.redirect.load())
        {
            // Arm the redirect at a frame boundary: everything after this swap
            // lands in our offscreen target.
            S.gameCur = 0;
            attachGameTexture(0);
            S.redirect = true;
            Log("fg: framebuffer redirect ACTIVE");
            return false;
        }

        if (S.suppressNextSwap.exchange(false, std::memory_order_relaxed))
        {
            // A framebuffer readback happened during this render traversal.
            // OpenMW uses such a traversal to make save/F12 screenshots before
            // its POST_RENDER GUI camera. Swallowing this swap leaves the last
            // real frame visible and prevents the helper frame from flashing.
            ++S.suppressedReadbackFrames;
            Log("fg: suppressed framebuffer-readback helper frame (%llu total)",
                (unsigned long long)S.suppressedReadbackFrames);
            return true;
        }

        // Frame is complete in gameTex[gameCur]; hand it to the presenter.
        GLsync fence = glFenceSync_(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (fence)
            glFlush();
        else
            glFinish(); // no fence: complete the write before handing it off
        uint64_t now = plat::nowNs();

        const int completedIdx = S.gameCur;
        GLsync reuseFence = nullptr;
        {
            std::lock_guard<std::mutex> lock(S.mtx);
            if (S.pending.idx >= 0)
            {
                if (S.pending.fence)
                    glDeleteSync_(S.pending.fence);
                S.dropCount.fetch_add(1);
                S.totalDropCount.fetch_add(1);
            }
            S.pending = Submission{ S.gameCur, fence, now, S.gen.load() };
            for (int i = 0; i < kNumGameTex; ++i)
            {
                if (i != S.pending.idx && i != S.presCur && i != S.presPrev)
                {
                    S.gameCur = i;
                    reuseFence = S.reuseFence[i];
                    S.reuseFence[i] = nullptr;
                    break;
                }
            }
            S.submitSignal = true;
        }
        S.cvSubmit.notify_one();

        if (reuseFence)
        {
            // Queue a GPU-side dependency, without a per-frame CPU glFinish.
            glWaitSync_(reuseFence, 0, GL_TIMEOUT_IGNORED);
            glDeleteSync_(reuseFence);
        }
        // Loading traversals read the framebuffer before drawing their frozen
        // scene background. Rotating straight to an old slot makes those reads
        // alternate between old viewpoints even with interpolation disabled.
        // Retain the just-completed native image, after waiting for the old
        // presenter's reads of the destination. Subsequent game commands are
        // ordered after this copy on the same context; no CPU readback is used.
        fg::PreserveFrameContents(S.gameTex[completedIdx], S.gameTex[S.gameCur], S.width, S.height);
        attachGameTexture(S.gameCur);

        // The presenter owns the real backbuffer; nothing to present here.
        return true;
    }

    void ShutdownIfActive()
    {
        if (!S.active.load())
            return;
        Log("fg: game context/window teardown, shutting down frame generation");
        teardownOnGameThread();
        S.hardFailed = true; // no re-init during process shutdown
    }

#ifdef _WIN32
    BOOL OnSwap(HDC hdc, BOOL(WINAPI* realSwap)(HDC))
    {
        plat::win::setSwapTarget(hdc);
        if (OnSwapShared())
            return TRUE;
        return realSwap(hdc);
    }
#endif

    void RemapBindFramebuffer(GLenum target, GLuint* framebuffer)
    {
        (void)target;
        if (*framebuffer != 0 || !S.redirect.load(std::memory_order_relaxed))
            return;
        if (!plat::isGameContextCurrent())
            return;
        *framebuffer = S.gameFbo;
    }

    void RemapDrawBuffer(GLenum* buf)
    {
        if (!S.redirect.load(std::memory_order_relaxed))
            return;
        if (*buf != GL_BACK && *buf != GL_BACK_LEFT && *buf != GL_FRONT && *buf != GL_FRONT_LEFT)
            return;
        if (!plat::isGameContextCurrent())
            return;
        GLint bound = 0;
        real_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
        if ((GLuint)bound == S.gameFbo)
            *buf = GL_COLOR_ATTACHMENT0;
    }

    void RemapReadBuffer(GLenum* buf)
    {
        if (!S.redirect.load(std::memory_order_relaxed))
            return;
        if (*buf != GL_BACK && *buf != GL_BACK_LEFT && *buf != GL_FRONT && *buf != GL_FRONT_LEFT)
            return;
        if (!plat::isGameContextCurrent())
            return;
        GLint bound = 0;
        real_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &bound);
        if ((GLuint)bound == S.gameFbo)
            *buf = GL_COLOR_ATTACHMENT0;
    }

    void NoteFramebufferReadback()
    {
        if (!g_cfg.suppressReadbackFrames || !S.redirect.load(std::memory_order_relaxed)
            || S.gameFbo == 0 || !plat::isGameContextCurrent())
            return;

        // Query the real binding (not the spoofed value returned to OpenMW).
        GLint bound = 0;
        real_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &bound);
        if ((GLuint)bound == S.gameFbo)
            S.suppressNextSwap = true;
    }

    void SpoofFramebufferBinding(GLint* result)
    {
        if (!S.redirect.load(std::memory_order_relaxed) || S.gameFbo == 0)
            return;
        if (*result == (GLint)S.gameFbo && plat::isGameContextCurrent())
            *result = 0;
    }
}
