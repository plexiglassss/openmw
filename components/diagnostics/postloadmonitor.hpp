#ifndef OPENMW_DIAGNOSTICS_POSTLOADMONITOR_H
#define OPENMW_DIAGNOSTICS_POSTLOADMONITOR_H

#include "postload.hpp"

#include <condition_variable>
#include <system_error>
#include <thread>

namespace Diagnostics::PostLoad
{
    // Own this beside the engine loop and stop it before teardown. The callback
    // receives diagnostic snapshots only and must not access engine/SDL state.
    // Logging is outside both locks. A blocked logger can still delay delivery;
    // this is a bounded observer, not a guaranteed deadlock detector.
    class Monitor
    {
        Capture& mCapture;
        void (*mCallback)(const Snapshot&);
        const std::chrono::milliseconds mInterval;
        std::mutex mMutex;
        std::condition_variable mWake;
        bool mStop = false;
        std::atomic<bool> mFailed{ false };
        std::thread mThread;

        void run()
        {
            std::unique_lock<std::mutex> lock(mMutex);
            while (!mWake.wait_for(lock, mInterval, [this] { return mStop; }))
            {
                lock.unlock();
                try
                {
                    if (const auto report = mCapture.heartbeat())
                        mCallback(*report);
                }
                catch (...)
                {
                    mFailed.store(true, std::memory_order_relaxed);
                    return;
                }
                lock.lock();
            }
        }

    public:
        explicit Monitor(Capture& recorder, void (*callback)(const Snapshot&),
            std::chrono::milliseconds interval = std::chrono::seconds(1))
            : mCapture(recorder), mCallback(callback), mInterval(interval)
        {
            try
            {
                mThread = std::thread([this] { run(); });
            }
            catch (...)
            {
                mFailed.store(true, std::memory_order_relaxed);
            }
        }
        bool failed() const { return mFailed.load(std::memory_order_relaxed); }
        void stop()
        {
            {
                const std::lock_guard<std::mutex> lock(mMutex);
                mStop = true;
            }
            mWake.notify_all();
            if (mThread.joinable())
                mThread.join();
        }
        ~Monitor() { stop(); }
        Monitor(const Monitor&) = delete;
        Monitor& operator=(const Monitor&) = delete;
    };
}
#endif
