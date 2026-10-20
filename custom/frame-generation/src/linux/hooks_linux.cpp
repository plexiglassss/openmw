// Linux hook layer: exported interposer symbols that shadow libSDL2/libGL in
// the dynamic linker's global scope (LD_PRELOAD precedes them). Equivalent of
// hooks.cpp (MinHook) on Windows.
//
//  - SDL_GL_SwapWindow: the swap choke point — OpenMW calls it via PLT.
//  - SDL_PollEvent: input sniffing (hotkey key states) + full capture while
//    the overlay menu is open; also applies menu cursor state on the game
//    thread (SDL is not thread-safe, the presenter must not touch it).
//  - SDL_SetRelativeMouseMode / SDL_WarpMouseInWindow: suppressed while the
//    menu is open so the game cannot recenter/grab the cursor.
//  - glXGetProcAddress(ARB): hands out wrapped glBindFramebuffer(EXT), which
//    is how OSG's resolved pointer becomes ours (OSG finds our export via
//    dlsym(dlopen(NULL)) before libGL).
//  - glGetIntegerv / glDrawBuffer / glReadBuffer: state-cache lies for the
//    framebuffer redirect, same as the Windows MinHook versions.
#include "../fg/framegen.hpp"
#include "../fg/overlay.hpp"
#include "../hooks.hpp"
#include "../log.hpp"
#include "linux_shared.hpp"

#include <dlfcn.h>

#include <atomic>
#include <cstring>
#include <mutex>

#define FG_EXPORT extern "C" __attribute__((visibility("default")))

// Trampoline pointers shared with the FG core (declared in hooks.hpp).
void(APIENTRY* real_glGetIntegerv)(GLenum, GLint*) = nullptr;
void(APIENTRY* real_glDrawBuffer)(GLenum) = nullptr;
void(APIENTRY* real_glReadBuffer)(GLenum) = nullptr;
void(APIENTRY* real_glBindFramebuffer)(GLenum, GLuint) = nullptr;
void(APIENTRY* real_glBindFramebufferEXT)(GLenum, GLuint) = nullptr;

namespace lin
{
    void* glHandle()
    {
        static void* h = [] {
            void* p = dlopen("libGL.so.1", RTLD_LAZY | RTLD_NOLOAD);
            if (!p)
                p = dlopen("libGL.so.1", RTLD_LAZY);
            if (!p)
                p = dlopen("libGL.so", RTLD_LAZY);
            return p;
        }();
        return h;
    }

    void* realSym(const char* name)
    {
        void* p = dlsym(RTLD_NEXT, name);
        if (!p && glHandle())
            p = dlsym(glHandle(), name);
        return p;
    }

    void* realSdl(const char* name)
    {
        return dlsym(RTLD_NEXT, name);
    }

    void resolveRealGl()
    {
        if (!real_glGetIntegerv)
            real_glGetIntegerv = (void(APIENTRY*)(GLenum, GLint*))realSym("glGetIntegerv");
        if (!real_glDrawBuffer)
            real_glDrawBuffer = (void(APIENTRY*)(GLenum))realSym("glDrawBuffer");
        if (!real_glReadBuffer)
            real_glReadBuffer = (void(APIENTRY*)(GLenum))realSym("glReadBuffer");
    }
}

namespace
{
    // --- input state (game thread writes, any thread reads) ---
    std::atomic<bool> g_keys[SDL_NUM_SCANCODES] = {};
    std::atomic<bool> g_focus{ true };

    constexpr int kQueueCap = 512;
    std::mutex g_qmtx;
    SDL_Event g_queue[kQueueCap];
    int g_qn = 0;

    SDL_Window* g_swapWin = nullptr;

    // cursor bookkeeping around menu open/close (game thread)
    bool g_menuWasOpen = false;
    SDL_bool g_gameRelative = SDL_FALSE; // game's last requested relative mode

    bool isInputEvent(const SDL_Event& e)
    {
        switch (e.type)
        {
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            case SDL_TEXTINPUT:
            case SDL_TEXTEDITING:
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEWHEEL:
                return true;
            default:
                return false;
        }
    }

    void noteEvent(const SDL_Event& e)
    {
        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
        {
            int sc = e.key.keysym.scancode;
            if (sc >= 0 && sc < SDL_NUM_SCANCODES)
                g_keys[sc].store(e.type == SDL_KEYDOWN, std::memory_order_relaxed);
        }
        else if (e.type == SDL_WINDOWEVENT)
        {
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                g_focus = true;
            else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                g_focus = false;
        }
    }

    // Applies/undoes the menu's cursor requirements. Game thread only, called
    // from the SDL_PollEvent and SDL_GL_SwapWindow interposers each frame.
    void applyMenuCursorState()
    {
        bool open = fg::overlay::MenuOpen();
        if (open == g_menuWasOpen)
            return;
        g_menuWasOpen = open;
        static auto realSetRel = (int (*)(SDL_bool))lin::realSdl("SDL_SetRelativeMouseMode");
        if (!realSetRel)
            return;
        if (open)
            realSetRel(SDL_FALSE); // absolute mouse for the ImGui menu
        else
            realSetRel(g_gameRelative); // whatever the game last asked for
    }
}

namespace lin
{
    bool keyDown(int scancode)
    {
        return scancode >= 0 && scancode < SDL_NUM_SCANCODES
            && g_keys[scancode].load(std::memory_order_relaxed);
    }

    bool focused()
    {
        return g_focus.load(std::memory_order_relaxed);
    }

    int drainOverlayEvents(SDL_Event* out, int cap)
    {
        std::lock_guard<std::mutex> lock(g_qmtx);
        int n = g_qn < cap ? g_qn : cap;
        if (n)
            memcpy(out, g_queue, sizeof(SDL_Event) * n);
        g_qn = 0;
        return n;
    }

    void setSwapWindow(SDL_Window* w)
    {
        g_swapWin = w;
    }

    SDL_Window* swapWindow()
    {
        return g_swapWin;
    }
}

// ------------------------------------------------------------- SDL interposers

FG_EXPORT void glBindFramebuffer(GLenum target, GLuint framebuffer); // defined below

namespace
{
    // OSG resolves GL extension functions with dlsym(dlopen(NULL), name),
    // which finds our exported wrapper — but that capture is lazy (first
    // call), and the engine may not bind any FBO before the first swap.
    // Replicate the engine's lookup once: if it resolves to our interposer,
    // eagerly fill the real pointer so the redirect check can pass; if it
    // resolves elsewhere, leave it null so framegen hard-fails safely.
    void verifyBindFramebufferInterposition()
    {
        void* self = dlopen(nullptr, RTLD_LAZY);
        void* seen = self ? dlsym(self, "glBindFramebuffer") : nullptr;
        if (seen == (void*)&glBindFramebuffer)
        {
            if (!real_glBindFramebuffer)
                real_glBindFramebuffer = (void(APIENTRY*)(GLenum, GLuint))lin::realSym("glBindFramebuffer");
            Log("hooks: glBindFramebuffer interposition verified (dlsym(NULL) resolves to us)");
        }
        else
        {
            Log("hooks: dlsym(NULL, glBindFramebuffer) resolves to %p, not our interposer %p — "
                "engine binds may bypass the redirect", seen, (void*)&glBindFramebuffer);
        }
    }
}

FG_EXPORT void SDL_GL_SwapWindow(SDL_Window* window)
{
    static auto real = (void (*)(SDL_Window*))lin::realSdl("SDL_GL_SwapWindow");
    if (!real)
        return;
    if (!lin::armed())
    {
        real(window);
        return;
    }
    static std::once_flag verifyOnce;
    std::call_once(verifyOnce, verifyBindFramebufferInterposition);
    lin::setSwapWindow(window);
    applyMenuCursorState();
    if (!fg::OnSwapShared())
        real(window);
}

FG_EXPORT int SDL_PollEvent(SDL_Event* event)
{
    static auto real = (int (*)(SDL_Event*))lin::realSdl("SDL_PollEvent");
    if (!real)
        return 0;
    if (!lin::armed() || !event)
        return real(event);

    applyMenuCursorState();
    for (;;)
    {
        int r = real(event);
        if (!r)
            return 0;
        noteEvent(*event);
        // The game must not see input while the menu is open; route it to the
        // overlay's queue instead.
        if (fg::overlay::MenuOpen() && isInputEvent(*event))
        {
            std::lock_guard<std::mutex> lock(g_qmtx);
            if (g_qn < kQueueCap)
                g_queue[g_qn++] = *event;
            continue;
        }
        return r;
    }
}

// The game tears its GL context/window down on exit while our presenter
// thread is still presenting; stop FG first or the process segfaults inside
// the driver (and OpenMW's crash catcher turns a clean quit into a report).
FG_EXPORT void SDL_GL_DeleteContext(SDL_GLContext context)
{
    static auto real = (void (*)(SDL_GLContext))lin::realSdl("SDL_GL_DeleteContext");
    if (!real)
        return;
    if (lin::armed())
        fg::ShutdownIfActive();
    real(context);
}

FG_EXPORT void SDL_DestroyWindow(SDL_Window* window)
{
    static auto real = (void (*)(SDL_Window*))lin::realSdl("SDL_DestroyWindow");
    if (!real)
        return;
    if (lin::armed() && window == lin::swapWindow())
        fg::ShutdownIfActive();
    real(window);
}

FG_EXPORT int SDL_SetRelativeMouseMode(SDL_bool enabled)
{
    static auto real = (int (*)(SDL_bool))lin::realSdl("SDL_SetRelativeMouseMode");
    if (!real)
        return -1;
    g_gameRelative = enabled;
    if (lin::armed() && fg::overlay::MenuOpen())
        return 0; // pretend success; re-applied when the menu closes
    return real(enabled);
}

FG_EXPORT void SDL_WarpMouseInWindow(SDL_Window* window, int x, int y)
{
    static auto real = (void (*)(SDL_Window*, int, int))lin::realSdl("SDL_WarpMouseInWindow");
    if (!real)
        return;
    if (lin::armed() && fg::overlay::MenuOpen())
        return; // no recentering under the menu cursor
    real(window, x, y);
}

// -------------------------------------------------------------- GL interposers

namespace
{
    void APIENTRY hookGlBindFramebuffer(GLenum target, GLuint framebuffer)
    {
        fg::RemapBindFramebuffer(target, &framebuffer);
        real_glBindFramebuffer(target, framebuffer);
    }

    void APIENTRY hookGlBindFramebufferEXT(GLenum target, GLuint framebuffer)
    {
        fg::RemapBindFramebuffer(target, &framebuffer);
        real_glBindFramebufferEXT(target, framebuffer);
    }
}

typedef void (*GLXFuncPtr)(void);

FG_EXPORT GLXFuncPtr glXGetProcAddressARB(const unsigned char* name)
{
    static auto real = (GLXFuncPtr(*)(const unsigned char*))lin::realSym("glXGetProcAddressARB");
    GLXFuncPtr p = nullptr;
    if (real)
        p = real(name);
    else if (lin::glHandle())
        p = (GLXFuncPtr)dlsym(lin::glHandle(), (const char*)name);
    if (!p || !name || !lin::armed())
        return p;

    if (!strcmp((const char*)name, "glBindFramebuffer"))
    {
        real_glBindFramebuffer = (void(APIENTRY*)(GLenum, GLuint))p;
        return (GLXFuncPtr)hookGlBindFramebuffer;
    }
    if (!strcmp((const char*)name, "glBindFramebufferEXT"))
    {
        real_glBindFramebufferEXT = (void(APIENTRY*)(GLenum, GLuint))p;
        return (GLXFuncPtr)hookGlBindFramebufferEXT;
    }
    return p;
}

FG_EXPORT GLXFuncPtr glXGetProcAddress(const unsigned char* name)
{
    return glXGetProcAddressARB(name);
}

// Direct-linked variants: some engines resolve glBindFramebuffer straight from
// libGL's exports instead of GetProcAddress. Cheap insurance.
FG_EXPORT void glBindFramebuffer(GLenum target, GLuint framebuffer)
{
    if (!real_glBindFramebuffer)
        real_glBindFramebuffer = (void(APIENTRY*)(GLenum, GLuint))lin::realSym("glBindFramebuffer");
    if (!real_glBindFramebuffer)
        return;
    hookGlBindFramebuffer(target, framebuffer);
}

FG_EXPORT void glBindFramebufferEXT(GLenum target, GLuint framebuffer)
{
    if (!real_glBindFramebufferEXT)
        real_glBindFramebufferEXT = (void(APIENTRY*)(GLenum, GLuint))lin::realSym("glBindFramebufferEXT");
    if (!real_glBindFramebufferEXT)
        return;
    hookGlBindFramebufferEXT(target, framebuffer);
}

FG_EXPORT void glGetIntegerv(GLenum pname, GLint* params)
{
    if (!real_glGetIntegerv)
        real_glGetIntegerv = (void(APIENTRY*)(GLenum, GLint*))lin::realSym("glGetIntegerv");
    if (!real_glGetIntegerv)
        return;
    real_glGetIntegerv(pname, params);
    if (params && (pname == GL_FRAMEBUFFER_BINDING || pname == GL_READ_FRAMEBUFFER_BINDING))
        fg::SpoofFramebufferBinding(params);
}

FG_EXPORT void glDrawBuffer(GLenum buf)
{
    if (!real_glDrawBuffer)
        real_glDrawBuffer = (void(APIENTRY*)(GLenum))lin::realSym("glDrawBuffer");
    if (!real_glDrawBuffer)
        return;
    fg::RemapDrawBuffer(&buf);
    real_glDrawBuffer(buf);
}

FG_EXPORT void glReadBuffer(GLenum buf)
{
    if (!real_glReadBuffer)
        real_glReadBuffer = (void(APIENTRY*)(GLenum))lin::realSym("glReadBuffer");
    if (!real_glReadBuffer)
        return;
    fg::RemapReadBuffer(&buf);
    real_glReadBuffer(buf);
}
