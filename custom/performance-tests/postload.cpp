#include <components/diagnostics/postload.hpp>

#include <cmath>
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
            require(r && r->finished == (i == 4), "minimum five observations");
        }
        require(!c.active() && !c.finishFrame({}, true), "capture must stop at boundary");

        c.arm(t);
        const auto fastEpoch = c.begin().epoch;
        unsigned details = 0;
        for (int i = 1; i <= 10000; ++i)
        {
            r = c.finishFrame({ fastEpoch, t + (i - 1) * 1us }, true, t + i * 1us);
            require(r.has_value(), "fast sample stopped early");
            details += r->details ? 1u : 0u;
        }
        require(r->finished && r->frameLimit && near(r->elapsedMs, 10), "hard frame cap");
        require(details == 5, "fast frames must not flood log");

        c.arm(t);
        const auto slowEpoch = c.begin().epoch;
        details = 0;
        for (int i = 1; i <= 100; ++i)
        {
            r = c.finishFrame({ slowEpoch, t + (i - 1) * 100ms }, true, t + i * 100ms);
            require(r.has_value(), "slow sample stopped early");
            details += r->details ? 1u : 0u;
        }
        require(r->finished && !r->frameLimit && details == 40, "slow frame detail bound");
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
    cellAttribution();
    workerOverlap();
    std::cout << "Post-load tests passed: lifecycle, bounds, nested cells, worker overlap\n";
}
