#include "apps/openmw/mwworld/cellpreloader.hpp"

#include <components/loadinglistener/loadinglistener.hpp>
#include <components/terrain/world.hpp>
#include <components/terrain/view.hpp>
#include <components/misc/combinedperformance.hpp>
#include <osg/Group>
#include <gtest/gtest.h>
#include <future>

namespace
{
    class ArrivalWorld : public Terrain::World
    {
        struct View : Terrain::View { void reset() override {} };
    public:
        std::promise<void> mStarted;
        std::promise<void> mRelease;
        explicit ArrivalWorld(osg::Group* parent) : World(parent, nullptr, ~0u, {}) {}
        std::unique_ptr<Terrain::View> createView() override { return std::make_unique<View>(); }
        std::shared_ptr<Terrain::ArrivalView> createArrival(const osg::Vec3f& point, const osg::Vec4i& grid) override
        { return std::make_shared<Terrain::ArrivalView>(point, grid, 0); }
        bool validArrival(const Terrain::ArrivalView&) const override { return true; }
        void preloadArrival(Terrain::View*, const std::shared_ptr<Terrain::ArrivalView>& arrival,
            std::atomic_bool& abort, Loading::Reporter&, const Terrain::ArrivalView::Progress& nearReady) override
        {
            if (arrival->mPoint.x() == 0)
            {
                mStarted.set_value();
                mRelease.get_future().wait(); // Deliberately non-preemptible current unit of work.
            }
            if (!abort)
            {
                nearReady(1, 0);
                arrival->finish(true);
            }
        }
    };

    TEST(ArrivalCancellation, RequiredArrivalDoesNotJoinAnObsoleteRunningPrediction)
    {
        if (!Misc::useResponsiveArrivalCancellation())
            GTEST_SKIP() << "Previous cancellation behavior selected";
        osg::ref_ptr<osg::Group> parent = new osg::Group;
        ArrivalWorld world(parent);
        std::shared_ptr<SceneUtil::WorkQueue> queue = std::make_shared<SceneUtil::WorkQueue>(2);
        MWWorld::CellPreloader loader(nullptr, nullptr, &world, nullptr);
        loader.setWorkQueue(queue);
        loader.predictArrival(&world, { {}, {} });
        world.mStarted.get_future().wait();
        loader.cancelPrediction();
        Loading::Listener listener;
        auto required = std::async(std::launch::async, [&] {
            return loader.ensureArrival(&world, { { 1024, 0, 0 }, {} }, listener, 0);
        });
        const bool ready = required.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        // Always unblock before asserting, so a failed regression cannot strand the runner.
        world.mRelease.set_value();
        EXPECT_TRUE(ready);
        EXPECT_TRUE(required.get());
        loader.clearArrivals(); // Joins the retired owner before world destruction.
    }
    class StagedWorld : public ArrivalWorld
    {
    public:
        using ArrivalWorld::ArrivalWorld;
        std::promise<void> mNear, mFarRelease;
        void preloadArrival(Terrain::View*, const std::shared_ptr<Terrain::ArrivalView>& arrival,
            std::atomic_bool& abort, Loading::Reporter&, const Terrain::ArrivalView::Progress& nearReady) override
        {
            arrival->markActiveReady();
            nearReady(1, 1);
            mNear.set_value();
            mFarRelease.get_future().wait();
            arrival->finish(!abort);
        }
    };

    TEST(StagedArrival, RequiredMilestoneDoesNotWaitForDistantWorkerCompletion)
    {
        RecordProperty("staged_arrival_enabled", Misc::useStagedArrival() ? "true" : "false");
        osg::ref_ptr<osg::Group> parent = new osg::Group;
        StagedWorld world(parent);
        MWWorld::CellPreloader loader(nullptr, nullptr, &world, nullptr);
        loader.setWorkQueue(std::make_shared<SceneUtil::WorkQueue>(1));
        Loading::Listener listener;
        auto result = std::async(std::launch::async, [&] {
            return loader.ensureArrival(&world, { { 1024, 0, 0 }, {} }, listener, 0);
        });
        world.mNear.get_future().wait();
        const auto status = result.wait_for(std::chrono::milliseconds(100));
        world.mFarRelease.set_value(); // unblock even if an expectation fails
        EXPECT_EQ(status, Misc::useStagedArrival() ? std::future_status::ready : std::future_status::timeout);
        EXPECT_TRUE(result.get());
        loader.clearArrivals();
    }

    class TerrainOnlyWorld : public ArrivalWorld
    {
    public:
        using ArrivalWorld::ArrivalWorld;
        std::atomic_uint mPreloaded{ 0 };
        void preload(Terrain::View*, const osg::Vec3f&, const osg::Vec4i&, std::atomic_bool&,
            Loading::Reporter&) override { ++mPreloaded; }
    };
    class BlockWorker : public SceneUtil::WorkItem
    {
    public:
        std::promise<void> mStarted, mRelease;
        void doWork() override { mStarted.set_value(); mRelease.get_future().wait(); }
    };

    TEST(StagedArrival, TerrainHasCapacityWhenEverySpeculativeWorkerIsOccupied)
    {
        RecordProperty("staged_arrival_enabled", Misc::useStagedArrival() ? "true" : "false");
        osg::ref_ptr<osg::Group> parent = new osg::Group;
        TerrainOnlyWorld world(parent);
        auto queue = std::make_shared<SceneUtil::WorkQueue>(1);
        auto blocked = std::make_shared<BlockWorker>();
        queue->addWorkItem(blocked);
        blocked->mStarted.get_future().wait();
        MWWorld::CellPreloader loader(nullptr, nullptr, &world, nullptr);
        loader.setWorkQueue(queue);
        const MWWorld::PositionCellGrid position{ {}, {} };
        loader.setTerrainPreloadPositions(std::span(&position, 1));
        Loading::Listener listener;
        auto result = std::async(std::launch::async, [&] { loader.syncTerrainLoad(listener); });
        const auto status = result.wait_for(std::chrono::milliseconds(100));
        blocked->mRelease.set_value();
        EXPECT_EQ(status, Misc::useStagedArrival() ? std::future_status::ready : std::future_status::timeout);
        result.get();
        EXPECT_EQ(world.mPreloaded, 1u);
    }

    class BackpressuredWorld : public ArrivalWorld
    {
    public:
        using ArrivalWorld::ArrivalWorld;
        std::promise<void> mNear;
        void preloadArrival(Terrain::View*, const std::shared_ptr<Terrain::ArrivalView>& arrival,
            std::atomic_bool& abort, Loading::Reporter&, const Terrain::ArrivalView::Progress& nearReady) override
        {
            osg::ref_ptr<osg::Node> node = new osg::Group;
            arrival->publishGround({ { nullptr, 0 }, node, 1, {} });
            arrival->publish({ { { nullptr, 0 }, node, 256ull * 1024 * 1024,
                std::make_shared<std::atomic_bool>(false) } });
            arrival->markActiveReady();
            nearReady(1, 1);
            mNear.set_value();
            arrival->waitForDistantCapacity(abort); // No camera will admit this prediction.
            arrival->finish(false);
        }
    };
    class SignalWorker : public SceneUtil::WorkItem
    {
    public:
        std::promise<void> mRan;
        void doWork() override { mRan.set_value(); }
    };
    TEST(StagedArrival, UnpresentedPredictionDoesNotOccupyTheModelQueue)
    {
        RecordProperty("staged_arrival_enabled", Misc::useStagedArrival() ? "true" : "false");
        osg::ref_ptr<osg::Group> parent = new osg::Group;
        BackpressuredWorld world(parent);
        auto queue = std::make_shared<SceneUtil::WorkQueue>(1);
        MWWorld::CellPreloader loader(nullptr, nullptr, &world, nullptr);
        loader.setWorkQueue(queue);
        loader.predictArrival(&world, { {}, {} });
        world.mNear.get_future().wait();
        auto signal = std::make_shared<SignalWorker>();
        auto result = signal->mRan.get_future();
        queue->addWorkItem(signal);
        const auto status = result.wait_for(std::chrono::milliseconds(100));
        loader.clearArrivals(); // Cancels the parked producer before the test can fail.
        result.get();
        EXPECT_EQ(status, Misc::useStagedArrival() ? std::future_status::ready : std::future_status::timeout);
    }

}
