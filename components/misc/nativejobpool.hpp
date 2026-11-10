#ifndef OPENMW_COMPONENTS_MISC_NATIVEJOBPOOL_H
#define OPENMW_COMPONENTS_MISC_NATIVEJOBPOOL_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Misc
{
    /// A small persistent pool for coarse, read-only native gameplay queries.
    ///
    /// The pool deliberately does not expose fire-and-forget work. Callers submit
    /// bounded parallel loops and wait for completion, so snapshots passed to the
    /// workers remain valid and no job can outlive the owning frame/query. The
    /// submitted function must be safe to call concurrently for different indices.
    class NativeJobPool
    {
    public:
        struct Statistics
        {
            std::uint64_t mParallelCalls = 0;
            std::uint64_t mSerialCalls = 0;
            std::uint64_t mItems = 0;
            std::uint64_t mTasks = 0;
        };

        NativeJobPool(std::size_t workerCount, std::size_t minimumItemsPerTask);
        NativeJobPool(const NativeJobPool&) = delete;
        NativeJobPool& operator=(const NativeJobPool&) = delete;
        ~NativeJobPool();

        std::size_t workerCount() const { return mThreads.size(); }
        std::size_t minimumItemsPerTask() const { return mMinimumItemsPerTask; }
        Statistics statistics() const;

        template <class Function>
        void parallelFor(std::size_t count, Function&& function)
        {
            parallelFor(count, mMinimumItemsPerTask, std::forward<Function>(function));
        }

        // Heavier kernels set their own (measured in items) grain size.
        template <class Function>
        void parallelFor(std::size_t count, std::size_t grain, Function&& function)
        {
            if (count == 0)
                return;

            const std::size_t taskCount = chooseTaskCount(count, grain);
            if (taskCount <= 1 || isWorkerThread())
            {
                mSerialCalls.fetch_add(1, std::memory_order_relaxed);
                mItems.fetch_add(count, std::memory_order_relaxed);
                for (std::size_t i = 0; i < count; ++i)
                    function(i);
                return;
            }

            struct State
            {
                std::atomic_size_t mRemaining{ 0 };
                std::mutex mMutex;
                std::condition_variable mDone;
                std::size_t mFirstErrorIndex = std::numeric_limits<std::size_t>::max();
                std::exception_ptr mFirstError;
            };

            auto state = std::make_shared<State>();
            state->mRemaining = taskCount;
            auto fn = std::make_shared<std::decay_t<Function>>(std::forward<Function>(function));
            auto runChunk = [state, fn, count, taskCount](std::size_t taskIndex) {
                const auto width = count / taskCount;
                const auto remainder = count % taskCount;
                const auto begin = taskIndex * width + std::min(taskIndex, remainder);
                const auto end = begin + width + (taskIndex < remainder);
                for (std::size_t index = begin; index < end; ++index)
                {
                    try
                    {
                        (*fn)(index);
                    }
                    catch (...)
                    {
                        std::lock_guard<std::mutex> lock(state->mMutex);
                        if (index < state->mFirstErrorIndex)
                        {
                            state->mFirstErrorIndex = index;
                            state->mFirstError = std::current_exception();
                        }
                    }
                }
                if (state->mRemaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    std::lock_guard<std::mutex> lock(state->mMutex);
                    state->mDone.notify_one();
                }
            };

            mParallelCalls.fetch_add(1, std::memory_order_relaxed);
            mItems.fetch_add(count, std::memory_order_relaxed);
            mTasks.fetch_add(taskCount, std::memory_order_relaxed);

            // Use fixed contiguous chunks. Per-item atomic dispatch costs more than
            // the distance/effect kernels this pool is intended to accelerate.
            // Keep one chunk on the calling thread to reduce queue traffic and to
            // guarantee forward progress if all workers are handling another query.
            // Construct every allocating callable before publishing any borrowed work.
            std::vector<std::function<void()>> tasks;
            tasks.reserve(taskCount - 1);
            for (std::size_t i = 1; i < taskCount; ++i)
                tasks.emplace_back([runChunk, i] { runChunk(i); });
            std::size_t queued = 0;
            try
            {
                for (; queued < tasks.size(); ++queued)
                    enqueue(std::move(tasks[queued]));
            }
            catch (...)
            {
                // Already-published jobs must finish before caller-owned inputs go out of scope.
                for (std::size_t i = queued + 1; i < taskCount; ++i)
                    runChunk(i);
            }
            runChunk(0);

            std::unique_lock<std::mutex> lock(state->mMutex);
            state->mDone.wait(lock, [&] { return state->mRemaining.load(std::memory_order_acquire) == 0; });
            if (state->mFirstError)
                std::rethrow_exception(state->mFirstError);
        }

    private:
        std::size_t chooseTaskCount(std::size_t count, std::size_t grain) const;
        void enqueue(std::function<void()> task);
        bool isWorkerThread() const;
        void run();

        const std::size_t mMinimumItemsPerTask;
        std::vector<std::thread> mThreads;
        std::vector<std::function<void()>> mQueue;
        std::size_t mQueueHead = 0;
        std::size_t mQueued = 0;
        mutable std::mutex mMutex;
        std::condition_variable mWorkAvailable;
        std::condition_variable mQueueSpaceAvailable;
        bool mStopping = false;

        std::atomic_uint64_t mParallelCalls{ 0 };
        std::atomic_uint64_t mSerialCalls{ 0 };
        std::atomic_uint64_t mItems{ 0 };
        std::atomic_uint64_t mTasks{ 0 };
    };
}

#endif
