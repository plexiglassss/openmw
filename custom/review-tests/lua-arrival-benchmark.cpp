// Synthetic sandbox-construction benchmark. This is not a gameplay FPS test.
#include <components/lua/luastate.hpp>
#include <components/testing/util.hpp>

#include <ctime>
#include <iostream>

int main()
{
    // Process CPU time excludes unrelated build jobs descheduling this probe.
    const auto now = [] { return std::clock(); };
    const auto ms = [](std::clock_t begin, std::clock_t end) {
        return 1000.0 * static_cast<double>(end - begin) / CLOCKS_PER_SEC;
    };
    for (int functions : { 8, 64, 256 })
    {
        std::string source = "local functions = {}\n";
        for (int i = 1; i <= functions; ++i)
            source += "functions[" + std::to_string(i)
                + "] = function(v) local value = v + " + std::to_string(i) + "; return value * value end\n";
        source += "return functions\n";
        TestingOpenMW::VFSTestFile script(source);
        constexpr VFS::Path::NormalizedView path("benchmark.lua");
        auto vfs = TestingOpenMW::createTestVFS({ { path, &script } });
        LuaUtil::ScriptsConfiguration configuration;
        LuaUtil::LuaState lua(vfs.get(), &configuration);
        auto started = now();
        { auto result = lua.runInNewSandbox(VFS::Path::Normalized(path)); }
        const double cold = ms(started, now());
        started = now();
        { auto result = lua.runInNewSandbox(VFS::Path::Normalized(path)); }
        const double second = ms(started, now());
        sol::state_view state(lua.unsafeState());
        state.collect_garbage();
        const auto baseline = lua.getTotalMemoryUsage();
        double construction = 0, collection = 0;
        std::uint64_t peakLiveBytes = 0;
        for (int batch = 0; batch < 20; ++batch)
        {
            std::vector<sol::table> instances;
            instances.reserve(100);
            started = now();
            for (int item = 0; item < 100; ++item)
                instances.push_back(lua.runInNewSandbox(VFS::Path::Normalized(path)));
            construction += ms(started, now());
            peakLiveBytes = std::max(peakLiveBytes, lua.getTotalMemoryUsage() - baseline);
            instances.clear();
            started = now();
            state.collect_garbage();
            collection += ms(started, now());
        }
        std::cout << "{\"functions_per_script\":" << functions << ",\"sandboxes\":2000,\"cold_ms\":" << cold
                  << ",\"second_use_ms\":" << second << ",\"source_bytes\":" << source.size()
                  << ",\"construction_ms\":" << construction << ",\"collection_ms\":" << collection
                  << ",\"peak_live_lua_bytes\":" << peakLiveBytes << "}\n";
    }
}
