#ifndef OPENMW_COMPONENTS_MISC_OWNEDJOBS_H
#define OPENMW_COMPONENTS_MISC_OWNEDJOBS_H

#include "nativejobpool.hpp"

#include <optional>
#include <string>

namespace Misc
{
    struct OwnedJobState
    {
        virtual ~OwnedJobState() = default;
        virtual void discard() = 0;
        // Cancellation never waits for the computation. Only publishing a finished value takes this lock.
        void cancel()
        {
            mCancelled.store(true);
            std::lock_guard lock(mPublication);
            if (mDone.load(std::memory_order_acquire))
                discard();
        }
        std::mutex mPublication;
        std::atomic_bool mDone{ false };
        std::atomic_bool mCancelled{ false };
        bool mPublished = false; // Owner-thread fields below.
        bool mConsumed = false;
        std::exception_ptr mError;
    };

    template <class T>
    struct OwnedJob : OwnedJobState
    {
        std::optional<T> mResult;
        void discard() override { mResult.reset(); mError = {}; }
    };

    // Only the calling script thread touches the registry. A worker owns its copied input and
    // result slot. Workers never hold a pointer to this registry, the world, or a calling VM.
    class OwnedJobs
    {
    public:
        explicit OwnedJobs(NativeJobPool& pool) : mPool(pool) {}
        ~OwnedJobs() { clear(); }

        bool hasRoom()
        {
            publish();
            return mPool.workerCount() != 0 && mActive.size() < 16;
        }

        template <class T, class F>
        std::shared_ptr<OwnedJob<T>> submit(F work)
        {
            if (!hasRoom())
                return {};
            auto job = std::make_shared<OwnedJob<T>>();
            mActive.emplace_back(job);
            if (mPool.trySubmit([job, work = std::move(work)]() mutable {
                    std::optional<T> value;
                    std::exception_ptr error;
                    try
                    {
                        if (!job->mCancelled.load())
                            value.emplace(work(job->mCancelled));
                    }
                    catch (...)
                    {
                        error = std::current_exception();
                    }
                    std::lock_guard lock(job->mPublication);
                    if (!job->mCancelled.load())
                    {
                        job->mResult = std::move(value);
                        job->mError = std::move(error);
                    }
                    job->mDone.store(true, std::memory_order_release);
                }))
                return job;
            mActive.pop_back();
            return {};
        }

        void publish()
        {
            bool prefixDone = true;
            std::erase_if(mActive, [&](const auto& weak) {
                auto state = weak.lock();
                if (!state || state->mConsumed)
                    return true;
                if (state->mCancelled.load())
                {
                    if (!state->mDone.load(std::memory_order_acquire))
                        return false;
                    state->discard();
                    return true;
                }
                prefixDone = prefixDone && state->mDone.load(std::memory_order_acquire);
                state->mPublished = prefixDone;
                return false;
            });
        }

        void clear()
        {
            for (const auto& weak : mActive)
                if (auto state = weak.lock())
                    state->cancel();
            mActive.clear();
        }

    private:
        NativeJobPool& mPool;
        std::vector<std::weak_ptr<OwnedJobState>> mActive;
    };
}
#endif
