#include <components/terrain/preloadprediction.hpp>
#include <components/terrain/nextgrid.hpp>
#include <components/sceneutil/preparationcost.hpp>

#include <gtest/gtest.h>
#include <limits>
#include <thread>

namespace
{
    TEST(CostAwarePreload, FastContinuousMovementStillPreparesOneNeighbouringGrid)
    {
        const auto fast = Terrain::predictPreload(500, 250, 0.01, 2, 8192);
        EXPECT_FLOAT_EQ(fast.mX, 8192);
        EXPECT_FLOAT_EQ(fast.mY, 4096);
        EXPECT_GE(fast.mInterval, 0.025);
        EXPECT_LT(fast.mInterval, 0.25);
        EXPECT_EQ(Terrain::nextGridCells(-5, 3, 1, fast.mX, fast.mY, 8192).size(), 5u);
        const auto reversed = Terrain::predictPreload(-500, -250, 0.01, 2, 8192);
        EXPECT_FLOAT_EQ(reversed.mX, -8192);
        EXPECT_FLOAT_EQ(reversed.mY, -4096);
        const auto normal = Terrain::predictPreload(2, 0, 0.016, 0.5, 8192);
        EXPECT_FLOAT_EQ(normal.mX, 62.5);
        EXPECT_DOUBLE_EQ(normal.mInterval, 0.25);
    }

    TEST(CostAwarePreload, DiscontinuitiesAndInvalidInputsNeverProduceDistantRequests)
    {
        for (const auto prediction : { Terrain::predictPreload(9000, 0, 0.016, 3, 8192),
                 Terrain::predictPreload(10, 0, 1, 3, 8192), Terrain::predictPreload(0, 0, 0.016, 3, 8192),
                 Terrain::predictPreload(std::numeric_limits<float>::infinity(), 0, 0.016, 3, 8192),
                 Terrain::predictPreload(1, 0, 0, 3, 8192), Terrain::predictPreload(1, 0, 0.016, 3, 0),
                 Terrain::predictPreload(1, 0, 0.016, std::numeric_limits<double>::quiet_NaN(), 8192) })
        {
            EXPECT_FLOAT_EQ(prediction.mX, 0);
            EXPECT_FLOAT_EQ(prediction.mY, 0);
            EXPECT_DOUBLE_EQ(prediction.mInterval, 0.25);
        }
    }

    TEST(CostAwarePreload, ColdWorkAndQueueDelayExtendLeadWithoutIncreasingSpatialCoverage)
    {
        SceneUtil::PreparationCost cost;
        EXPECT_DOUBLE_EQ(cost.leadTime(0.5, 0), 0.5);
        cost.observe(0.2, 1.6, 40, 32);
        const double lead = cost.leadTime(0.5, 0);
        EXPECT_GT(lead, 2.0);
        EXPECT_GT(cost.leadTime(0.5, 3), lead);
        EXPECT_DOUBLE_EQ(cost.leadTime(0.5, 10000), 4.0);
        for (int i = 0; i < 100; ++i)
        {
            const auto predicted = Terrain::predictPreload(10000.f / 60, 0, 1.0 / 60, cost.leadTime(0.5, i), 8192);
            EXPECT_LE(predicted.mX, 8192);
            EXPECT_EQ(Terrain::nextGridCells(0, 0, 1, predicted.mX, predicted.mY, 8192).size(), 3u);
        }
    }

    TEST(CostAwarePreload, WarmSamplesDecayRecentLatencyAndIgnoreInvalidMeasurements)
    {
        SceneUtil::PreparationCost cost;
        cost.observe(2, 0.5, 10, 5);
        const double initial = cost.leadTime(0.5, 0);
        for (int i = 0; i < 30; ++i)
            cost.observe(0, 0.001, 10, 0);
        EXPECT_LT(cost.leadTime(0.5, 0), initial);
        EXPECT_DOUBLE_EQ(cost.leadTime(0.5, 0), 0.5);
        const auto count = cost.samples();
        cost.observe(-1, 2, 10, 5);
        cost.observe(0, std::numeric_limits<double>::quiet_NaN(), 10, 5);
        cost.observe(0, 1, 0, 0);
        cost.observe(0, 1, 1, 2);
        EXPECT_EQ(cost.samples(), count);
    }

    TEST(CostAwarePreload, WorkerPublishesCompleteSamplesAndExcludesCancellationAndFailure)
    {
        SceneUtil::PreparationSample sample;
        std::atomic_bool abort{false};
        std::thread worker([&] {
            SceneUtil::PreparationSample::Run run(&sample, abort);
            sample.mAssets = 20;
            sample.mColdAssets = 7;
        });
        while (!sample.mComplete.load(std::memory_order_acquire))
            std::this_thread::yield();
        EXPECT_TRUE(sample.mSuccessful);
        EXPECT_EQ(sample.mAssets, 20u);
        EXPECT_EQ(sample.mColdAssets, 7u);
        EXPECT_GE(sample.mQueueSeconds, 0);
        EXPECT_GE(sample.mWorkSeconds, 0);
        worker.join();
        SceneUtil::PreparationSample cancelled;
        { SceneUtil::PreparationSample::Run run(&cancelled, abort); abort = true; }
        EXPECT_TRUE(cancelled.mComplete.load());
        EXPECT_FALSE(cancelled.mSuccessful);
        SceneUtil::PreparationSample failed;
        abort = false;
        try { SceneUtil::PreparationSample::Run run(&failed, abort); throw 1; } catch (int) {}
        EXPECT_TRUE(failed.mComplete.load());
        EXPECT_FALSE(failed.mSuccessful);
    }
}
