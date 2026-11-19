#ifndef OPENMW_COMPONENTS_SCENEUTIL_PREPARATIONCOST_H
#define OPENMW_COMPONENTS_SCENEUTIL_PREPARATIONCOST_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>

namespace SceneUtil
{
    // One worker writes a sample and publishes it once. The owner never reads
    // its fields until mComplete has acquired the publication.
    struct PreparationSample
    {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point mQueued = Clock::now();
        std::atomic_bool mComplete{ false };
        double mQueueSeconds = 0;
        double mWorkSeconds = 0;
        std::size_t mAssets = 0;
        std::size_t mColdAssets = 0;
        bool mSuccessful = false;

        double age() const { return std::chrono::duration<double>(Clock::now() - mQueued).count(); }

        struct Run
        {
            PreparationSample* mSample;
            const std::atomic_bool& mAbort;
            const Clock::time_point mStart = Clock::now();
            const int mExceptions = std::uncaught_exceptions();
            Run(PreparationSample* sample, const std::atomic_bool& abort) : mSample(sample), mAbort(abort) {}
            ~Run()
            {
                if (!mSample)
                    return;
                mSample->mQueueSeconds = std::chrono::duration<double>(mStart - mSample->mQueued).count();
                mSample->mWorkSeconds = std::chrono::duration<double>(Clock::now() - mStart).count();
                mSample->mSuccessful = !mAbort.load(std::memory_order_relaxed)
                    && std::uncaught_exceptions() == mExceptions;
                mSample->mComplete.store(true, std::memory_order_release);
            }
        };
    };

    class PreparationCost
    {
    public:
        void observe(double queue, double work, std::size_t assets, std::size_t cold)
        {
            if (!std::isfinite(queue) || !std::isfinite(work) || queue < 0 || work < 0 || !assets || cold > assets)
                return;
            // React immediately to a costly cell; decay gradually on warm ones.
            // No disk cache, reference scan or driver query is needed here.
            mLatency = std::max(std::min(4.0, queue + work), mLatency * 0.875);
            if (cold)
            {
                const double perAsset = std::min(4.0, work / cold);
                mColdAssetSeconds = mColdAssetSeconds > 0 ? 0.75 * mColdAssetSeconds + 0.25 * perAsset : perAsset;
            }
            mColdAssets = mSamples ? 0.75 * mColdAssets + 0.25 * cold : cold;
            ++mSamples;
        }

        double leadTime(double configured, double oldestPending) const
        {
            configured = std::isfinite(configured) ? std::clamp(configured, 0.0, 4.0) : 0.0;
            oldestPending = std::isfinite(oldestPending) ? std::clamp(oldestPending, 0.0, 4.0) : 0.0;
            const double measured = std::max({ mLatency, mColdAssetSeconds * mColdAssets, oldestPending });
            return std::clamp(std::max(configured, measured * 1.25 + (measured > 0 ? 0.1 : 0.0)), 0.0, 4.0);
        }
        std::size_t samples() const { return mSamples; }

    private:
        double mLatency = 0;
        double mColdAssetSeconds = 0;
        double mColdAssets = 0;
        std::size_t mSamples = 0;
    };
}

#endif
