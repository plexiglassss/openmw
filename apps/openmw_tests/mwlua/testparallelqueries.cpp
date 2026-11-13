#include "apps/openmw/mwlua/context.hpp"
#include "apps/openmw/mwlua/corebindings.hpp"
#include "apps/openmw/mwlua/nativequeryservice.hpp"
#include "apps/openmw/mwlua/nearbybindings.hpp"
#include <components/misc/combinedperformance.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace
{
    TEST(ParallelQueryBindings, PublicWorkerAndDistanceSnapshotRoundTrip)
    {
        Misc::NativeJobPool pool(2, 8);
        MWLua::NativeQueryService service(pool);
        LuaUtil::LuaState runtime(nullptr, nullptr);
        MWLua::Context context{ MWLua::Context::Local };
        context.mLua = &runtime;
        context.mNativeQueryService = &service;
        sol::state_view lua(runtime.unsafeState());
        sol::table core(lua, sol::create), nearby(lua, sol::create);
        MWLua::addIsolatedWorkerBindings(core, context);
        MWLua::addNearbyDistanceBindings(nearby, context);
        lua["core"] = core;
        lua["nearby"] = nearby;
        lua["zero"] = osg::Vec3f();
        lua["near"] = osg::Vec3f(3, 4, 0);
        lua["far"] = osg::Vec3f(0, 0, 8);
        lua["expectWorker"] = Misc::useIsolatedLuaWorkers();
        lua["expectDistances"] = Misc::useAsyncDistanceJobs();
        RecordProperty("batched_visual_rays", Misc::useBatchedVisualRays() ? 1 : 0);
        RecordProperty("async_distances", Misc::useAsyncDistanceJobs() ? 1 : 0);
        RecordProperty("isolated_workers", Misc::useIsolatedLuaWorkers() ? 1 : 0);
        auto run = [&](const char* code) {
            LuaUtil::LuaState::throwIfError(lua.safe_script(code, sol::script_pass_on_error));
        };
        run(R"(
            assert((nearby.queryDistancesAsync ~= nil) == expectDistances)
            assert((core.defineWorker ~= nil) == expectWorker)
            pending = {}
            if expectWorker then
                assert(type(core.loadWorker)=='function' and not pcall(core.loadWorker,'../invalid'))
                launch = core.defineWorker('return function(t) table.sort(t) return t end')
                local input = {9, 5, 1}
                pending[1] = {assert(launch(input)), function(result) assert(result[1]==1 and result[2]==5 and result[3]==9) end}
                input[2] = 8
                assert(not pcall(launch, {1, 'not a number'}))
                assert(not pcall(launch, {hidden=42}))
                assert(not pcall(launch, zero))
            end
            if expectDistances then
                local source = {far,near,near,zero}
                s = nearby.snapshotPositions(source)
                source[2] = far
                local options = {maxDistance=5, sortByDistance=true}
                local function check(r)
                    assert(#r==3 and r:sourceIndex(1)==4 and r:sourceIndex(2)==2 and r:sourceIndex(3)==3)
                    assert(r:distance(2)==5 and r:distance2D(2)==5 and r:heightDifference(2)==0 and not r:object(2))
                end
                check(nearby.queryDistances(s, zero, options))
                pending[#pending+1] = {assert(nearby.queryDistancesAsync(s,zero,options)),check}
            end
            function advance()
                for i=#pending,1,-1 do
                    local t = pending[i]; local status, value = t[1]:poll()
                    if status ~= 'pending' then
                        assert(status=='ready', tostring(value)); t[2](value)
                        assert(t[1]:poll()=='consumed'); table.remove(pending,i)
                    end
                end
                return #pending==0
            end
        )");
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!lua.get<sol::function>("advance").call<bool>())
        {
            ASSERT_LT(std::chrono::steady_clock::now(), limit);
            std::this_thread::yield();
        }
        run("if expectWorker then a=assert(launch({1})); a:cancel(); assert(a:poll()=='cancelled') end");
        run("if expectDistances then previous=assert(nearby.queryDistancesAsync(s,zero)) end");
        service.reset();
        run("if expectDistances then assert(previous:poll()=='cancelled'); assert(not pcall(nearby.queryDistancesAsync,s,zero)) end");
    }
}
