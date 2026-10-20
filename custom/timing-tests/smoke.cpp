#include <components/diagnostics/timing.hpp>
#include <cassert>
#include <stdexcept>

int main()
{
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
    // Main thread exit must flush the last partial frame.
}
