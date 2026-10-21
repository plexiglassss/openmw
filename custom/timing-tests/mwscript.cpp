#include <components/diagnostics/mwscript.hpp>

#include <cassert>
#include <stdexcept>
#include <thread>

int main()
{
    if (!Diagnostics::enabled())
        return 2;
    Diagnostics::setFrame(42);
    {
        Diagnostics::MWScriptNativeScope ignored("outside.script");
    }
    {
        Diagnostics::MWScriptContext context("outer");
        Diagnostics::Scope run("mwscript", "outer", "run");
        {
            Diagnostics::Scope compile("mwscript_phase", "outer", "compile");
        }
        {
            Diagnostics::Scope execute("mwscript_phase", "outer", "execute");
            {
                Diagnostics::MWScriptNativeScope search("world.searchPtr");
                Diagnostics::MWScriptNativeScope inactive("world.findInactive");
                Diagnostics::MWScriptNativeScope cell("cell.load");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            try
            {
                Diagnostics::MWScriptContext nested("inner");
                Diagnostics::MWScriptNativeScope operation("world.disable");
                throw std::runtime_error("expected");
            }
            catch (const std::runtime_error&)
            {
            }
            assert(Diagnostics::activeMWScript == "outer");
            {
                Diagnostics::MWScriptNativeScope operation("world.disable");
            }
            std::thread worker([] {
                assert(Diagnostics::activeMWScript.empty());
                Diagnostics::MWScriptNativeScope ignored("other.thread");
            });
            worker.join();
        }
    }
    assert(Diagnostics::activeMWScript.empty());
    {
        Diagnostics::MWScriptNativeScope ignored("after.script");
    }
    Diagnostics::setFrame(43);
}
