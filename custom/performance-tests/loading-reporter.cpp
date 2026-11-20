#include <components/loadinglistener/reporter.hpp>
#include <components/loadinglistener/loadinglistener.hpp>

#include <chrono>
#include <future>
#include <functional>
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
            // A notification may arrive before the listener returns. The revision
            // predicate must retain it without treating progress as completion.
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

    struct SnapshotListener : Loading::Listener
    {
        std::vector<std::size_t> totals;
        std::vector<std::size_t> progress;
        std::function<void()> duringRange;
        std::function<void()> duringProgress;

        void setProgressRange(std::size_t value) override
        {
            totals.push_back(value);
            if (duringRange)
                duringRange();
        }
        void setProgress(std::size_t value) override
        {
            progress.push_back(value);
            if (duringProgress)
                duringProgress();
        }
    };

    void wait(Loading::Reporter& reporter, Loading::Listener& listener, bool timed)
    {
        Loading::Reporter::WaitStats stats;
        if (timed)
            reporter.waitWithStats(listener, stats);
        else
            reporter.wait(listener);
    }

    void workerCompletesDuringCallback(bool timed, bool rangeCallback)
    {
        Loading::Reporter reporter;
        reporter.addTotal(2);
        SnapshotListener listener;
        std::promise<void> entered;
        auto enter = entered.get_future();
        std::promise<void> finished;
        auto finish = finished.get_future();
        std::jthread worker([&] {
            enter.wait();
            reporter.addTotal(3);
            reporter.addProgress(1);
            reporter.complete();
            finished.set_value();
        });
        const auto callback = [&] {
            entered.set_value();
            require(finish.wait_for(std::chrono::seconds(3)) == std::future_status::ready,
                "progress callback blocked resource worker progress or completion");
        };
        if (rangeCallback)
            listener.duringRange = callback;
        else
            listener.duringProgress = callback;
        wait(reporter, listener, timed);
        // The range and progress passed to one callback pair belong to the same
        // snapshot even if the worker changes both between those callbacks.
        require(listener.totals == std::vector<std::size_t>{ 2 }
                && listener.progress == std::vector<std::size_t>{ 0 },
            "incoherent progress snapshot or completion callback repeated");
    }

    void notificationDuringCallback(bool timed, bool totalOnly)
    {
        Loading::Reporter reporter;
        reporter.addTotal(2);
        SnapshotListener listener;
        std::promise<void> entered;
        auto enter = entered.get_future();
        std::promise<void> updated;
        auto update = updated.get_future();
        std::jthread worker([&] {
            enter.wait();
            if (totalOnly)
                reporter.addTotal(3);
            else
                reporter.addProgress(0);
            updated.set_value();
        });
        listener.duringProgress = [&] {
            if (listener.progress.size() == 1)
            {
                entered.set_value();
                require(update.wait_for(std::chrono::seconds(3)) == std::future_status::ready,
                    "worker update blocked by listener");
            }
            else
                reporter.complete();
        };
        wait(reporter, listener, timed);
        require(listener.totals == std::vector<std::size_t>{ 2, totalOnly ? 5u : 2u }
                && listener.progress == std::vector<std::size_t>{ 0, 0 },
            "lost total-only or zero-progress notification during listener callback");
    }

    void rangeCallbackFailure(bool timed)
    {
        Loading::Reporter reporter;
        SnapshotListener listener;
        listener.duringRange = [] { throw std::runtime_error("range callback fixture"); };
        bool threw = false;
        try { wait(reporter, listener, timed); }
        catch (const std::runtime_error&) { threw = true; }
        require(threw && listener.progress.empty(), "range exception swallowed or progress callback continued");
        reporter.addProgress(1);
        reporter.complete();
        wait(reporter, listener, timed);
    }
}

int main()
{
    completedBeforeWait();
    callbackFailure();
    concurrentWait(false);
    concurrentWait(true);
    for (const bool timed : { false, true })
    {
        for (const bool rangeCallback : { false, true })
            workerCompletesDuringCallback(timed, rangeCallback);
        for (const bool totalOnly : { false, true })
            notificationDuringCallback(timed, totalOnly);
        rangeCallbackFailure(timed);
    }
    std::cout << "Loading Reporter passed: completed fast path, callback order, worker progress/completion during both callbacks, coherent snapshots, total-only/zero-progress notifications, completion and exceptions\n";
}
