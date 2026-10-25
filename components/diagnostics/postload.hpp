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
        bool frameLimit = false;
        double elapsedMs = 0;
        double frameMs = 0;
        Metrics frame;
        Metrics total;
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

        static double milliseconds(Clock::duration duration)
        {
            return std::chrono::duration<double, std::milli>(duration).count();
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
            // The load may finish inside an already-open input/state phase.
            const auto start = stamp.epoch == 0 ? mArmed : std::max(stamp.time, mArmed);
            const double ms = milliseconds(now - start);
            const auto i = static_cast<std::size_t>(phase);
            mFrame.phaseMs[i] += ms;
            mTotal.phaseMs[i] += ms;
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
            mFrame = {};
            result.details = mDetails < 40 && (mFrames <= 5 || result.frameMs >= 100.0);
            if (result.details)
                ++mDetails;
            // Finish at a frame boundary so a single long stall is not lost.
            // Keep at least five observations, even if the first exceeds 10 s.
            result.frameLimit = mFrames >= 10000;
            result.finished = (result.elapsedMs >= 10000.0 && mFrames >= 5) || result.frameLimit;
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
        const Stamp mStamp = capture().begin();
    public:
        explicit Scope(Phase phase) : mPhase(phase) { currentPhase = phase; }
        ~Scope()
        {
            if (capture().active())
                capture().endPhase(mStamp, mPhase);
            currentPhase = mPrevious;
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
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
