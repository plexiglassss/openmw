#ifndef OPENMW_DIAGNOSTICS_POSTLOAD_H
#define OPENMW_DIAGNOSTICS_POSTLOAD_H

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

namespace Diagnostics::PostLoad
{
    using Clock = std::chrono::steady_clock;
    enum class Phase : std::size_t
    {
        Other, GcWait, Input, Sound, LuaSync, State, MWScript, Mechanics, Physics,
        World, Gui, Maintenance, SceneGraph, Focus, LuaStart, Render, LuaFinish, GcStart, LuaExecution, Count
    };
    inline constexpr std::size_t phaseCount = static_cast<std::size_t>(Phase::Count);
    inline constexpr std::array<std::string_view, phaseCount> phaseNames = {
        "other", "gcWait", "input", "sound", "luaSync", "state", "mwscriptAndTime", "mechanics", "physics",
        "worldAndPreload", "gui", "maintenance", "sceneGraph", "focus", "luaStart", "render", "luaFinish", "gcStart", "luaExecution"
    };
    enum class LoopPhase : std::size_t { Advance, Bookkeeping, Limiter, HiddenSleep, Count };
    inline constexpr std::size_t loopPhaseCount = static_cast<std::size_t>(LoopPhase::Count);
    inline constexpr std::array<std::string_view, loopPhaseCount> loopPhaseNames = {
        "viewerAdvance", "postFrameBookkeeping", "frameLimiter", "hiddenWindowSleep"
    };
    inline constexpr double captureWindowMs = 180000.0;
    inline constexpr double heartbeatIntervalMs = 5000.0;
    struct Stamp
    {
        std::uint64_t epoch = 0;
        Clock::time_point time{};
    };
    struct Metrics
    {
        std::array<double, phaseCount> phaseMs{};
        std::array<double, phaseCount> outerCellMs{};
        std::array<std::uint64_t, phaseCount> cellLoads{};
    };
    struct Activity
    {
        std::uint64_t id = 0;
        bool loop = false;
        std::size_t index = 0;
        Clock::time_point started{};
    };
    struct ActivityToken
    {
        Stamp stamp;
        Activity previous;
        std::uint64_t id = 0;
        bool lua = false;
    };
    struct WindowState
    {
        bool known = false;
        bool visible = false;
        bool focused = false;
        bool minimized = false;
        bool paused = false;
    };
    struct Snapshot
    {
        std::uint64_t epoch = 0;
        unsigned frames = 0;
        unsigned completedFrames = 0;
        double elapsedMs = 0;
        double lastFrameAgoMs = 0;
        Activity main;
        Activity lua;
        double mainMs = 0;
        double luaMs = 0;
        WindowState window;
        double windowStateAgeMs = 0;
        std::array<double, loopPhaseCount> loopTotalMs{};
        bool windowExpired = false;
    };
    struct Report
    {
        std::uint64_t epoch = 0;
        unsigned frames = 0;
        unsigned completedFrames = 0;
        bool partial = false;
        bool completed = false;
        bool firstCompleted = false;
        bool details = false;
        bool finished = false;
        double elapsedMs = 0;
        double frameMs = 0;
        Metrics frame;
        Metrics total;
        std::array<double, loopPhaseCount> loopTotalMs{};
    };

    // No logging or engine access while holding this mutex. Disabled sampling
    // costs an atomic load; counters and timing storage remain fixed in size.
    class Capture
    {
        std::atomic<bool> mActive{ false };
        std::mutex mMutex;
        std::uint64_t mEpoch = 0;
        Clock::time_point mArmed{};
        unsigned mFrames = 0;
        unsigned mCompleted = 0;
        unsigned mDetails = 0;
        Metrics mFrame;
        Metrics mTotal;
        std::array<double, loopPhaseCount> mLoopTotalMs{};
        Activity mMainActivity;
        Activity mLuaActivity;
        std::uint64_t mActivitySequence = 0;
        WindowState mWindow;
        Clock::time_point mWindowObserved{};
        Clock::time_point mLastFrame{};
        double mNextHeartbeatMs = 1000.0;
        bool mHeartbeatsFinished = false;

        static double milliseconds(Clock::duration duration)
        {
            return std::chrono::duration<double, std::milli>(duration).count();
        }
        void recordPhase(Stamp stamp, Phase phase, Clock::time_point now)
        {
            const auto start = stamp.epoch == 0 ? mArmed : std::max(stamp.time, mArmed);
            const double ms = milliseconds(now - start);
            const auto i = static_cast<std::size_t>(phase);
            mFrame.phaseMs[i] += ms;
            mTotal.phaseMs[i] += ms;
        }
        ActivityToken enterActivity(bool lua, bool loop, std::size_t index, Clock::time_point now)
        {
            if (!active())
                return {};
            const std::lock_guard<std::mutex> lock(mMutex);
            if (!active())
                return {};
            Activity& activity = lua ? mLuaActivity : mMainActivity;
            ActivityToken token{ { mEpoch, now }, activity, ++mActivitySequence, lua };
            activity = { token.id, loop, index, std::max(now, mArmed) };
            return token;
        }
        void restoreActivity(const ActivityToken& token)
        {
            Activity& activity = token.lua ? mLuaActivity : mMainActivity;
            if (token.stamp.epoch == mEpoch && activity.id == token.id)
                activity = token.previous;
        }

    public:
        bool active() const { return mActive.load(std::memory_order_relaxed); }
        void arm(Clock::time_point now = Clock::now())
        {
            const std::lock_guard<std::mutex> lock(mMutex);
            ++mEpoch;
            mArmed = now;
            mFrames = mCompleted = mDetails = 0;
            mFrame = {};
            mTotal = {};
            mLoopTotalMs = {};
            mMainActivity = mLuaActivity = {};
            mWindow = {};
            mLastFrame = now;
            mNextHeartbeatMs = 1000.0;
            mHeartbeatsFinished = false;
            mActive.store(true, std::memory_order_relaxed);
        }
        void cancel()
        {
            const std::lock_guard<std::mutex> lock(mMutex);
            mActive.store(false, std::memory_order_relaxed);
        }
        Stamp begin()
        {
            if (!active())
                return {};
            const std::lock_guard<std::mutex> lock(mMutex);
            if (!active())
                return {};
            return { mEpoch, Clock::now() };
        }
        void endPhase(Stamp stamp, Phase phase, Clock::time_point now = Clock::now())
        {
            if (!active())
                return;
            const std::lock_guard<std::mutex> lock(mMutex);
            if (!active() || (stamp.epoch != 0 && stamp.epoch != mEpoch))
                return;
            recordPhase(stamp, phase, now);
        }
        ActivityToken beginPhase(Phase phase)
        {
            return active() ? beginPhase(phase, Clock::now()) : ActivityToken{};
        }
        ActivityToken beginPhase(Phase phase, Clock::time_point now)
        {
            return enterActivity(phase == Phase::LuaExecution, false, static_cast<std::size_t>(phase), now);
        }
        void finishPhase(const ActivityToken& token, Phase phase, Clock::time_point now = Clock::now())
        {
            if (!active())
                return;
            const std::lock_guard<std::mutex> lock(mMutex);
            if (!active() || (token.stamp.epoch != 0 && token.stamp.epoch != mEpoch))
                return;
            // Successful loading can arm the recorder inside an existing scope.
            recordPhase(token.stamp, phase, now);
            restoreActivity(token);
        }
        ActivityToken beginLoop(LoopPhase phase)
        {
            return active() ? beginLoop(phase, Clock::now()) : ActivityToken{};
        }
        ActivityToken beginLoop(LoopPhase phase, Clock::time_point now)
        {
            return enterActivity(false, true, static_cast<std::size_t>(phase), now);
        }
        void finishLoop(const ActivityToken& token, LoopPhase phase, Clock::time_point now = Clock::now())
        {
            if (!active() || token.stamp.epoch == 0)
                return;
            const std::lock_guard<std::mutex> lock(mMutex);
            if (!active() || token.stamp.epoch != mEpoch)
                return;
            mLoopTotalMs[static_cast<std::size_t>(phase)] += milliseconds(now - token.stamp.time);
            restoreActivity(token);
        }
        void observeWindow(WindowState state, Clock::time_point now = Clock::now())
        {
            if (!active())
                return;
            const std::lock_guard<std::mutex> lock(mMutex);
            if (active())
            {
                mWindow = state;
                mWindowObserved = now;
            }
        }
        // Called by the diagnostic monitor, never by game-state code. Only
        // fixed recorder data is copied; SDL and engine objects stay on main.
        std::optional<Snapshot> heartbeat(Clock::time_point now = Clock::now())
        {
            if (!active())
                return {};
            const std::lock_guard<std::mutex> lock(mMutex);
            if (!active() || mHeartbeatsFinished)
                return {};
            const double elapsedMs = milliseconds(now - mArmed);
            if (elapsedMs < mNextHeartbeatMs)
                return {};
            mNextHeartbeatMs = elapsedMs + heartbeatIntervalMs;
            Snapshot result;
            result.epoch = mEpoch;
            result.frames = mFrames;
            result.completedFrames = mCompleted;
            result.elapsedMs = elapsedMs;
            result.lastFrameAgoMs = milliseconds(now - mLastFrame);
            result.main = mMainActivity;
            result.lua = mLuaActivity;
            result.mainMs = result.main.id ? milliseconds(now - result.main.started) : 0;
            result.luaMs = result.lua.id ? milliseconds(now - result.lua.started) : 0;
            result.window = mWindow;
            result.windowStateAgeMs = mWindow.known ? milliseconds(now - mWindowObserved) : 0;
            result.loopTotalMs = mLoopTotalMs;
            result.windowExpired = elapsedMs >= captureWindowMs;
            mHeartbeatsFinished = result.windowExpired;
            return result;
        }
        void cellLoaded(Stamp stamp, Phase phase, bool outer, Clock::time_point now = Clock::now())
        {
            if (!active() || stamp.epoch == 0)
                return;
            const std::lock_guard<std::mutex> lock(mMutex);
            if (!active() || stamp.epoch != mEpoch)
                return;
            const auto i = static_cast<std::size_t>(phase);
            ++mFrame.cellLoads[i];
            ++mTotal.cellLoads[i];
            if (outer)
            {
                const double ms = milliseconds(now - stamp.time);
                mFrame.outerCellMs[i] += ms;
                mTotal.outerCellMs[i] += ms;
            }
        }
        std::optional<Report> finishFrame(Stamp stamp, bool completed, Clock::time_point now = Clock::now())
        {
            if (!active())
                return {};
            const std::lock_guard<std::mutex> lock(mMutex);
            if (!active())
                return {};
            Report result;
            result.epoch = mEpoch;
            result.frames = ++mFrames;
            result.completed = completed;
            result.firstCompleted = completed && mCompleted == 0;
            if (completed)
                ++mCompleted;
            result.completedFrames = mCompleted;
            result.partial = stamp.epoch != mEpoch;
            result.frameMs = milliseconds(now - (result.partial ? mArmed : stamp.time));
            result.elapsedMs = milliseconds(now - mArmed);
            result.frame = mFrame;
            result.total = mTotal;
            result.loopTotalMs = mLoopTotalMs;
            mLastFrame = now;
            mFrame = {};
            result.details = mDetails < 80 && (mFrames <= 5 || result.frameMs >= 100.0);
            if (result.details)
                ++mDetails;
            // Retain a bounded long-tail window even after many fast frames.
            // Finish at a frame boundary so an outstanding long stall is kept;
            // heartbeat output independently stops at the wall-time bound.
            result.finished = result.elapsedMs >= captureWindowMs;
            if (result.finished)
                mActive.store(false, std::memory_order_relaxed);
            return result;
        }
    };

    inline Capture& capture()
    {
        static Capture value;
        return value;
    }
    inline thread_local Phase currentPhase = Phase::Other;
    inline thread_local unsigned cellLoadDepth = 0;
    inline thread_local std::uint64_t cellLoadEpoch = 0;

    class Scope
    {
        const Phase mPrevious = currentPhase;
        const Phase mPhase;
        const ActivityToken mToken;
    public:
        explicit Scope(Phase phase) : mPhase(phase), mToken(capture().beginPhase(phase)) { currentPhase = phase; }
        ~Scope()
        {
            if (capture().active())
                capture().finishPhase(mToken, mPhase);
            currentPhase = mPrevious;
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };

    class LoopScope
    {
        const LoopPhase mPhase;
        const ActivityToken mToken;
    public:
        explicit LoopScope(LoopPhase phase) : mPhase(phase), mToken(capture().beginLoop(phase)) {}
        ~LoopScope()
        {
            if (capture().active())
                capture().finishLoop(mToken, mPhase);
        }
        LoopScope(const LoopScope&) = delete;
        LoopScope& operator=(const LoopScope&) = delete;
    };

    class CellLoadScope
    {
        const Stamp mStamp = capture().begin();
        const Phase mPhase = currentPhase;
        bool mOuter = false;
    public:
        CellLoadScope()
        {
            if (mStamp.epoch != 0)
            {
                if (cellLoadEpoch != mStamp.epoch)
                {
                    cellLoadEpoch = mStamp.epoch;
                    cellLoadDepth = 0;
                }
                mOuter = cellLoadDepth++ == 0;
            }
        }
        ~CellLoadScope()
        {
            if (mStamp.epoch != 0)
            {
                if (cellLoadEpoch == mStamp.epoch)
                    --cellLoadDepth;
                if (capture().active())
                    capture().cellLoaded(mStamp, mPhase, mOuter);
            }
        }
        CellLoadScope(const CellLoadScope&) = delete;
        CellLoadScope& operator=(const CellLoadScope&) = delete;
    };
}
#endif
