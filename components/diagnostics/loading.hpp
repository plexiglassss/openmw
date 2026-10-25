#ifndef OPENMW_DIAGNOSTICS_LOADING_H
#define OPENMW_DIAGNOSTICS_LOADING_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <ostream>
#include <string_view>

namespace Diagnostics::Loading
{
    using Clock = std::chrono::steady_clock;
    inline double milliseconds(Clock::duration duration)
    {
        return std::chrono::duration<double, std::milli>(duration).count();
    }

    enum class Stage : std::size_t
    {
        PlayerWorldSetup, PlayerBaseSetup, PlayerLuaSetup, PlayerRender, PlayerUi, PlayerMechanics, PlayerCamera,
        ViewReset, QuadTree, ViewSelection, RenderNodes, ProgressReporting, TerrainChunks, ObjectChunks,
        GroundcoverChunks, ObjectReferences, ObjectReferenceRead, ObjectSelection, ObjectTemplates,
        ObjectAnalysis, ObjectInstances, ObjectMerge, ObjectCompile, ObjectFinish,
        TemplateRequest, TemplateCache, TemplatePrepare, TemplateLoad, TemplateFiltering,
        TemplateShaders, TemplateOptimize, TemplateCompile, TemplatePublish, TemplateWait, Count
    };
    inline constexpr std::size_t stageCount = static_cast<std::size_t>(Stage::Count);
    inline constexpr std::array<std::string_view, stageCount> stageNames = {
        "playerWorldSetup", "playerBaseSetup", "playerLuaSetup", "playerRender", "playerUi", "playerMechanics",
        "playerCamera", "viewReset", "quadTree", "viewSelection", "renderNodes", "progressReporting",
        "terrainChunks", "objectChunks", "groundcoverChunks", "objectReferences", "objectReferenceRead",
        "objectSelection", "objectTemplates", "objectAnalysis", "objectInstances", "objectMerge", "objectCompile",
        "objectFinish", "templateRequest", "templateCache", "templatePrepare", "templateLoad", "templateFiltering",
        "templateShaders", "templateOptimize", "templateCompile", "templatePublish", "templateWait"
    };

    struct Metrics
    {
        std::array<double, stageCount> elapsedMs{};
        std::array<std::uint64_t, stageCount> calls{};
        std::array<unsigned, stageCount> depth{};
    };
    inline thread_local Metrics* currentMetrics = nullptr;

    class Session
    {
        Metrics* const mPrevious = currentMetrics;
    public:
        explicit Session(Metrics& metrics) { currentMetrics = &metrics; }
        ~Session() { currentMetrics = mPrevious; }
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
    };

    class Timer
    {
        Metrics* const mMetrics = currentMetrics;
        const std::size_t mIndex;
        Clock::time_point mStart{};
        bool mOuter = false;
    public:
        explicit Timer(Stage stage) : mIndex(static_cast<std::size_t>(stage))
        {
            // Outside a player/terrain capture this is only a TLS check.
            if (mMetrics)
            {
                ++mMetrics->calls[mIndex];
                mOuter = mMetrics->depth[mIndex]++ == 0;
                if (mOuter)
                    mStart = Clock::now();
            }
        }
        ~Timer()
        {
            if (mMetrics)
            {
                --mMetrics->depth[mIndex];
                if (mOuter)
                    mMetrics->elapsedMs[mIndex] += milliseconds(Clock::now() - mStart);
            }
        }
        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;
    };

    inline std::ostream& operator<<(std::ostream& stream, const Metrics& metrics)
    {
        for (std::size_t i = 0; i < stageCount; ++i)
            if (metrics.calls[i])
                stream << ' ' << stageNames[i] << "Ms=" << metrics.elapsedMs[i] << "/calls=" << metrics.calls[i];
        return stream;
    }

    struct JobSnapshot
    {
        std::uint64_t id = 0;
        bool enqueueRecorded = false;
        bool started = false;
        bool finished = false;
        bool abortRequested = false;
        bool exception = false;
        double enqueueToStartMs = 0;
        double workMs = 0;
        Metrics metrics;
    };

    class Job
    {
        inline static std::atomic<std::uint64_t> sNextId{ 1 };
        mutable std::mutex mMutex;
        Clock::time_point mEnqueued{};
        Clock::time_point mStarted{};
        JobSnapshot mSnapshot;
    public:
        Job() { mSnapshot.id = sNextId.fetch_add(1, std::memory_order_relaxed); }
        void enqueued(Clock::time_point now = Clock::now())
        {
            const std::lock_guard lock(mMutex);
            mEnqueued = now;
            mSnapshot.enqueueRecorded = true;
        }
        void started(Clock::time_point now = Clock::now())
        {
            const std::lock_guard lock(mMutex);
            mStarted = now;
            mSnapshot.started = true;
            if (mSnapshot.enqueueRecorded)
                mSnapshot.enqueueToStartMs = milliseconds(now - mEnqueued);
        }
        void finished(const Metrics& metrics, bool abortRequested, bool exception,
            Clock::time_point now = Clock::now())
        {
            const std::lock_guard lock(mMutex);
            mSnapshot.workMs = milliseconds(now - mStarted);
            mSnapshot.metrics = metrics;
            mSnapshot.abortRequested = abortRequested;
            mSnapshot.exception = exception;
            mSnapshot.finished = true;
        }
        JobSnapshot snapshot() const
        {
            const std::lock_guard lock(mMutex);
            return mSnapshot;
        }
    };

    class JobRun
    {
        Job& mJob;
        const std::atomic<bool>& mAbort;
        const int mExceptions = std::uncaught_exceptions();
        Metrics mMetrics;
        Session mSession{ mMetrics };
    public:
        JobRun(Job& job, const std::atomic<bool>& abort) : mJob(job), mAbort(abort) { mJob.started(); }
        ~JobRun()
        {
            mJob.finished(mMetrics, mAbort.load(std::memory_order_relaxed), std::uncaught_exceptions() > mExceptions);
        }
        JobRun(const JobRun&) = delete;
        JobRun& operator=(const JobRun&) = delete;
    };
}
#endif
