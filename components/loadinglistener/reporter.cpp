#include "reporter.hpp"
#include "loadinglistener.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace Loading
{
    void Reporter::addTotal(std::size_t value)
    {
        const std::lock_guard lock(mMutex);
        mTotal += value;
        mUpdated.notify_all();
    }

    void Reporter::addProgress(std::size_t value)
    {
        const std::lock_guard lock(mMutex);
        mProgress += value;
        mUpdated.notify_all();
    }

    void Reporter::complete()
    {
        const std::lock_guard lock(mMutex);
        mDone = true;
        mUpdated.notify_all();
    }

    void Reporter::wait(Listener& listener) const
    {
        std::unique_lock lock(mMutex);
        while (!mDone)
        {
            listener.setProgressRange(mTotal);
            listener.setProgress(mProgress);
            mUpdated.wait(lock);
        }
    }

    void Reporter::waitWithStats(Listener& listener, WaitStats& stats) const
    {
        using Clock = std::chrono::steady_clock;
        const auto milliseconds = [](Clock::duration duration) {
            return std::chrono::duration<double, std::milli>(duration).count();
        };
        stats = {};
        const auto start = Clock::now();
        std::unique_lock lock(mMutex);
        stats.initialLockMs = milliseconds(Clock::now() - start);
        while (!mDone)
        {
            const auto listenerStart = Clock::now();
            listener.setProgressRange(mTotal);
            listener.setProgress(mProgress);
            stats.listenerMs += milliseconds(Clock::now() - listenerStart);
            ++stats.progressUpdates;
            const auto conditionStart = Clock::now();
            mUpdated.wait(lock);
            stats.conditionMs += milliseconds(Clock::now() - conditionStart);
            ++stats.conditionWaits;
        }
        stats.elapsedMs = milliseconds(Clock::now() - start);
    }
}
