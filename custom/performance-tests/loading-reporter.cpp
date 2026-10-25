#include <components/loadinglistener/reporter.hpp>
#include <components/loadinglistener/loadinglistener.hpp>

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    struct Listener : Loading::Listener
    {
        std::promise<void> firstCallback;
        std::promise<void> secondCallback;
        std::shared_future<void> workerStarted;
        std::vector<char> calls;
        bool first = true;
        bool second = true;
        void setProgressRange(std::size_t) override { calls.push_back('r'); }
        void setProgress(std::size_t) override
        {
            calls.push_back('p');
            if (first)
            {
                first = false;
                firstCallback.set_value();
                require(workerStarted.wait_for(std::chrono::seconds(3)) == std::future_status::ready,
                    "worker did not start");
            }
            else if (second)
            {
                second = false;
                secondCallback.set_value();
            }
        }
    };
    void completedBeforeWait()
    {
        Loading::Reporter reporter;
        reporter.complete();
        Listener listener;
        Loading::Reporter::WaitStats stats;
        stats.listenerMs = 123;
        reporter.waitWithStats(listener, stats);
        require(listener.calls.empty(), "completed reporter called listener");
        require(stats.conditionWaits == 0 && stats.progressUpdates == 0 && stats.listenerMs == 0,
            "completed reporter retained stale statistics");
        reporter.wait(listener);
    }
    void concurrentWait(bool timed)
    {
        Loading::Reporter reporter;
        reporter.addTotal(3);
        Listener listener;
        auto callback = listener.firstCallback.get_future();
        auto secondCallback = listener.secondCallback.get_future();
        std::promise<void> workerStarted;
        listener.workerStarted = workerStarted.get_future().share();
        std::thread worker([&] {
            require(callback.wait_for(std::chrono::seconds(3)) == std::future_status::ready, "listener did not start");
            workerStarted.set_value();
            // The listener still owns the Reporter lock until its callback returns.
            // These progress-only notifications must not finish the wait.
            reporter.addProgress(0);
            require(secondCallback.wait_for(std::chrono::seconds(3)) == std::future_status::ready,
                "wait ended on progress-only notification");
            reporter.addProgress(3);
            reporter.complete();
        });
        Loading::Reporter::WaitStats stats;
        if (timed)
            reporter.waitWithStats(listener, stats);
        else
            reporter.wait(listener);
        worker.join();
        require(listener.calls.size() >= 2 && listener.calls.size() % 2 == 0, "missing callback pairs");
        for (std::size_t i = 0; i < listener.calls.size(); i += 2)
            require(listener.calls[i] == 'r' && listener.calls[i + 1] == 'p', "callback order changed");
        if (timed)
        {
            require(stats.progressUpdates == listener.calls.size() / 2, "callback counter mismatch");
            require(stats.conditionWaits == stats.progressUpdates && stats.conditionWaits > 0, "wait count mismatch");
            require(stats.initialLockMs >= 0 && stats.listenerMs >= 0 && stats.conditionMs >= 0,
                "negative elapsed interval");
            require(stats.elapsedMs + 0.001 >= stats.initialLockMs + stats.listenerMs + stats.conditionMs,
                "subintervals exceed measured wait");
        }
    }
    void callbackFailure()
    {
        struct FailingListener : Loading::Listener
        {
            void setProgress(std::size_t) override { throw std::runtime_error("fixture callback"); }
        } listener;
        Loading::Reporter reporter;
        Loading::Reporter::WaitStats stats;
        bool threw = false;
        try { reporter.waitWithStats(listener, stats); }
        catch (const std::runtime_error&) { threw = true; }
        require(threw, "listener exception swallowed");
        // Must be able to acquire the reporter mutex after unwinding.
        reporter.complete();
        reporter.waitWithStats(listener, stats);
        require(stats.progressUpdates == 0, "completed reporter invoked failing listener");
    }
}

int main()
{
    completedBeforeWait();
    callbackFailure();
    concurrentWait(false);
    concurrentWait(true);
    std::cout << "Loading Reporter passed: completed fast path, callback order, progress-only wakeups, completion and exceptions\n";
}
