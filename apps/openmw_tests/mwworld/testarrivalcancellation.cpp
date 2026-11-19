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
        Terrain::View* createView() override { return new View; }
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
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(2);
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
}
