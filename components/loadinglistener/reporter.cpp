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
        {
            const std::lock_guard lock(mMutex);
            mTotal += value;
            ++mRevision;
        }
        mUpdated.notify_all();
    }

    void Reporter::addProgress(std::size_t value)
    {
        {
            const std::lock_guard lock(mMutex);
            mProgress += value;
            ++mRevision;
        }
        mUpdated.notify_all();
    }

    void Reporter::complete()
    {
        {
            const std::lock_guard lock(mMutex);
            mDone = true;
        }
        mUpdated.notify_all();
    }

    void Reporter::wait(Listener& listener) const
    {
        std::unique_lock lock(mMutex);
        while (!mDone)
        {
            const auto total = mTotal;
            const auto progress = mProgress;
            const auto revision = mRevision;
            // A listener can render, compile graphics resources, or handle input.
            // Do not stop the resource workers while the main thread does that work.
            lock.unlock();
            listener.setProgressRange(total);
            listener.setProgress(progress);
            lock.lock();
            // Updates can arrive during either callback. A predicate avoids losing
            // those notifications before entering the condition-variable wait.
            mUpdated.wait(lock, [&] { return mDone || mRevision != revision; });
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
            const auto total = mTotal;
            const auto progress = mProgress;
            const auto revision = mRevision;
            lock.unlock();
            const auto listenerStart = Clock::now();
            listener.setProgressRange(total);
            listener.setProgress(progress);
            stats.listenerMs += milliseconds(Clock::now() - listenerStart);
            ++stats.progressUpdates;
            const auto conditionStart = Clock::now();
            lock.lock();
            mUpdated.wait(lock, [&] { return mDone || mRevision != revision; });
            stats.conditionMs += milliseconds(Clock::now() - conditionStart);
            ++stats.conditionWaits;
        }
        stats.elapsedMs = milliseconds(Clock::now() - start);
    }
}
