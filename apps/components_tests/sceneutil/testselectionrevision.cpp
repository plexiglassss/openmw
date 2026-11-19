#include <components/sceneutil/selectionrevision.hpp>
#include <components/sceneutil/asyncpreparation.hpp>
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

namespace
{
    TEST(SelectionRevision, TracksSourceCellsForSubcellsNegativeCoordinatesAndLargeChunks)
    {
        SceneUtil::SelectionRevision revisions;
        revisions.invalidate(-2, -3, false);
        EXPECT_NE(revisions.get(-1.75f, -2.75f, .5f, false), 0u);
        EXPECT_NE(revisions.get(-1.25f, -2.25f, .5f, false), 0u);
        EXPECT_NE(revisions.get(-1, -2, 2, false), 0u);
        EXPECT_EQ(revisions.get(-.5f, -2.5f, 1, false), 0u);
        EXPECT_EQ(revisions.get(-1.5f, -1.5f, 1, false), 0u);
        const auto far = revisions.get(-1.5f, -2.5f, 1, false);
        revisions.invalidate(-2, -3, true);
        EXPECT_EQ(revisions.get(-1.5f, -2.5f, 1, false), far);
        EXPECT_GT(revisions.get(-1.5f, -2.5f, 1, true), far);
        revisions.clear();
        EXPECT_GT(revisions.get(100, 100, 1, false), far);
    }

    TEST(SelectionRevision, UnrelatedChangesPreserveGraphicsWaitButOwnedChangesRejectIt)
    {
        using Key = std::pair<int, std::uint64_t>;
        using Jobs = SceneUtil::AsyncPreparation<Key, std::shared_ptr<int>>;
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Jobs jobs(queue);
        SceneUtil::SelectionRevision revisions;
        const auto revision = [&](int x) { return revisions.get(x + .5f, .5f, 1, false); };
        const auto cancelStale = [&] { jobs.cancelIf([&](const Key& key) { return key.second != revision(key.first); }); };
        const Key key{ 0, revision(0) };
        auto gate = std::make_shared<std::atomic_bool>(false);
        std::atomic_int cancelled{ 0 };
        jobs.request(key, 0, [&](const std::atomic_bool&) {
            return Jobs::Prepared{ std::make_shared<int>(42), gate, [&] { ++cancelled; } };
        });
        for (int frame = 1; frame <= 20; ++frame)
        {
            revisions.invalidate(frame, 0, false);
            revisions.invalidate(0, 0, true); // active-only ownership changes leave far scenery valid
            cancelStale();
            EXPECT_TRUE(jobs.contains(key));
            EXPECT_FALSE(jobs.poll(key, frame * .01).mReady);
        }
        *gate = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!jobs.poll(key, .21).mReady && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        ASSERT_TRUE(jobs.poll(key, .21).mReady);
        EXPECT_EQ(jobs.submitted(), 1u);
        EXPECT_EQ(cancelled, 0);
        revisions.invalidate(0, 0, false);
        cancelStale();
        EXPECT_FALSE(jobs.poll(key, .22).mReady);
        EXPECT_FALSE(jobs.contains(key));
        EXPECT_EQ(cancelled, 1);
    }
}
