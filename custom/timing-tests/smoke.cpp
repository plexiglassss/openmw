#include <components/diagnostics/timing.hpp>
#include <cassert>
#include <components/crashcatcher/freezewarningtimer.hpp>
#include <stdexcept>

int main()
{
    Crash::FreezeWarningTimer freeze;
    const auto origin = std::chrono::steady_clock::time_point{};
    assert(!freeze.update(true, origin));
    assert(!freeze.update(true, origin + std::chrono::seconds(59)));
    assert(freeze.update(true, origin + std::chrono::seconds(60)));
    assert(!freeze.update(false, origin + std::chrono::seconds(61)));
    assert(!freeze.update(true, origin + std::chrono::seconds(62)));
    assert(!freeze.update(true, origin + std::chrono::seconds(121)));
    assert(freeze.update(true, origin + std::chrono::seconds(122)));
    if (!Diagnostics::enabled()) return 2;
    Diagnostics::setFrame(42);
    {
        Diagnostics::Scope parent("lua", "parent.lua", "onUpdate");
        {
            Diagnostics::Scope child("lua", "child,\"quoted\".lua", "event");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        for (int i = 0; i < 3; ++i)
        {
            Diagnostics::Scope repeated("lua", "repeated.lua", "timer");
        }
        try
        {
            Diagnostics::Scope exception("lua", "exception.lua", "onUpdate");
            throw std::runtime_error("expected");
        }
        catch (const std::exception&) {}
        assert(Diagnostics::Scope::currentSource() == "parent.lua");
    }
    assert(Diagnostics::Scope::currentSource().empty());
    std::thread worker([] {
        Diagnostics::setFrame(42);
        Diagnostics::Scope scope("gc", "Lua", "step");
    });
    worker.join(); // Also verifies thread-exit output.
    Diagnostics::setFrame(43);
    { Diagnostics::Scope last("phase", "last", "shutdown"); }
    for (unsigned i = 44; i < 172; ++i)
    {
        Diagnostics::setFrame(i);
        Diagnostics::Scope row("test", "batched", "frame");
    }
    // Main thread exit must flush all buffered frames and the last partial frame.
}
