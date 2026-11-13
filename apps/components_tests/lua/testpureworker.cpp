#include <components/lua/pureworker.hpp>
#include <components/misc/ownedjobs.hpp>
#include <gtest/gtest.h>
#include <chrono>
#include <future>

namespace
{
    using namespace std::chrono_literals;
    TEST(ParallelWorker, LimitFailureDoesNotContaminateTheNextFreshLuaState)
    {
        std::atomic_bool cancel{ false };
        for (const char* code : {"return function(t) while true do end end", "return function(t) return {named=3} end",
                 "return function(t) return {io.open('input')} end", "return function(t) local a={} for i=1,1000000 do a[i]=i end return a end"})
            EXPECT_THROW(LuaUtil::runPureWorker(code, {}, cancel), std::runtime_error);
        EXPECT_THROW(LuaUtil::runPureWorker("\x1b", {}, cancel), std::invalid_argument);
        const char* counter = "return function(t) counter=(counter or 0)+1; return {counter,math.sqrt(t[1])} end";
        for (int i = 0; i < 2; ++i)
            EXPECT_EQ(LuaUtil::runPureWorker(counter, {9}, cancel), (std::vector<double>{1,3}));
    }

    TEST(ParallelWorker, IndependentThreadsPublishInOrderAndBoundOutstandingResults)
    {
        Misc::NativeJobPool pool(2, 1);
        Misc::OwnedJobs jobs(pool);
        std::promise<void> release;
        auto gate = release.get_future().share();
        auto first = jobs.submit<std::thread::id>([gate](const auto& cancelled) {
            gate.wait_for(5s);
            LuaUtil::runPureWorker("return function(t) return t end", {1}, cancelled);
            return std::this_thread::get_id();
        });
        auto second = jobs.submit<std::thread::id>([](const auto& cancelled) {
            LuaUtil::runPureWorker("return function(t) return t end", {2}, cancelled);
            return std::this_thread::get_id();
        });
        const auto limit = std::chrono::steady_clock::now() + 4s;
        while (!second->mDone.load() && std::chrono::steady_clock::now() < limit)
            std::this_thread::yield();
        jobs.publish();
        EXPECT_TRUE(second->mDone.load());
        EXPECT_FALSE(second->mPublished);
        release.set_value();
        while (!first->mDone.load() && std::chrono::steady_clock::now() < limit + 2s)
            std::this_thread::yield();
        jobs.publish();
        ASSERT_TRUE(first->mPublished && second->mPublished);
        EXPECT_NE(first->mResult.value(), second->mResult.value());
        EXPECT_NE(first->mResult.value(), std::this_thread::get_id());
        std::vector<std::shared_ptr<Misc::OwnedJob<int>>> held;
        for (int i = 0; i < 14; ++i)
            held.push_back(jobs.submit<int>([](const auto&) { return 42; }));
        EXPECT_FALSE(jobs.hasRoom());
        EXPECT_FALSE(jobs.submit<int>([](const auto&) { return 0; }));
        jobs.clear();
        EXPECT_TRUE(first->mCancelled.load());
        EXPECT_TRUE(jobs.hasRoom());
        Misc::NativeJobPool disabled(0,1);
        Misc::OwnedJobs disabledJobs(disabled);
        EXPECT_FALSE(disabledJobs.submit<int>([](const auto&) { return 0; }));
    }
}
