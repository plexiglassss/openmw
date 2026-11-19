#include <components/diagnostics/loading.hpp>

#include <cmath>
#include <iostream>
#include <sstream>
#include <set>
#include <stdexcept>
#include <thread>

using namespace Diagnostics::Loading;
using namespace std::chrono_literals;

namespace
{
    constexpr auto index(Stage stage) { return static_cast<std::size_t>(stage); }
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    bool near(double a, double b) { return std::abs(a - b) < 0.0001; }

    void scopes()
    {
        // Exercise every exported stage, including the expanded cold-object
        // breakdown. Names and counters must remain paired and distinct.
        std::set<std::string_view> names;
        Metrics stages;
        {
            Session session(stages);
            for (std::size_t i = 0; i < stageCount; ++i)
            {
                require(!stageNames[i].empty() && names.insert(stageNames[i]).second, "stage name mapping");
                Timer timer(static_cast<Stage>(i));
            }
        }
        for (std::size_t i = 0; i < stageCount; ++i)
            require(stages.calls[i] == 1 && stages.depth[i] == 0 && stages.elapsedMs[i] >= 0, "stage accounting");
        require(currentMetrics == nullptr, "initial TLS state");
        { Timer inactive(Stage::ObjectChunks); }
        require(currentMetrics == nullptr, "inactive timer altered session");
        Metrics outer;
        Metrics inner;
        {
            Session session(outer);
            Timer top(Stage::RenderNodes);
            {
                Timer recursive(Stage::RenderNodes);
                require(outer.depth[index(Stage::RenderNodes)] == 2, "same-stage depth");
                {
                    Session nested(inner);
                    Timer other(Stage::ObjectChunks);
                    require(currentMetrics == &inner, "nested session activation");
                }
                require(currentMetrics == &outer, "nested session restoration");
            }
            require(outer.depth[index(Stage::RenderNodes)] == 1, "recursive depth unwind");
            require(outer.elapsedMs[index(Stage::RenderNodes)] == 0, "inner same-stage timer double counted");
            try
            {
                Session nested(inner);
                Timer failing(Stage::TerrainChunks);
                throw std::runtime_error("fixture");
            }
            catch (const std::runtime_error&) {}
            require(currentMetrics == &outer, "exception failed to restore session");
        }
        require(currentMetrics == nullptr, "outer session restoration");
        require(outer.calls[index(Stage::RenderNodes)] == 2, "nested calls count");
        require(outer.depth[index(Stage::RenderNodes)] == 0, "outer depth leaked");
        require(inner.calls[index(Stage::ObjectChunks)] == 1 && inner.calls[index(Stage::TerrainChunks)] == 1,
            "scopes wrote to wrong session");
        require(outer.calls[index(Stage::ObjectChunks)] == 0, "nested session contaminated parent");
        std::ostringstream text;
        text << outer;
        require(text.str().find("renderNodesMs=") != std::string::npos
                && text.str().find("/calls=2") != std::string::npos, "metric formatting");
    }

    void jobTimes()
    {
        Job job;
        Job other;
        require(job.snapshot().id != other.snapshot().id, "job IDs must be unique");
        require(!job.snapshot().enqueueRecorded && !job.snapshot().started, "fresh job state");
        const auto t = Clock::now();
        job.enqueued(t);
        job.started(t + 40ms);
        const auto running = job.snapshot();
        require(running.started && !running.finished && near(running.enqueueToStartMs, 40), "queue interval");
        Metrics metrics;
        metrics.elapsedMs[index(Stage::RenderNodes)] = 60;
        metrics.elapsedMs[index(Stage::ObjectChunks)] = 50;
        metrics.calls[index(Stage::ObjectChunks)] = 10;
        job.finished(metrics, true, false, t + 140ms);
        const auto done = job.snapshot();
        require(done.finished && done.abortRequested && !done.exception && near(done.workMs, 100), "work interval");
        require(done.metrics.calls[index(Stage::ObjectChunks)] == 10
                && near(done.metrics.elapsedMs[index(Stage::ObjectChunks)], 50), "published stage snapshot");
        require(near(done.enqueueToStartMs, 40), "completion changed queue interval");
    }

    void workerAndFailure()
    {
        Job job;
        std::atomic<bool> abort{ false };
        std::atomic<bool> finished{ false };
        job.enqueued();
        Metrics mainMetrics;
        {
            Session mainSession(mainMetrics);
            std::thread worker([&] {
                require(currentMetrics == nullptr, "session leaked across threads");
                {
                    JobRun run(job, abort);
                    for (int i = 0; i < 1000; ++i)
                    {
                        Timer timer(Stage::GroundcoverChunks);
                    }
                }
                require(currentMetrics == nullptr, "worker session leaked");
                finished.store(true);
            });
            while (!finished.load())
            {
                const auto snapshot = job.snapshot();
                if (snapshot.finished)
                    require(snapshot.metrics.calls[index(Stage::GroundcoverChunks)] == 1000,
                        "partially published final snapshot");
                std::this_thread::yield();
            }
            worker.join();
            require(currentMetrics == &mainMetrics, "worker altered main TLS");
        }
        require(job.snapshot().finished && !job.snapshot().exception, "normal run not published");
        require(mainMetrics.calls[index(Stage::GroundcoverChunks)] == 0, "worker charged to main thread");

        Job failed;
        failed.enqueued();
        try
        {
            JobRun run(failed, abort);
            Timer timer(Stage::ObjectChunks);
            throw std::runtime_error("fixture");
        }
        catch (const std::runtime_error&) {}
        const auto snapshot = failed.snapshot();
        require(snapshot.finished && snapshot.exception && !snapshot.abortRequested, "exception status lost");
        require(snapshot.metrics.calls[index(Stage::ObjectChunks)] == 1, "unwound phase lost");
        require(currentMetrics == nullptr, "failed run leaked TLS");
    }
}

int main()
{
    scopes();
    jobTimes();
    workerAndFailure();
    std::cout << "Loading diagnostics passed: inactive/nested scopes, clocks, snapshots, threads and exceptions\n";
}
