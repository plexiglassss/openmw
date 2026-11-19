#include "nativejobpool.hpp"

#include <stdexcept>
#include <system_error>

namespace Misc
{
    namespace
    {
        thread_local const NativeJobPool* sWorkerPool = nullptr;
    }

    NativeJobPool::NativeJobPool(std::size_t workerCount, std::size_t minimumItemsPerTask)
        : mMinimumItemsPerTask(std::max<std::size_t>(1, minimumItemsPerTask))
        , mQueue(std::max<std::size_t>(32, workerCount * 8))
    {
        mThreads.reserve(workerCount);
        try
        {
            for (std::size_t i = 0; i < workerCount; ++i)
                mThreads.emplace_back([this] { run(); });
        }
        catch (const std::system_error&)
        {
            {
                std::lock_guard<std::mutex> lock(mMutex);
                mStopping = true;
            }
            mWorkAvailable.notify_all();
            for (std::thread& thread : mThreads)
                if (thread.joinable())
                    thread.join();
            mThreads.clear();
            mStopping = false;
        }
    }

    NativeJobPool::~NativeJobPool()
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mStopping = true;
        }
        mWorkAvailable.notify_all();
        mQueueSpaceAvailable.notify_all();
        for (std::thread& thread : mThreads)
            if (thread.joinable())
                thread.join();
    }

    NativeJobPool::Statistics NativeJobPool::statistics() const
    {
        return {
            .mParallelCalls = mParallelCalls.load(std::memory_order_relaxed),
            .mSerialCalls = mSerialCalls.load(std::memory_order_relaxed),
            .mItems = mItems.load(std::memory_order_relaxed),
            .mTasks = mTasks.load(std::memory_order_relaxed),
        };
    }

    std::size_t NativeJobPool::chooseTaskCount(std::size_t count, std::size_t grain) const
    {
        grain = std::max<std::size_t>(grain, 1);
        return std::max<std::size_t>(1, std::min(mThreads.size() + 1, count / grain));
    }

    void NativeJobPool::enqueue(std::function<void()> task)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mQueueSpaceAvailable.wait(lock, [&] {
            return mStopping || mQueued < mQueue.size();
        });
        if (mStopping)
            throw std::runtime_error("Native job pool is stopping");
        mQueue[(mQueueHead + mQueued) % mQueue.size()] = std::move(task);
        ++mQueued;
        lock.unlock();
        mWorkAvailable.notify_one();
    }

    bool NativeJobPool::trySubmit(std::function<void()> task)
    {
        {
            std::lock_guard lock(mMutex);
            if (mStopping || mThreads.empty() || mQueued == mQueue.size())
                return false;
            mQueue[(mQueueHead + mQueued) % mQueue.size()] = std::move(task);
            ++mQueued;
        }
        mWorkAvailable.notify_one();
        return true;
    }

    bool NativeJobPool::isWorkerThread() const
    {
        return sWorkerPool == this;
    }

    void NativeJobPool::run()
    {
        sWorkerPool = this;
        for (;;)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mWorkAvailable.wait(lock, [&] { return mStopping || mQueued != 0; });
                if (mStopping && mQueued == 0)
                    break;
                task = std::move(mQueue[mQueueHead]);
                mQueueHead = (mQueueHead + 1) % mQueue.size();
                --mQueued;
            }
            mQueueSpaceAvailable.notify_one();
            task();
        }
        sWorkerPool = nullptr;
    }
}
