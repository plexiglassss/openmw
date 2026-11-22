#include <components/terrain/arrivalview.hpp>
#include <components/terrain/viewdata.hpp>
#include <gtest/gtest.h>
#include <osg/Group>
#include <future>

namespace
{
    TEST(StagedArrival, GroundNeverCertifiesAnExactObjectQuery)
    {
        Terrain::ArrivalView arrival({}, {}, 0);
        osg::ref_ptr<osg::Node> ground = new osg::Group;
        osg::ref_ptr<osg::Node> full = new osg::Group;
        const Terrain::ArrivalView::Key key{ nullptr, 1 };
        arrival.publishGround({ key, ground, 64, {} });
        arrival.markActiveReady();
        EXPECT_EQ(arrival.find(key), nullptr);
        EXPECT_EQ(arrival.cameraNode(key, 1).node, ground);
        EXPECT_TRUE(arrival.cameraNode(key, 1).ground);
        auto ready = std::make_shared<std::atomic_bool>(false);
        arrival.publish({ { key, full, 128, ready } });
        EXPECT_EQ(arrival.find(key), full); // CPU data is already exact for a ray.
        EXPECT_EQ(arrival.cameraNode(key, 1).node, ground); // GL has not completed.
        ready->store(true, std::memory_order_release);
        EXPECT_EQ(arrival.cameraNode(key, 1).node, full);
        EXPECT_FALSE(arrival.cameraNode(key, 1).ground);
    }

    TEST(StagedArrival, CamerasShareAdmissionQuotaAndRequiredNodesBypassIt)
    {
        Terrain::ArrivalView arrival({}, {}, 0);
        osg::ref_ptr<osg::Node> ground = new osg::Group, full = new osg::Group;
        auto ready = std::make_shared<std::atomic_bool>(true);
        for (unsigned i = 0; i < 6; ++i)
        {
            arrival.publishGround({ { nullptr, i }, ground, 1, {} });
            arrival.publish({ { { nullptr, i }, full, 1, ready } });
        }
        for (unsigned i = 0; i < 4; ++i)
            EXPECT_FALSE(arrival.cameraNode({ nullptr, i }, 1).ground);
        EXPECT_TRUE(arrival.cameraNode({ nullptr, 4 }, 1).ground);
        EXPECT_FALSE(arrival.cameraNode({ nullptr, 0 }, 1).ground);
        arrival.publish({ { { nullptr, 99 }, full, 1, {} } });
        EXPECT_EQ(arrival.cameraNode({ nullptr, 99 }, 1).node, full);
        EXPECT_FALSE(arrival.cameraNode({ nullptr, 4 }, 2).ground);
        arrival.chargeTraversal(std::chrono::milliseconds(3));
        EXPECT_TRUE(arrival.cameraNode({ nullptr, 5 }, 2).ground);
    }

    TEST(StagedArrival, BackpressureUnblocksOnAdmissionOrCancellation)
    {
        Terrain::ArrivalView arrival({}, {}, 0);
        osg::ref_ptr<osg::Node> node = new osg::Group;
        std::atomic_bool abort{ false };
        auto ready = std::make_shared<std::atomic_bool>(true);
        arrival.publishGround({ { nullptr, 0 }, node, 1, {} });
        arrival.publish({ { { nullptr, 0 }, node, 256ull * 1024 * 1024, ready } });
        auto wait = std::async(std::launch::async, [&] { return arrival.waitForDistantCapacity(abort); });
        EXPECT_EQ(wait.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
        arrival.cameraNode({ nullptr, 0 }, 1);
        EXPECT_EQ(wait.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        EXPECT_TRUE(wait.get());
        arrival.publishGround({ { nullptr, 1 }, node, 1, {} });
        arrival.publish({ { { nullptr, 1 }, node, 256ull * 1024 * 1024, ready } });
        wait = std::async(std::launch::async, [&] { return arrival.waitForDistantCapacity(abort); });
        abort = true;
        EXPECT_EQ(wait.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        EXPECT_FALSE(wait.get());
    }

    TEST(StagedArrival, FailedContinuationDisablesStagedCoverage)
    {
        Terrain::ArrivalView arrival({}, {}, 0);
        arrival.markActiveReady();
        EXPECT_TRUE(arrival.activeReady());
        arrival.finish(false);
        EXPECT_FALSE(arrival.activeReady());
        Terrain::ViewDataEntry entry;
        entry.mRenderingNode = new osg::Group;
        entry.mRenderingGroundFallback = true;
        EXPECT_TRUE(entry.needsRendering(false, false));
    }
    TEST(StagedArrival, ExistingCameraCoverageStillWaitsForPendingGraphics)
    {
        Terrain::ArrivalView arrival({}, {}, 0);
        osg::ref_ptr<osg::Node> node = new osg::Group;
        std::atomic_bool abort{ false };
        const Terrain::ArrivalView::Key key{ nullptr, 1 };
        arrival.publishGround({ key, node, 1, {} });
        auto ready = std::make_shared<std::atomic_bool>(false);
        arrival.publish({ { key, node, 256ull * 1024 * 1024, ready } });
        auto wait = std::async(std::launch::async, [&] { return arrival.waitForDistantCapacity(abort); });
        EXPECT_EQ(wait.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
        arrival.acknowledgeVisible(key);
        EXPECT_EQ(wait.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
        EXPECT_TRUE(arrival.cameraNode(key, 1).ground);
        ready->store(true, std::memory_order_release);
        EXPECT_EQ(wait.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        EXPECT_TRUE(wait.get());
        EXPECT_FALSE(arrival.cameraNode(key, 1).ground);
    }

    TEST(StagedArrival, InvalidationReleasesBackpressureAndCancellationRemovesOwnedCompileSets)
    {
        Terrain::ArrivalView arrival({}, {}, 0);
        osg::ref_ptr<osg::Node> node = new osg::Group;
        arrival.mCompile = new osgUtil::IncrementalCompileOperation;
        osg::ref_ptr<osgUtil::IncrementalCompileOperation::CompileSet> set =
            new osgUtil::IncrementalCompileOperation::CompileSet(node);
        ASSERT_TRUE(arrival.enqueueCompile(set));
        EXPECT_FALSE(arrival.mCompile->getToCompile().empty());
        std::atomic_bool abort{ false }, valid{ true };
        arrival.publishGround({ { nullptr, 0 }, node, 1, {} });
        arrival.publish({ { { nullptr, 0 }, node, 256ull * 1024 * 1024, {} } });
        auto wait = std::async(std::launch::async,
            [&] { return arrival.waitForDistantCapacity(abort, [&] { return valid.load(); }); });
        EXPECT_EQ(wait.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
        valid = false;
        EXPECT_EQ(wait.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        EXPECT_FALSE(wait.get());
        arrival.cancel();
        EXPECT_TRUE(arrival.cancelled());
        EXPECT_TRUE(arrival.mCompile->getToCompile().empty());
        EXPECT_TRUE(arrival.mCompile->getCompiled().empty());
        EXPECT_FALSE(arrival.enqueueCompile(set));
        EXPECT_FALSE(arrival.activeReady());
    }

}
