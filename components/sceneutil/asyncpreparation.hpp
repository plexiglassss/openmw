#ifndef OPENMW_COMPONENTS_SCENEUTIL_ASYNCPREPARATION_H
#define OPENMW_COMPONENTS_SCENEUTIL_ASYNCPREPARATION_H

#include "workqueue.hpp"
#include "unrefqueue.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <utility>

namespace SceneUtil
{
    // The owner thread only requests and polls. Factories always execute on the
    // work queue. A separate completion gate can cover context-bound GPU work.
    // Values and gates are immutable after the worker publishes mPrepared.
    template <class Key, class Value>
    class AsyncPreparation
    {
    public:
        struct Prepared
        {
            Value mValue{};
            std::shared_ptr<std::atomic_bool> mCompletion;
            std::function<void()> mCancel;
            std::size_t mEstimatedBytes = 0;
        };
        using Factory = std::function<Prepared(const std::atomic_bool&)>;
        struct Result
        {
            bool mReady = false;
            bool mFailed = false;
            Value mValue{};
        };

    private:
        class Job final : public WorkItem
        {
        public:
            explicit Job(Factory factory) : mFactory(std::move(factory)) {}
            void abort() override
            {
                mAbort.store(true, std::memory_order_release);
                cancelPrepared();
            }
            void cancelPrepared()
            {
                if (mPrepared.load(std::memory_order_acquire) && !mCancelled.exchange(true))
                    if (mResult.mCancel)
                        mResult.mCancel();
            }
            void doWork() override
            {
                if (!mAbort.load(std::memory_order_relaxed))
                {
                    try { mResult = mFactory(mAbort); }
                    catch (...) { mFailed = true; }
                }
                mFactory = {};
                mPrepared.store(true, std::memory_order_release);
                if (mAbort.load(std::memory_order_acquire))
                    cancelPrepared();
            }
            bool ready() const
            {
                return mPrepared.load(std::memory_order_acquire) && !mFailed && !mAbort
                    && (!mResult.mCompletion || mResult.mCompletion->load(std::memory_order_acquire));
            }
            bool failed() const { return mPrepared.load(std::memory_order_acquire) && mFailed; }
            Factory mFactory;
            Prepared mResult;
            std::atomic_bool mAbort{ false };
            std::atomic_bool mPrepared{ false };
            std::atomic_bool mCancelled{ false };
            bool mFailed = false;
        };
        struct Entry
        {
            osg::ref_ptr<Job> mJob;
            double mLastUse = 0;
        };

    public:
        explicit AsyncPreparation(WorkQueue* queue, std::size_t maximumPending = 2,
            std::size_t maximumEntries = 64, std::size_t estimatedByteBudget = 1024ull * 1024 * 1024)
            : mQueue(queue), mMaximumPending(maximumPending), mMaximumEntries(maximumEntries),
              mEstimatedByteBudget(estimatedByteBudget) {}
        ~AsyncPreparation()
        {
            clear();
            // Only destruction joins: the owner and all factory inputs remain alive.
            for (const auto& job : mRetired)
                job->waitTillDone();
        }
        AsyncPreparation(const AsyncPreparation&) = delete;
        AsyncPreparation& operator=(const AsyncPreparation&) = delete;

        void clear()
        {
            for (auto& [key, entry] : mEntries)
            {
                mQueue->cancelWorkItem(entry.mJob.get());
                mRetired.push_back(std::move(entry.mJob));
            }
            mEntries.clear();
        }

        void update(double now)
        {
            if (now == mLastUpdate)
                return;
            mLastUpdate = now;
            for (auto it = mEntries.begin(); it != mEntries.end();)
            {
                const double retention = it->second.mJob->ready() ? 0.25 : 1.0;
                if (it->second.mLastUse + retention < now)
                {
                    mQueue->cancelWorkItem(it->second.mJob.get());
                    mRetired.push_back(std::move(it->second.mJob));
                    it = mEntries.erase(it);
                }
                else
                    ++it;
            }
            for (auto it = mRetired.begin(); it != mRetired.end();)
            {
                if ((*it)->isDone())
                {
                    mUnref.push(osg::ref_ptr<osg::Referenced>(std::move(*it)));
                    it = mRetired.erase(it);
                }
                else
                    ++it;
            }
            mUnref.flush(*mQueue);
        }

        Result request(const Key& key, double now, Factory factory)
        {
            auto found = mEntries.find(key);
            if (found != mEntries.end())
            {
                auto& entry = found->second;
                // A failed factory retries only after the entry ages out. Keep
                // that deadline fixed rather than retrying once per cull.
                if (!entry.mJob->failed())
                    entry.mLastUse = now;
                if (entry.mJob->ready())
                    return { true, false, entry.mJob->mResult.mValue };
                return { false, entry.mJob->failed(), {} };
            }
            std::size_t pending = 0;
            std::size_t estimatedBytes = 0;
            for (const auto& [existing, entry] : mEntries)
            {
                pending += !entry.mJob->ready() && !entry.mJob->failed();
                estimatedBytes += entry.mJob->mPrepared.load(std::memory_order_acquire)
                    ? entry.mJob->mResult.mEstimatedBytes : 64ull * 1024 * 1024;
            }
            for (const auto& job : mRetired)
                pending += !job->isDone();
            if (pending >= mMaximumPending || mEntries.size() >= mMaximumEntries
                || estimatedBytes >= mEstimatedByteBudget)
                return {};
            osg::ref_ptr<Job> job = new Job(std::move(factory));
            mEntries.emplace(key, Entry{ job, now });
            try
            {
                mQueue->addWorkItem(job);
            }
            catch (...)
            {
                // A job that never entered the queue must not be left for the
                // destructor to wait on.
                mEntries.erase(key);
                throw;
            }
            ++mSubmitted;
            return {};
        }

        std::size_t submitted() const { return mSubmitted; }
        std::size_t size() const { return mEntries.size(); }

    private:
        osg::ref_ptr<WorkQueue> mQueue;
        const std::size_t mMaximumPending;
        const std::size_t mMaximumEntries;
        // Admission uses a reservation until the worker can measure its graph.
        // An indivisible decode may exceed the estimate; it blocks further admission.
        const std::size_t mEstimatedByteBudget;
        std::map<Key, Entry> mEntries;
        std::vector<osg::ref_ptr<Job>> mRetired;
        UnrefQueue mUnref;
        double mLastUpdate = -1;
        std::size_t mSubmitted = 0;
    };
}
#endif
