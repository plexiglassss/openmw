#include <components/diagnostics/postload.hpp>
#include <components/diagnostics/postloadmonitor.hpp>

#include <cmath>
#include <condition_variable>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace Diagnostics::PostLoad;
using namespace std::chrono_literals;

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    bool near(double lhs, double rhs) { return std::abs(lhs - rhs) < 0.0001; }
    constexpr auto index(Phase phase) { return static_cast<std::size_t>(phase); }

    void lifecycle()
    {
        Capture c;
        const auto t = Clock::now();
        require(!c.active() && c.begin().epoch == 0, "inactive capture");
        c.endPhase({}, Phase::Input, t);
        require(!c.finishFrame({}, true, t), "inactive frame must not emit");

        // Loading finished inside input: exclude the 50 ms before arming.
        const Stamp beforeLoad = c.begin();
        c.arm(t + 50ms);
        const auto epoch = c.begin().epoch;
        c.endPhase(beforeLoad, Phase::Input, t + 60ms);
        auto r = c.finishFrame(beforeLoad, true, t + 80ms);
        require(r && r->partial && r->firstCompleted && r->completedFrames == 1, "partial first frame");
        require(near(r->frameMs, 30) && near(r->elapsedMs, 30), "load time must be excluded");
        require(near(r->frame.phaseMs[index(Phase::Input)], 10), "mid-phase arming boundary");

        c.endPhase({ epoch, t + 80ms }, Phase::World, t + 85ms);
        r = c.finishFrame({ epoch, t + 80ms }, false, t + 90ms);
        require(r && !r->partial && !r->firstCompleted && r->completedFrames == 1, "skipped frame accounting");
        require(near(r->frame.phaseMs[index(Phase::Input)], 0), "per-frame metrics must reset");
        require(near(r->total.phaseMs[index(Phase::Input)], 10), "totals must persist");

        c.cancel();
        require(!c.finishFrame({}, true, t + 100ms), "cleanup cancels output");
        c.arm(t + 100ms);
        const auto nextEpoch = c.begin().epoch;
        require(nextEpoch != epoch, "reload must create a new sample");
        c.endPhase({ epoch, t }, Phase::World, t + 110ms);
        c.cellLoaded({ epoch, t }, Phase::World, true, t + 110ms);
        r = c.finishFrame({ epoch, t }, true, t + 120ms);
        require(r && r->epoch == nextEpoch && r->partial && r->frames == 1, "reload during open frame");
        require(near(r->frameMs, 20) && near(r->total.phaseMs[index(Phase::World)], 0)
                && r->total.cellLoads[index(Phase::World)] == 0, "stale scopes must not contaminate reload");
    }

    void bounds()
    {
        Capture c;
        const auto t = Clock::now();
        c.arm(t);
        const auto epoch = c.begin().epoch;
        c.endPhase({ epoch, t }, Phase::MWScript, t + 20s);
        auto r = c.finishFrame({ epoch, t }, true, t + 20s);
        require(r && r->details && !r->finished && near(r->frameMs, 20000), "long first stall lost");
        require(near(r->frame.phaseMs[index(Phase::MWScript)], 20000), "long phase lost");
        for (int i = 1; i < 5; ++i)
        {
            r = c.finishFrame({ epoch, t + 20s + (i - 1) * 1ms }, true, t + 20s + i * 1ms);
            require(r && !r->finished, "five observations must not end long-tail capture");
        }
        r = c.finishFrame({ epoch, t + 20s + 4ms }, true, t + 181s);
        require(r && r->finished && r->frameMs > 160000, "late blocking frame must be retained");
        require(!c.active() && !c.finishFrame({}, true), "capture must stop at boundary");

        c.arm(t);
        const auto fastEpoch = c.begin().epoch;
        unsigned details = 0;
        for (int i = 1; i <= 20000; ++i)
        {
            r = c.finishFrame({ fastEpoch, t + (i - 1) * 1us }, true, t + i * 1us);
            require(r.has_value(), "fast sample stopped early");
            details += r->details ? 1u : 0u;
        }
        require(!r->finished && c.active(), "fast frame count must not truncate wall-time coverage");
        require(details == 5, "fast frames must not flood log");
        r = c.finishFrame({ fastEpoch, t + 179s }, true, t + 180s);
        require(r && r->finished && near(r->elapsedMs, captureWindowMs), "wall-time bound");

        c.arm(t);
        const auto slowEpoch = c.begin().epoch;
        details = 0;
        for (int i = 1; i <= 100; ++i)
        {
            r = c.finishFrame({ slowEpoch, t + (i - 1) * 100ms }, true, t + i * 100ms);
            require(r.has_value(), "slow sample stopped early");
            details += r->details ? 1u : 0u;
        }
        require(!r->finished && details == 80, "slow frame detail bound without ending capture");
        r = c.finishFrame({ slowEpoch, t + 10s }, true, t + 181s);
        require(r && r->finished && !r->details, "summary survives exhausted detail budget");
    }

    void lateFreeze()
    {
        Capture c;
        const auto t = Clock::now();
        c.arm(t);
        const auto epoch = c.begin().epoch;
        for (int i = 0; i < 5; ++i)
            require(c.finishFrame({ epoch, t + i * 3600ms }, true, t + (i + 1) * 3600ms).has_value(),
                "initial observations missing");
        require(c.active(), "reported 18-second sequence prematurely ends recording");
        c.observeWindow({ true, true, true, false, false }, t + 18s);
        const auto blocked = c.beginPhase(Phase::Render, t + 18s);
        const auto heartbeat = c.heartbeat(t + 95s);
        require(heartbeat && heartbeat->frames == 5 && heartbeat->completedFrames == 5,
            "watchdog must run without another completed frame");
        require(heartbeat->main.index == index(Phase::Render) && near(heartbeat->mainMs, 77000)
                && near(heartbeat->lastFrameAgoMs, 77000), "outstanding phase duration lost");
        require(heartbeat->window.focused && near(heartbeat->windowStateAgeMs, 77000),
            "window flags must be labelled as an aged main-thread observation");
        c.finishPhase(blocked, Phase::Render, t + 109s);
        const auto r = c.finishFrame({ epoch, t + 18s }, true, t + 109s);
        require(r && r->details && near(r->frameMs, 91000) && !r->finished,
            "later 91-second frame must be recorded after it returns");
        require(near(r->frame.phaseMs[index(Phase::Render)], 91000), "late phase total missing");
    }

    void activityAndLoopAttribution()
    {
        Capture c;
        const auto t = Clock::now();
        c.arm(t);
        const auto main = c.beginPhase(Phase::MWScript, t);
        const auto lua = c.beginPhase(Phase::LuaExecution, t + 1ms);
        const auto nested = c.beginPhase(Phase::Input, t + 2ms);
        c.finishPhase(nested, Phase::Input, t + 3ms);
        auto h = c.heartbeat(t + 1s);
        require(h && h->main.index == index(Phase::MWScript) && near(h->mainMs, 1000)
                && h->lua.index == index(Phase::LuaExecution) && near(h->luaMs, 999),
            "nested main scope restoration and independent Lua lane");
        c.finishPhase(lua, Phase::LuaExecution, t + 2s);
        c.finishPhase(main, Phase::MWScript, t + 2s);
        c.finishFrame(main.stamp, true, t + 2s);
        const auto limiter = c.beginLoop(LoopPhase::Limiter, t + 2s);
        h = c.heartbeat(t + 6s);
        require(h && h->main.loop && h->main.index == static_cast<std::size_t>(LoopPhase::Limiter)
                && near(h->mainMs, 4000), "wait after frame return must be visible while blocked");
        c.finishLoop(limiter, LoopPhase::Limiter, t + 7s);
        const auto hidden = c.beginLoop(LoopPhase::HiddenSleep, t + 7s);
        c.finishLoop(hidden, LoopPhase::HiddenSleep, t + 7s + 5ms);
        c.observeWindow({ true, false, false, true, true }, t + 7s);
        const auto skipped = c.finishFrame({ main.stamp.epoch, t + 7s + 5ms }, false, t + 7s + 6ms);
        require(skipped && near(skipped->frameMs, 1) && skipped->completedFrames == 1,
            "outside-frame waits must not inflate engine-frame duration");
        require(near(skipped->loopTotalMs[static_cast<std::size_t>(LoopPhase::Limiter)], 5000)
                && near(skipped->loopTotalMs[static_cast<std::size_t>(LoopPhase::HiddenSleep)], 5),
            "separate completed main-loop wait totals");
        h = c.heartbeat(t + 11s);
        require(h && h->window.known && !h->window.visible && h->window.minimized && h->window.paused,
            "hidden-window observations are retained");

        const auto staleMain = c.beginPhase(Phase::Input, t + 12s);
        const auto staleLua = c.beginPhase(Phase::LuaExecution, t + 12s);
        c.cancel();
        require(!c.heartbeat(t + 20s), "cancelled capture must not produce new snapshots");
        c.arm(t + 20s);
        const auto freshMain = c.beginPhase(Phase::World, t + 20s);
        c.finishPhase(staleMain, Phase::Input, t + 21s);
        c.finishPhase(staleLua, Phase::LuaExecution, t + 21s);
        h = c.heartbeat(t + 21s);
        require(h && h->epoch == freshMain.stamp.epoch && h->main.id == freshMain.id
                && !h->lua.id && !h->window.known, "stale scope ends must not overwrite rearmed state");
        require(!c.heartbeat(t + 22s), "heartbeat rate limit");
        unsigned reports = 1;
        for (int i = 26; i <= 201; i += 5)
            if (auto next = c.heartbeat(t + std::chrono::seconds(i)))
                ++reports;
        require(reports <= 37 && !c.heartbeat(t + 300s), "heartbeat has an independent wall-time bound");
        require(c.active(), "an outstanding frame may still complete after the heartbeat bound");
    }

    std::mutex monitorMutex;
    std::condition_variable monitorWake;
    unsigned monitorCalls = 0;
    Snapshot monitorSnapshot;
    void observeSnapshot(const Snapshot& snapshot)
    {
        const std::lock_guard<std::mutex> lock(monitorMutex);
        monitorSnapshot = snapshot;
        ++monitorCalls;
        monitorWake.notify_all();
    }
    void throwFromSnapshot(const Snapshot&) { throw std::runtime_error("injected diagnostic callback error"); }
    void monitorLifetime()
    {
        Capture c;
        c.arm(Clock::now() - 2s);
        const auto blocked = c.beginPhase(Phase::Physics);
        {
            Monitor monitor(c, observeSnapshot, 1ms);
            std::unique_lock<std::mutex> lock(monitorMutex);
            require(monitorWake.wait_for(lock, 2s, [] { return monitorCalls != 0; }),
                "monitor must observe a main-thread scope without frame completion");
            require(monitorSnapshot.frames == 0 && monitorSnapshot.main.id == blocked.id,
                "monitor observed wrong scope");
            lock.unlock();
            monitor.stop();
            require(!monitor.failed(), "healthy monitor failed");
            monitor.stop(); // teardown is idempotent and has no join under Capture's lock
        }
        c.cancel();
        c.arm(Clock::now() - 2s);
        {
            Monitor monitor(c, throwFromSnapshot, 1ms);
            const auto deadline = Clock::now() + 2s;
            while (!monitor.failed() && Clock::now() < deadline)
                std::this_thread::yield();
            require(monitor.failed(), "callback exception must stop observer without terminating engine");
            monitor.stop();
        }
        c.cancel();
        {
            Monitor inactive(c, observeSnapshot, 1h);
            // stop must wake a long timed wait immediately, not wait one hour.
            inactive.stop();
        }
    }

    void cellAttribution()
    {
        Capture c;
        const auto t = Clock::now();
        c.arm(t);
        const auto epoch = c.begin().epoch;
        c.cellLoaded({ epoch, t + 2ms }, Phase::MWScript, false, t + 5ms);
        c.cellLoaded({ epoch, t }, Phase::MWScript, true, t + 10ms);
        c.cellLoaded({ epoch, t + 10ms }, Phase::World, true, t + 14ms);
        const auto r = c.finishFrame({ epoch, t }, true, t + 20ms);
        require(r && r->total.cellLoads[index(Phase::MWScript)] == 2, "nested load call count");
        require(near(r->total.outerCellMs[index(Phase::MWScript)], 10), "nested elapsed must not double count");
        require(r->total.cellLoads[index(Phase::World)] == 1
                && near(r->total.outerCellMs[index(Phase::World)], 4), "phase attribution");

        // Exercise the production RAII nesting, including reload while a scope is open.
        capture().cancel();
        currentPhase = Phase::Other;
        {
            Scope input(Phase::Input);
            capture().arm();
            {
                CellLoadScope outer;
                {
                    Scope world(Phase::World);
                    CellLoadScope inner;
                    require(cellLoadDepth == 2, "RAII nested depth");
                }
                require(currentPhase == Phase::Input && cellLoadDepth == 1, "RAII restoration");
            }
            const auto nested = capture().finishFrame({}, true);
            require(nested && nested->total.cellLoads[index(Phase::Input)] == 1
                    && nested->total.cellLoads[index(Phase::World)] == 1, "RAII nested calls");
            require(near(nested->total.outerCellMs[index(Phase::World)], 0), "only outer scope duration");
            {
                CellLoadScope stale;
                capture().cancel();
                capture().arm();
                { CellLoadScope fresh; }
            }
            require(cellLoadDepth == 0, "stale destructor must not underflow depth");
        }
        require(currentPhase == Phase::Other, "phase restored after arming");
        const auto reloaded = capture().finishFrame({}, true);
        require(reloaded && reloaded->total.cellLoads[index(Phase::Input)] == 1, "stale cell excluded");
        capture().cancel();
    }

    void workerOverlap()
    {
        Capture c;
        const auto t = Clock::now();
        c.arm(t);
        const auto epoch = c.begin().epoch;
        // Independent producers, with deliberately overlapping time intervals.
        // No sleep, GPU or engine dependency: this tests the actual recorder.
        std::vector<std::thread> workers;
        for (int n = 0; n < 4; ++n)
            workers.emplace_back([&] {
                for (int i = 0; i < 250; ++i)
                {
                    c.endPhase({ epoch, t }, Phase::LuaExecution, t + 1ms);
                    c.cellLoaded({ epoch, t }, Phase::LuaExecution, true, t + 1ms);
                }
            });
        c.endPhase({ epoch, t }, Phase::Render, t + 8ms);
        c.endPhase({ epoch, t + 8ms }, Phase::LuaFinish, t + 10ms);
        for (auto& worker : workers)
            worker.join();
        const auto r = c.finishFrame({ epoch, t }, true, t + 10ms);
        require(r && near(r->frameMs, 10) && near(r->frame.phaseMs[index(Phase::Render)], 8)
                && near(r->frame.phaseMs[index(Phase::LuaFinish)], 2), "main wait/render attribution");
        require(near(r->frame.phaseMs[index(Phase::LuaExecution)], 1000)
                && r->frame.cellLoads[index(Phase::LuaExecution)] == 1000, "worker updates lost");
        require(near(r->frame.outerCellMs[index(Phase::LuaExecution)], 1000), "worker cell elapsed lost");
    }
}

int main()
{
    lifecycle();
    bounds();
    lateFreeze();
    activityAndLoopAttribution();
    monitorLifetime();
    cellAttribution();
    workerOverlap();
    std::cout << "Post-load tests passed: lifecycle, long-tail bounds, late freeze, activity/loop attribution, monitor lifetime, nested cells, worker overlap\n";
}
