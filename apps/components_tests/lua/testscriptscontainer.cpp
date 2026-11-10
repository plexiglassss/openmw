#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <sstream>

#include <array>

#include <components/esm/luascripts.hpp>

#include <components/lua/asyncpackage.hpp>
#include <components/lua/luastate.hpp>
#include <components/lua/scriptscontainer.hpp>
#include <components/lua/scripttracker.hpp>

#include <components/testing/util.hpp>

namespace
{
    using namespace testing;
    using namespace TestingOpenMW;

    constexpr VFS::Path::NormalizedView invalidPath("invalid.lua");

    VFSTestFile invalidScript("not a script");

    constexpr VFS::Path::NormalizedView incorrectPath("incorrect.lua");

    VFSTestFile incorrectScript(
        "return { incorrectSection = {}, engineHandlers = { incorrectHandler = function() end } }");

    constexpr VFS::Path::NormalizedView emptyPath("empty.lua");

    VFSTestFile emptyScript("");

    constexpr VFS::Path::NormalizedView test1Path("test1.lua");
    constexpr VFS::Path::NormalizedView test2Path("test2.lua");

    VFSTestFile testScript(R"X(
return {
    engineHandlers = {
        onUpdate = function(dt) print(' update ' .. tostring(dt)) end,
        onLoad = function() print('load') end,
    },
    eventHandlers = {
        Event1 = function(eventData) print(' event1 ' .. tostring(eventData.x)) end,
        Event2 = function(eventData) print(' event2 ' .. tostring(eventData.x)) end,
        Print = function() print('print') end
    }
}
)X");

    constexpr VFS::Path::NormalizedView stopEventPath("stopevent.lua");

    VFSTestFile stopEventScript(R"X(
return {
    eventHandlers = {
        Event1 = function(eventData)
            print(' event1 ' .. tostring(eventData.x))
            return eventData.x >= 1
        end
    }
}
)X");

    constexpr VFS::Path::NormalizedView loadSave1Path("loadsave1.lua");
    constexpr VFS::Path::NormalizedView loadSave2Path("loadsave2.lua");

    VFSTestFile loadSaveScript(R"X(
x = 0
y = 0
return {
    engineHandlers = {
        onSave = function(state)
            return {x = x, y = y}
        end,
        onLoad = function(state)
            x, y = state.x, state.y
        end
    },
    eventHandlers = {
        Set = function(eventData)
            eventData.n = eventData.n - 1
            if eventData.n == 0 then
                x, y = eventData.x, eventData.y
            end
        end,
        Print = function()
            print(x, y)
        end
    }
}
)X");

    constexpr VFS::Path::NormalizedView testInterfacePath("testinterface.lua");

    VFSTestFile interfaceScript(R"X(
return {
    interfaceName = "TestInterface",
    interface = {
        fn = function(x) print('FN', x) end,
        value = 3.5
    },
}
)X");

    constexpr VFS::Path::NormalizedView overrideInterfacePath("overrideinterface.lua");

    VFSTestFile overrideInterfaceScript(R"X(
local old = nil
local interface = {
    fn = function(x)
        print('NEW FN', x)
        old.fn(x)
    end,
    value,
}
return {
    interfaceName = "TestInterface",
    interface = interface,
    engineHandlers = {
        onInit = function() print('init') end,
        onLoad = function() print('load') end,
        onInterfaceOverride = function(oldInterface)
            print('override')
            old = oldInterface
            interface.value = oldInterface.value + 1
        end
    },
}
)X");

    constexpr VFS::Path::NormalizedView useInterfacePath("useinterface.lua");

    VFSTestFile useInterfaceScript(R"X(
local interfaces = require('openmw.interfaces')
return {
    engineHandlers = {
        onUpdate = function()
            interfaces.TestInterface.fn(interfaces.TestInterface.value)
        end,
    },
}
)X");

    constexpr VFS::Path::NormalizedView unloadPath("unload.lua");

    VFSTestFile unloadScript(R"X(
x = 0
y = 0
z = 0
return {
    engineHandlers = {
        onSave = function(state)
            print('saving', x, y, z)
            return {x = x, y = y}
        end,
        onLoad = function(state)
            x, y = state.x, state.y
            print('loaded', x, y, z)
        end
    },
    eventHandlers = {
        Set = function(eventData)
            x, y, z = eventData.x, eventData.y, eventData.z
        end
    }
}
)X");

    constexpr VFS::Path::NormalizedView customDataPath("customdata.lua");

    VFSTestFile customDataScript(R"X(
data = nil
return {
    engineHandlers = {
        onSave = function()
            return data
        end,
        onLoad = function(state)
            data = state
        end,
        onInit = function(state)
            data = state
        end
    },
    eventHandlers = {
        WakeUp = function()
        end
    }
}
)X");

    constexpr VFS::Path::NormalizedView cacheActivityPath("cacheactivity.lua");

    VFSTestFile cacheActivityScript(R"X(
print('started')
return {
    engineHandlers = {
        onActive = function() print('active') end,
        onInactive = function() print('inactive') end,
        onSave = function() print('saved') end,
        onLoad = function() print('loaded') end,
    }
}
)X");

    constexpr VFS::Path::NormalizedView timerLifecyclePath("timerlifecycle.lua");

    VFSTestFile timerLifecycleScript(R"X(
local async = require('openmw.async')
local simulation = async:registerTimerCallback('simulation', function(value) print('simulation', value) end)
local game = async:registerTimerCallback('game', function(value) print('game', value) end)
return {
    engineHandlers = {
        onInit = function()
            print('init')
            async:newSimulationTimer(0, simulation, 'init')
        end,
        onLoad = function()
            print('load')
            async:newGameTimer(0, game, 'load')
        end,
        onSave = function() return {} end,
    },
}
)X");

    thread_local std::uint64_t timerBenchmarkLuaCalls = 0;

    void countTimerBenchmarkLuaCalls(lua_State*, lua_Debug* debug)
    {
        if (debug->event == LUA_HOOKCALL)
            ++timerBenchmarkLuaCalls;
    }

    class ActivityScriptsContainer : public LuaUtil::ScriptsContainer
    {
        bool mActive = false;
        EngineHandlerList mOnActive{ "onActive" };
        EngineHandlerList mOnInactive{ "onInactive" };

    public:
        ActivityScriptsContainer(LuaUtil::LuaState* lua, LuaUtil::ScriptTracker* tracker)
            : ScriptsContainer(lua, "Test", tracker, false)
        {
            registerEngineHandlers({ &mOnActive, &mOnInactive });
        }

        bool isActive() const override { return mActive; }

        void setActive(bool active)
        {
            mActive = active;
            if (active)
                callEngineHandlers(mOnActive);
            else
                callEngineHandlers(mOnInactive);
        }
    };

    constexpr VFS::Path::NormalizedView framePath("frame.lua");
    constexpr VFS::Path::NormalizedView followerPath("follower.lua");
    VFSTestFile frameScript(R"(
        local count = 0
        return { engineHandlers = { onUpdate = function(dt)
            count = count + 1
            if dt < 0 then error("frame handler failure") end
            if dt == 2 then print("frame", count) end
        end }}
    )");
    VFSTestFile followerScript("return {engineHandlers={onUpdate=function() print('following handler') end}}");

    struct LuaScriptsContainerTest : Test
    {
        std::unique_ptr<VFS::Manager> mVFS = createTestVFS({
            { framePath, &frameScript },
            { followerPath, &followerScript },
            { invalidPath, &invalidScript },
            { incorrectPath, &incorrectScript },
            { emptyPath, &emptyScript },
            { test1Path, &testScript },
            { test2Path, &testScript },
            { stopEventPath, &stopEventScript },
            { loadSave1Path, &loadSaveScript },
            { loadSave2Path, &loadSaveScript },
            { testInterfacePath, &interfaceScript },
            { overrideInterfacePath, &overrideInterfaceScript },
            { useInterfacePath, &useInterfaceScript },
            { unloadPath, &unloadScript },
            { customDataPath, &customDataScript },
            { cacheActivityPath, &cacheActivityScript },
            { timerLifecyclePath, &timerLifecycleScript },
        });

        LuaUtil::ScriptsConfiguration mCfg;
        LuaUtil::LuaState mLua{ mVFS.get(), &mCfg };

        LuaScriptsContainerTest()
        {
            ESM::LuaScriptsCfg cfg;
            LuaUtil::parseOMWScripts(cfg, R"X(
CUSTOM: invalid.lua
CUSTOM: incorrect.lua
CUSTOM: empty.lua
CUSTOM: test1.lua
CUSTOM: stopEvent.lua
CUSTOM: test2.lua
NPC: loadSave1.lua
CUSTOM, NPC: loadSave2.lua
CUSTOM, PLAYER: testInterface.lua
CUSTOM, PLAYER: overrideInterface.lua
CUSTOM, PLAYER: useInterface.lua
CUSTOM: unload.lua
CUSTOM: customdata.lua
CUSTOM: cacheactivity.lua
CUSTOM: timerlifecycle.lua
CUSTOM: frame.lua
CUSTOM: follower.lua
)X");
            mCfg.init(std::move(cfg), false);
        }

        int getId(VFS::Path::NormalizedView path) const
        {
            const std::optional<int> id = mCfg.findId(path);
            if (!id.has_value())
                throw std::invalid_argument("Script id is not found: " + std::string(path.value()));
            return *id;
        }

        void advanceCache(LuaUtil::ScriptTracker& tracker, int frames, double duration)
        {
            mLua.protectedCall([&](LuaUtil::LuaView& lua) {
                for (int i = 0; i < frames; ++i)
                    tracker.unloadInactiveScripts(lua, duration);
            });
        }

        void setUnloadState(LuaUtil::ScriptsContainer& scripts, int x, int y, int z)
        {
            mLua.protectedCall([&](LuaUtil::LuaView& lua) {
                scripts.receiveEvent("Set", LuaUtil::serialize(lua.sol().create_table_with("x", x, "y", y, "z", z)));
            });
        }

        sol::function timerCallback(std::function<void()> callback)
        {
            return sol::make_object(mLua.unsafeState(), std::move(callback));
        }
    };

    TEST_F(LuaScriptsContainerTest, addCustomScriptShouldNotStartInvalidScript)
    {
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");
        testing::internal::CaptureStdout();
        EXPECT_FALSE(scripts.addCustomScript(getId(invalidPath)));
        std::string output = testing::internal::GetCapturedStdout();
        EXPECT_THAT(output, HasSubstr("Can't start Test[invalid.lua]"));
    }

    TEST_F(LuaScriptsContainerTest, addCustomScriptShouldNotSuportScriptsWithInvalidHandlerAndSection)
    {
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");
        testing::internal::CaptureStdout();
        EXPECT_TRUE(scripts.addCustomScript(getId(incorrectPath)));
        std::string output = testing::internal::GetCapturedStdout();
        EXPECT_THAT(output, HasSubstr("Not supported handler 'incorrectHandler' in Test[incorrect.lua]"));
        EXPECT_THAT(output, HasSubstr("Not supported section 'incorrectSection' in Test[incorrect.lua]"));
    }

    TEST_F(LuaScriptsContainerTest, addCustomScriptShouldReturnFalseForDuplicates)
    {
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");
        EXPECT_TRUE(scripts.addCustomScript(getId(emptyPath)));
        EXPECT_FALSE(scripts.addCustomScript(getId(emptyPath)));
    }

    TEST_F(LuaScriptsContainerTest, CallHandler)
    {
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");
        testing::internal::CaptureStdout();
        EXPECT_TRUE(scripts.addCustomScript(getId(test1Path)));
        EXPECT_TRUE(scripts.addCustomScript(getId(stopEventPath)));
        EXPECT_TRUE(scripts.addCustomScript(getId(test2Path)));
        scripts.update(1.5f);
        EXPECT_EQ(internal::GetCapturedStdout(),
            "Test[test1.lua]:\t update 1.5\n"
            "Test[test2.lua]:\t update 1.5\n");
    }

    TEST_F(LuaScriptsContainerTest, UpdateHandlersKeepTracebacksOrderAndStateAfterCollection)
    {
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");
        ASSERT_TRUE(scripts.addCustomScript(getId(followerPath)));
        ASSERT_TRUE(scripts.addCustomScript(getId(framePath)));
        lua_gc(mLua.unsafeState(), LUA_GCCOLLECT, 0);
        testing::internal::CaptureStdout();
        scripts.update(-1.f);
        const auto failed = testing::internal::GetCapturedStdout();
        EXPECT_THAT(failed, HasSubstr("frame handler failure"));
        EXPECT_THAT(failed, HasSubstr("stack traceback"));
        EXPECT_THAT(failed, HasSubstr("following handler"));
        EXPECT_LT(failed.find("frame handler failure"), failed.find("following handler"));
        testing::internal::CaptureStdout();
        scripts.update(2.f);
        const auto recovered = testing::internal::GetCapturedStdout();
        EXPECT_THAT(recovered, HasSubstr("frame\t2"));
        EXPECT_THAT(recovered, HasSubstr("following handler"));
    }

    TEST_F(LuaScriptsContainerTest, RepeatedActorUpdateDispatchBenchmark)
    {
        LuaUtil::ScriptsContainer scripts(&mLua, "Bench");
        ASSERT_TRUE(scripts.addCustomScript(getId(framePath)));
        constexpr int iterations = 50000;
        std::array<double, 7> samples;
        for (auto& elapsed : samples)
        {
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < iterations; ++i)
                scripts.update(1.f);
            elapsed = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
        }
        std::sort(samples.begin(), samples.end());
        RecordProperty("median_ns_per_callback", std::to_string(samples[samples.size() / 2] / iterations));
    }

    TEST_F(LuaScriptsContainerTest, CallEvent)
    {
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");

        EXPECT_TRUE(scripts.addCustomScript(getId(test1Path)));
        EXPECT_TRUE(scripts.addCustomScript(getId(stopEventPath)));
        EXPECT_TRUE(scripts.addCustomScript(getId(test2Path)));

        sol::state_view sol = mLua.unsafeState();
        std::string x0 = LuaUtil::serialize(sol.create_table_with("x", 0.5));
        std::string x1 = LuaUtil::serialize(sol.create_table_with("x", 1.5));

        {
            testing::internal::CaptureStdout();
            scripts.receiveEvent("SomeEvent", x1);
            EXPECT_EQ(internal::GetCapturedStdout(), "");
        }
        {
            testing::internal::CaptureStdout();
            scripts.receiveEvent("Event1", x1);
            EXPECT_EQ(internal::GetCapturedStdout(),
                "Test[test2.lua]:\t event1 1.5\n"
                "Test[stopevent.lua]:\t event1 1.5\n"
                "Test[test1.lua]:\t event1 1.5\n");
        }
        {
            testing::internal::CaptureStdout();
            scripts.receiveEvent("Event2", x1);
            EXPECT_EQ(internal::GetCapturedStdout(),
                "Test[test2.lua]:\t event2 1.5\n"
                "Test[test1.lua]:\t event2 1.5\n");
        }
        {
            testing::internal::CaptureStdout();
            scripts.receiveEvent("Event1", x0);
            EXPECT_EQ(internal::GetCapturedStdout(),
                "Test[test2.lua]:\t event1 0.5\n"
                "Test[stopevent.lua]:\t event1 0.5\n");
        }
        {
            testing::internal::CaptureStdout();
            scripts.receiveEvent("Event2", x0);
            EXPECT_EQ(internal::GetCapturedStdout(),
                "Test[test2.lua]:\t event2 0.5\n"
                "Test[test1.lua]:\t event2 0.5\n");
        }
    }

    TEST_F(LuaScriptsContainerTest, RemoveScript)
    {
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");

        EXPECT_TRUE(scripts.addCustomScript(getId(test1Path)));
        EXPECT_TRUE(scripts.addCustomScript(getId(stopEventPath)));
        EXPECT_TRUE(scripts.addCustomScript(getId(test2Path)));

        sol::state_view sol = mLua.unsafeState();
        std::string x = LuaUtil::serialize(sol.create_table_with("x", 0.5));

        {
            testing::internal::CaptureStdout();
            scripts.update(1.5f);
            scripts.receiveEvent("Event1", x);
            EXPECT_EQ(internal::GetCapturedStdout(),
                "Test[test1.lua]:\t update 1.5\n"
                "Test[test2.lua]:\t update 1.5\n"
                "Test[test2.lua]:\t event1 0.5\n"
                "Test[stopevent.lua]:\t event1 0.5\n");
        }
        {
            testing::internal::CaptureStdout();
            const int stopEventScriptId = getId(stopEventPath);
            EXPECT_TRUE(scripts.hasScript(stopEventScriptId));
            scripts.removeScript(stopEventScriptId);
            EXPECT_FALSE(scripts.hasScript(stopEventScriptId));
            scripts.update(1.5f);
            scripts.receiveEvent("Event1", x);
            EXPECT_EQ(internal::GetCapturedStdout(),
                "Test[test1.lua]:\t update 1.5\n"
                "Test[test2.lua]:\t update 1.5\n"
                "Test[test2.lua]:\t event1 0.5\n"
                "Test[test1.lua]:\t event1 0.5\n");
        }
        {
            testing::internal::CaptureStdout();
            scripts.removeScript(getId(test1Path));
            scripts.update(1.5f);
            scripts.receiveEvent("Event1", x);
            EXPECT_EQ(internal::GetCapturedStdout(),
                "Test[test2.lua]:\t update 1.5\n"
                "Test[test2.lua]:\t event1 0.5\n");
        }
    }

    TEST_F(LuaScriptsContainerTest, AutoStart)
    {
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");
        scripts.setAutoStartConf(mCfg.getPlayerConf());
        testing::internal::CaptureStdout();
        scripts.addAutoStartedScripts();
        scripts.update(1.5f);
        EXPECT_EQ(internal::GetCapturedStdout(),
            "Test[overrideinterface.lua]:\toverride\n"
            "Test[overrideinterface.lua]:\tinit\n"
            "Test[overrideinterface.lua]:\tNEW FN\t4.5\n"
            "Test[testinterface.lua]:\tFN\t4.5\n");
    }

    TEST_F(LuaScriptsContainerTest, Interface)
    {
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");
        scripts.setAutoStartConf(mCfg.getLocalConf(ESM::REC_CREA, ESM::RefId(), ESM::RefNum()));
        const int addIfaceId = getId(testInterfacePath);
        const int overrideIfaceId = getId(overrideInterfacePath);
        const int useIfaceId = getId(useInterfacePath);

        testing::internal::CaptureStdout();
        scripts.addAutoStartedScripts();
        scripts.update(1.5f);
        EXPECT_EQ(internal::GetCapturedStdout(), "");

        testing::internal::CaptureStdout();
        EXPECT_TRUE(scripts.addCustomScript(addIfaceId));
        EXPECT_TRUE(scripts.addCustomScript(overrideIfaceId));
        EXPECT_TRUE(scripts.addCustomScript(useIfaceId));
        scripts.update(1.5f);
        scripts.removeScript(overrideIfaceId);
        scripts.update(1.5f);
        EXPECT_EQ(internal::GetCapturedStdout(),
            "Test[overrideinterface.lua]:\toverride\n"
            "Test[overrideinterface.lua]:\tinit\n"
            "Test[overrideinterface.lua]:\tNEW FN\t4.5\n"
            "Test[testinterface.lua]:\tFN\t4.5\n"
            "Test[testinterface.lua]:\tFN\t3.5\n");
    }

    TEST_F(LuaScriptsContainerTest, LoadSave)
    {
        LuaUtil::ScriptsContainer scripts1(&mLua, "Test");
        LuaUtil::ScriptsContainer scripts2(&mLua, "Test");
        LuaUtil::ScriptsContainer scripts3(&mLua, "Test");
        scripts1.setAutoStartConf(mCfg.getLocalConf(ESM::REC_NPC_, ESM::RefId(), ESM::RefNum()));
        scripts2.setAutoStartConf(mCfg.getLocalConf(ESM::REC_NPC_, ESM::RefId(), ESM::RefNum()));
        scripts3.setAutoStartConf(mCfg.getPlayerConf());

        scripts1.addAutoStartedScripts();
        EXPECT_TRUE(scripts1.addCustomScript(getId(test1Path)));

        sol::state_view sol = mLua.unsafeState();
        scripts1.receiveEvent("Set", LuaUtil::serialize(sol.create_table_with("n", 1, "x", 0.5, "y", 3.5)));
        scripts1.receiveEvent("Set", LuaUtil::serialize(sol.create_table_with("n", 2, "x", 2.5, "y", 1.5)));

        ESM::LuaScripts data;
        scripts1.save(data);

        {
            testing::internal::CaptureStdout();
            scripts2.load(data);
            scripts2.receiveEvent("Print", "");
            EXPECT_EQ(internal::GetCapturedStdout(),
                "Test[test1.lua]:\tload\n"
                "Test[loadsave2.lua]:\t0.5\t3.5\n"
                "Test[loadsave1.lua]:\t2.5\t1.5\n"
                "Test[test1.lua]:\tprint\n");
            EXPECT_FALSE(scripts2.hasScript(getId(testInterfacePath)));
        }
        {
            testing::internal::CaptureStdout();
            scripts3.load(data);
            scripts3.receiveEvent("Print", "");
            EXPECT_EQ(internal::GetCapturedStdout(),
                "Ignoring Test[loadsave1.lua]; this script is not allowed here\n"
                "Test[test1.lua]:\tload\n"
                "Test[overrideinterface.lua]:\toverride\n"
                "Test[overrideinterface.lua]:\tinit\n"
                "Test[loadsave2.lua]:\t0.5\t3.5\n"
                "Test[test1.lua]:\tprint\n");
            EXPECT_TRUE(scripts3.hasScript(getId(testInterfacePath)));
        }
    }

    TEST_F(LuaScriptsContainerTest, Timers)
    {
        using TimerType = LuaUtil::ScriptsContainer::TimerType;
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");
        const int test1Id = getId(test1Path);
        const int test2Id = getId(test2Path);

        testing::internal::CaptureStdout();
        EXPECT_TRUE(scripts.addCustomScript(test1Id));
        EXPECT_TRUE(scripts.addCustomScript(test2Id));
        EXPECT_EQ(internal::GetCapturedStdout(), "");

        int counter1 = 0, counter2 = 0, counter3 = 0, counter4 = 0;
        sol::function fn1 = sol::make_object(mLua.unsafeState(), [&]() { counter1++; });
        sol::function fn2 = sol::make_object(mLua.unsafeState(), [&]() { counter2++; });
        sol::function fn3 = sol::make_object(mLua.unsafeState(), [&](int d) { counter3 += d; });
        sol::function fn4 = sol::make_object(mLua.unsafeState(), [&](int d) { counter4 += d; });

        scripts.registerTimerCallback(test1Id, "A", fn3);
        scripts.registerTimerCallback(test1Id, "B", fn4);
        scripts.registerTimerCallback(test2Id, "B", fn3);
        scripts.registerTimerCallback(test2Id, "A", fn4);

        scripts.processTimers(1, 2);

        scripts.setupSerializableTimer(
            TimerType::SIMULATION_TIME, 10, test1Id, "B", sol::make_object(mLua.unsafeState(), 3));
        scripts.setupSerializableTimer(TimerType::GAME_TIME, 10, test2Id, "B", sol::make_object(mLua.unsafeState(), 4));
        scripts.setupSerializableTimer(
            TimerType::SIMULATION_TIME, 5, test1Id, "A", sol::make_object(mLua.unsafeState(), 1));
        scripts.setupSerializableTimer(TimerType::GAME_TIME, 5, test2Id, "A", sol::make_object(mLua.unsafeState(), 2));
        scripts.setupSerializableTimer(
            TimerType::SIMULATION_TIME, 15, test1Id, "A", sol::make_object(mLua.unsafeState(), 10));
        scripts.setupSerializableTimer(
            TimerType::SIMULATION_TIME, 15, test1Id, "B", sol::make_object(mLua.unsafeState(), 20));

        scripts.setupUnsavableTimer(TimerType::SIMULATION_TIME, 10, test2Id, fn2);
        scripts.setupUnsavableTimer(TimerType::GAME_TIME, 10, test1Id, fn2);
        scripts.setupUnsavableTimer(TimerType::SIMULATION_TIME, 5, test2Id, fn1);
        scripts.setupUnsavableTimer(TimerType::GAME_TIME, 5, test1Id, fn1);
        scripts.setupUnsavableTimer(TimerType::SIMULATION_TIME, 15, test2Id, fn1);

        EXPECT_EQ(counter1, 0);
        EXPECT_EQ(counter3, 0);

        scripts.processTimers(6, 4);

        EXPECT_EQ(counter1, 1);
        EXPECT_EQ(counter3, 1);
        EXPECT_EQ(counter4, 0);

        scripts.processTimers(6, 8);

        EXPECT_EQ(counter1, 2);
        EXPECT_EQ(counter2, 0);
        EXPECT_EQ(counter3, 1);
        EXPECT_EQ(counter4, 2);

        scripts.processTimers(11, 12);

        EXPECT_EQ(counter1, 2);
        EXPECT_EQ(counter2, 2);
        EXPECT_EQ(counter3, 5);
        EXPECT_EQ(counter4, 5);

        testing::internal::CaptureStdout();
        ESM::LuaScripts data;
        scripts.save(data);
        scripts.load(data);
        scripts.registerTimerCallback(test1Id, "B", fn4);
        EXPECT_EQ(internal::GetCapturedStdout(), "Test[test1.lua]:\tload\nTest[test2.lua]:\tload\n");

        testing::internal::CaptureStdout();
        scripts.processTimers(20, 20);
        EXPECT_EQ(internal::GetCapturedStdout(), "Test[test1.lua] callTimer failed: Callback 'A' doesn't exist\n");

        EXPECT_EQ(counter1, 2);
        EXPECT_EQ(counter2, 2);
        EXPECT_EQ(counter3, 5);
        EXPECT_EQ(counter4, 25);
    }

    TEST_F(LuaScriptsContainerTest, EmptyTimerQueuesAllowLaterSchedulingInBothClocks)
    {
        using TimerType = LuaUtil::ScriptsContainer::TimerType;
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");
        const int scriptId = getId(emptyPath);
        ASSERT_TRUE(scripts.addCustomScript(scriptId));
        std::vector<int> called;
        sol::function callback = sol::make_object(mLua.unsafeState(), [&](int value) { called.push_back(value); });
        scripts.registerTimerCallback(scriptId, "record", callback);

        for (int i = 0; i < 100; ++i)
            scripts.processTimers(0, 0);
        EXPECT_TRUE(called.empty());
        scripts.setupSerializableTimer(
            TimerType::SIMULATION_TIME, 10, scriptId, "record", sol::make_object(mLua.unsafeState(), 1));
        scripts.setupSerializableTimer(
            TimerType::GAME_TIME, 20, scriptId, "record", sol::make_object(mLua.unsafeState(), 2));
        scripts.processTimers(9, 19);
        EXPECT_TRUE(called.empty());
        scripts.processTimers(10, 19);
        EXPECT_EQ(called, (std::vector<int>{ 1 }));
        scripts.processTimers(10, 20);
        EXPECT_EQ(called, (std::vector<int>{ 1, 2 }));
        scripts.processTimers(30, 30);

        sol::function unsavable = timerCallback([&] { called.push_back(3); });
        scripts.setupUnsavableTimer(TimerType::GAME_TIME, 40, scriptId, unsavable);
        scripts.processTimers(40, 39);
        EXPECT_EQ(called.size(), 2);
        scripts.processTimers(40, 40);
        scripts.processTimers(40, 40);
        EXPECT_EQ(called, (std::vector<int>{ 1, 2, 3 }));
    }

    TEST_F(LuaScriptsContainerTest, EmptyTimerQueuesPreserveReentrantSchedulingAndProcessing)
    {
        using TimerType = LuaUtil::ScriptsContainer::TimerType;
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");
        const int scriptId = getId(emptyPath);
        ASSERT_TRUE(scripts.addCustomScript(scriptId));
        scripts.processTimers(0, 0);

        std::vector<int> called;
        sol::function simulation = timerCallback([&] { called.push_back(2); });
        sol::function game = timerCallback([&] { called.push_back(3); });
        sol::function outer = timerCallback([&] {
            called.push_back(1);
            // The outer timer has already been popped. A reentrant empty call
            // must remain harmless before either callback is scheduled.
            scripts.processTimers(10, 10);
            scripts.setupUnsavableTimer(TimerType::SIMULATION_TIME, 10, scriptId, simulation);
            scripts.setupUnsavableTimer(TimerType::GAME_TIME, 10, scriptId, game);
            scripts.processTimers(10, 10);
            called.push_back(4);
        });
        scripts.setupUnsavableTimer(TimerType::SIMULATION_TIME, 10, scriptId, outer);
        scripts.processTimers(10, 10);
        EXPECT_EQ(called, (std::vector<int>{ 1, 2, 3, 4 }));
        scripts.processTimers(10, 10);
        EXPECT_EQ(called, (std::vector<int>{ 1, 2, 3, 4 }));
    }

    TEST_F(LuaScriptsContainerTest, NoDueTimerProcessingRetainsTrackerActivityUntilProcessingStops)
    {
        LuaUtil::ScriptTracker tracker;
        LuaUtil::ScriptsContainer scripts(&mLua, "Test", &tracker, false);
        ASSERT_TRUE(scripts.addCustomScript(getId(unloadPath)));
        setUnloadState(scripts, 3, 2, 1);
        int futureCalls = 0;
        sol::function future = timerCallback([&] { ++futureCalls; });
        testing::internal::CaptureStdout();
        for (int i = 0; i < 1500; ++i)
        {
            if (i == 500)
                scripts.setupUnsavableTimer(
                    LuaUtil::ScriptsContainer::TimerType::SIMULATION_TIME, 1e9, getId(unloadPath), future);
            scripts.processTimers(i, i);
            advanceCache(tracker, 1, 1.0 / 60);
        }
        EXPECT_EQ(tracker.size(), 1);
        EXPECT_EQ(futureCalls, 0);
        EXPECT_EQ(internal::GetCapturedStdout(), "");

        testing::internal::CaptureStdout();
        ESM::LuaScripts saved;
        scripts.save(saved);
        // z is deliberately absent from saved state; keeping its value proves
        // repeated empty timer processing did not unload/reconstruct the script.
        EXPECT_EQ(internal::GetCapturedStdout(), "Test[unload.lua]:\tsaving\t3\t2\t1\n");

        testing::internal::CaptureStdout();
        advanceCache(tracker, 1000, 1.0 / 60);
        EXPECT_EQ(tracker.size(), 0);
        EXPECT_EQ(internal::GetCapturedStdout(), "Test[unload.lua]:\tsaving\t3\t2\t1\n");

        testing::internal::CaptureStdout();
        scripts.processTimers(3000, 3000);
        EXPECT_EQ(tracker.size(), 1);
        EXPECT_EQ(internal::GetCapturedStdout(), "Test[unload.lua]:\tloaded\t3\t2\t0\n");
    }

    TEST_F(LuaScriptsContainerTest, UnloadedTimerProcessingStillInitializesAndRunsTimersCreatedByLoadHandlers)
    {
        mLua.protectedCall([&](LuaUtil::LuaView& lua) {
            mLua.addCommonPackage("openmw.async",
                LuaUtil::getAsyncPackageInitializer(lua.sol(), [] { return 0.; }, [] { return 0.; }));
        });
        LuaUtil::ScriptsContainer scripts(&mLua, "Test", nullptr, false);
        const int scriptId = getId(timerLifecyclePath);
        scripts.setAutoStartConf({ { scriptId, {} } });

        testing::internal::CaptureStdout();
        scripts.processTimers(0, 0);
        EXPECT_EQ(internal::GetCapturedStdout(),
            "Test[timerlifecycle.lua]:\tinit\nTest[timerlifecycle.lua]:\tsimulation\tinit\n");
        ASSERT_TRUE(scripts.hasScript(scriptId));

        ESM::LuaScripts saved;
        scripts.save(saved);
        scripts.load(saved);
        testing::internal::CaptureStdout();
        scripts.processTimers(0, 0);
        EXPECT_EQ(internal::GetCapturedStdout(),
            "Test[timerlifecycle.lua]:\tload\nTest[timerlifecycle.lua]:\tgame\tload\n");
        testing::internal::CaptureStdout();
        scripts.processTimers(0, 0);
        EXPECT_EQ(internal::GetCapturedStdout(), "");
    }

    TEST_F(LuaScriptsContainerTest, NoDueTimersPreserveExactDeadlineAndNonfiniteComparisons)
    {
        using TimerType = LuaUtil::ScriptsContainer::TimerType;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        struct Case
        {
            double mDeadline;
            double mNow;
            bool mDue;
        };
        const Case cases[] = { { 5, 4, false }, { 5, 5, true }, { 5, 6, true }, { 5, nan, false },
            { nan, 5, false }, { nan, inf, false }, { inf, 5, false }, { inf, inf, true },
            { -inf, -inf, true }, { -inf, 5, true }, { 5, -inf, false }, { 5, inf, true } };
        for (TimerType clock : { TimerType::SIMULATION_TIME, TimerType::GAME_TIME })
            for (const auto& test : cases)
            {
                SCOPED_TRACE(::testing::Message() << "clock=" << static_cast<int>(clock)
                                                 << " deadline=" << test.mDeadline << " now=" << test.mNow);
                LuaUtil::ScriptsContainer scripts(&mLua, "Test");
                const int scriptId = getId(emptyPath);
                ASSERT_TRUE(scripts.addCustomScript(scriptId));
                int called = 0;
                sol::function callback = timerCallback([&] { ++called; });
                scripts.setupUnsavableTimer(clock, test.mDeadline, scriptId, callback);
                scripts.processTimers(clock == TimerType::SIMULATION_TIME ? test.mNow : 0,
                    clock == TimerType::GAME_TIME ? test.mNow : 0);
                EXPECT_EQ(called, test.mDue ? 1 : 0);
            }
    }

    TEST_F(LuaScriptsContainerTest, GameTimerSchedulingSimulationTimerPreservesNextPassOrdering)
    {
        using TimerType = LuaUtil::ScriptsContainer::TimerType;
        LuaUtil::ScriptsContainer scripts(&mLua, "Test");
        const int scriptId = getId(emptyPath);
        ASSERT_TRUE(scripts.addCustomScript(scriptId));
        std::vector<int> called;
        sol::function simulation = timerCallback([&] { called.push_back(3); });
        sol::function nextGame = timerCallback([&] { called.push_back(2); });
        sol::function firstGame = timerCallback([&] {
            called.push_back(1);
            scripts.setupUnsavableTimer(TimerType::SIMULATION_TIME, 10, scriptId, simulation);
            scripts.setupUnsavableTimer(TimerType::GAME_TIME, 10, scriptId, nextGame);
        });
        scripts.setupUnsavableTimer(TimerType::GAME_TIME, 10, scriptId, firstGame);
        scripts.processTimers(9, 9);
        EXPECT_TRUE(called.empty());
        scripts.processTimers(10, 10);
        EXPECT_EQ(called, (std::vector<int>{ 1, 2 }));
        scripts.processTimers(10, 10);
        EXPECT_EQ(called, (std::vector<int>{ 1, 2, 3 }));
    }

    TEST_F(LuaScriptsContainerTest, NoDueTimerDispatchNativeBenchmark)
    {
        // Measures the actual processTimers implementation on native loaded
        // containers. Compare otherwise identical baseline/candidate executables.
        // Empty/future-only queues are authored upper-coverage workloads, not a
        // town replay; each call is one container, not one script callback.
        constexpr int containers = 160;
        constexpr int frames = 2048;
        constexpr int rounds = 7;
        std::vector<std::unique_ptr<LuaUtil::ScriptsContainer>> scripts;
        for (int i = 0; i < containers; ++i)
        {
            auto container = std::make_unique<LuaUtil::ScriptsContainer>(&mLua, "TimerBenchmark");
            ASSERT_TRUE(container->addCustomScript(getId(emptyPath)));
            scripts.push_back(std::move(container));
        }

        lua_State* lua = mLua.unsafeState();
        sol::function future = timerCallback([] {});
        for (bool queued : { false, true })
        {
            const std::string prefix = queued ? "future" : "empty";
            if (queued)
                for (const auto& container : scripts)
                {
                    container->setupUnsavableTimer(LuaUtil::ScriptsContainer::TimerType::SIMULATION_TIME,
                        1e9, getId(emptyPath), future);
                    container->setupUnsavableTimer(
                        LuaUtil::ScriptsContainer::TimerType::GAME_TIME, 1e9, getId(emptyPath), future);
                }
            for (const auto& container : scripts)
                container->processTimers(0, 0);
            const lua_Hook previousHook = lua_gethook(lua);
            const int previousMask = lua_gethookmask(lua);
            const int previousCount = lua_gethookcount(lua);
            timerBenchmarkLuaCalls = 0;
            lua_sethook(lua, countTimerBenchmarkLuaCalls, LUA_MASKCALL, 0);
            for (const auto& container : scripts)
                container->processTimers(0, 0);
            lua_sethook(lua, previousHook, previousMask, previousCount);
            RecordProperty(prefix + "_lua_call_events_per_160_containers", std::to_string(timerBenchmarkLuaCalls));

            std::vector<double> samples;
            for (int round = 0; round < rounds; ++round)
            {
                const auto start = std::chrono::steady_clock::now();
                for (int frame = 0; frame < frames; ++frame)
                {
                    // Both executables use this compiler memory barrier once
                    // per simulated frame to prevent cross-frame hoisting of
                    // queue reads and activity writes under LTO.
                    std::atomic_signal_fence(std::memory_order_seq_cst);
                    for (const auto& container : scripts)
                        container->processTimers(frame, frame);
                }
                const auto elapsed = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start);
                samples.push_back(elapsed.count() / (frames * containers));
            }
            std::ostringstream sampleText;
            for (std::size_t i = 0; i < samples.size(); ++i)
                sampleText << (i == 0 ? "" : ",") << samples[i];
            std::sort(samples.begin(), samples.end());
            RecordProperty(prefix + "_samples_ns_per_container", sampleText.str());
            RecordProperty(prefix + "_median_ns_per_container", std::to_string(samples[samples.size() / 2]));
        }
        RecordProperty("lua_profiler_enabled", LuaUtil::LuaState::isProfilerEnabled() ? "true" : "false");
        RecordProperty("lua_runtime", LuaUtil::getLuaVersion());
        RecordProperty("scope", "actual native processTimers; 160 loaded containers; empty/future queues; no game FPS claim");

        int called = 0;
        sol::function sentinel = timerCallback([&] { ++called; });
        for (const auto& container : scripts)
        {
            container->setupUnsavableTimer(
                LuaUtil::ScriptsContainer::TimerType::SIMULATION_TIME, 1, getId(emptyPath), sentinel);
            container->processTimers(1, 0);
        }
        EXPECT_EQ(called, containers);
    }

    TEST_F(LuaScriptsContainerTest, CallbackWrapper)
    {
        sol::state_view view = mLua.unsafeState();
        LuaUtil::Callback callback{ view["print"], sol::table(view, sol::create) };
        callback.mHiddenData[LuaUtil::ScriptsContainer::sScriptDebugNameKey] = "some_script.lua";
        callback.mHiddenData[LuaUtil::ScriptsContainer::sScriptIdKey] = LuaUtil::ScriptId{ nullptr, 0 };

        testing::internal::CaptureStdout();
        callback.call(1.5);
        EXPECT_EQ(internal::GetCapturedStdout(), "1.5\n");

        testing::internal::CaptureStdout();
        callback.call(1.5, 2.5);
        EXPECT_EQ(internal::GetCapturedStdout(), "1.5\t2.5\n");

        const Debug::Level level = std::exchange(Log::sMinDebugLevel, Debug::All);

        testing::internal::CaptureStdout();
        callback.mHiddenData[LuaUtil::ScriptsContainer::sScriptIdKey] = sol::nil;
        callback.call(1.5, 2.5);
        EXPECT_EQ(internal::GetCapturedStdout(), "Ignored callback to the removed script some_script.lua\n");

        Log::sMinDebugLevel = level;
    }

    TEST_F(LuaScriptsContainerTest, Unload)
    {
        LuaUtil::ScriptTracker tracker;
        LuaUtil::ScriptsContainer scripts1(&mLua, "Test", &tracker, false);

        EXPECT_TRUE(scripts1.addCustomScript(*mCfg.findId(unloadPath)));
        EXPECT_EQ(tracker.size(), 1);

        mLua.protectedCall([&](LuaUtil::LuaView& lua) {
            scripts1.receiveEvent("Set", LuaUtil::serialize(lua.sol().create_table_with("x", 3, "y", 2, "z", 1)));
            testing::internal::CaptureStdout();
            for (int i = 0; i < 600; ++i)
                tracker.unloadInactiveScripts(lua);
            EXPECT_EQ(tracker.size(), 0);
            scripts1.receiveEvent("Set", LuaUtil::serialize(lua.sol().create_table_with("x", 10, "y", 20, "z", 30)));
            EXPECT_EQ(internal::GetCapturedStdout(),
                "Test[unload.lua]:\tsaving\t3\t2\t1\n"
                "Test[unload.lua]:\tloaded\t3\t2\t0\n");
        });
        EXPECT_EQ(tracker.size(), 1);
        ESM::LuaScripts data;
        scripts1.save(data);
        EXPECT_EQ(tracker.size(), 1);
        mLua.protectedCall([&](LuaUtil::LuaView& lua) {
            for (int i = 0; i < 600; ++i)
                tracker.unloadInactiveScripts(lua);
        });
        EXPECT_EQ(tracker.size(), 0);
        scripts1.load(data);
        EXPECT_EQ(tracker.size(), 0);
    }

    TEST_F(LuaScriptsContainerTest, InactiveCacheRetainsUnsavedStateAndAllowsExplicitSave)
    {
        LuaUtil::ScriptTracker tracker(30, std::numeric_limits<std::uint64_t>::max());
        LuaUtil::ScriptsContainer scripts(&mLua, "Test", &tracker, false);
        ASSERT_TRUE(scripts.addCustomScript(getId(unloadPath)));
        setUnloadState(scripts, 3, 2, 1);

        testing::internal::CaptureStdout();
        advanceCache(tracker, 600, 1.0 / 60); // Past the ordinary frame TTL, but only ten elapsed seconds.
        EXPECT_EQ(tracker.size(), 1);

        ESM::LuaScripts data;
        scripts.save(data);
        EXPECT_EQ(tracker.size(), 1);
        EXPECT_EQ(data.mScripts.size(), 1);
        // z is deliberately not serialized by unload.lua. It survives because the sandbox was retained.
        EXPECT_EQ(internal::GetCapturedStdout(), "Test[unload.lua]:\tsaving\t3\t2\t1\n");
    }

    TEST_F(LuaScriptsContainerTest, InactiveCacheExpiresAndReloadsSavedStateRepeatedly)
    {
        LuaUtil::ScriptTracker tracker(30, std::numeric_limits<std::uint64_t>::max());
        LuaUtil::ScriptsContainer scripts(&mLua, "Test", &tracker, false);
        ASSERT_TRUE(scripts.addCustomScript(getId(unloadPath)));
        setUnloadState(scripts, 3, 2, 1);

        testing::internal::CaptureStdout();
        advanceCache(tracker, 800, 0.05);
        EXPECT_EQ(tracker.size(), 0);
        setUnloadState(scripts, 10, 20, 30);
        EXPECT_EQ(tracker.size(), 1);
        advanceCache(tracker, 800, 0.05);
        EXPECT_EQ(tracker.size(), 0);
        setUnloadState(scripts, 100, 200, 300);
        EXPECT_EQ(tracker.size(), 1);
        EXPECT_EQ(internal::GetCapturedStdout(),
            "Test[unload.lua]:\tsaving\t3\t2\t1\n"
            "Test[unload.lua]:\tloaded\t3\t2\t0\n"
            "Test[unload.lua]:\tsaving\t10\t20\t30\n"
            "Test[unload.lua]:\tloaded\t10\t20\t0\n");
    }

    TEST_F(LuaScriptsContainerTest, InactiveCachePressureRestoresOrdinaryUnloading)
    {
        LuaUtil::ScriptTracker tracker(30, 1); // A live Lua state is larger than one byte.
        LuaUtil::ScriptsContainer scripts(&mLua, "Test", &tracker, false);
        ASSERT_TRUE(scripts.addCustomScript(getId(unloadPath)));
        setUnloadState(scripts, 3, 2, 1);

        testing::internal::CaptureStdout();
        advanceCache(tracker, 600, 1.0 / 60);
        EXPECT_EQ(tracker.size(), 0); // Pressure permits unloading before the thirty-second extension expires.
        setUnloadState(scripts, 10, 20, 30);
        EXPECT_EQ(tracker.size(), 1);
        EXPECT_EQ(internal::GetCapturedStdout(),
            "Test[unload.lua]:\tsaving\t3\t2\t1\n"
            "Test[unload.lua]:\tloaded\t3\t2\t0\n");
    }

    TEST_F(LuaScriptsContainerTest, InactiveCacheZeroThresholdDisablesRetention)
    {
        LuaUtil::ScriptTracker tracker(30, 0);
        LuaUtil::ScriptsContainer scripts(&mLua, "Test", &tracker, false);
        ASSERT_TRUE(scripts.addCustomScript(getId(unloadPath)));

        testing::internal::CaptureStdout();
        advanceCache(tracker, 600, 0);
        EXPECT_EQ(tracker.size(), 0);
        EXPECT_EQ(internal::GetCapturedStdout(), "Test[unload.lua]:\tsaving\t0\t0\t0\n");
    }

    TEST_F(LuaScriptsContainerTest, InactiveCacheDeliversEveryActivityHandlerWithoutReloading)
    {
        LuaUtil::ScriptTracker tracker(30, std::numeric_limits<std::uint64_t>::max());
        ActivityScriptsContainer scripts(&mLua, &tracker);

        testing::internal::CaptureStdout();
        EXPECT_TRUE(scripts.addCustomScript(getId(cacheActivityPath)));
        scripts.setActive(true);
        advanceCache(tracker, 20, 0.05);
        scripts.setActive(false);
        advanceCache(tracker, 400, 0.05);
        EXPECT_EQ(tracker.size(), 1);
        scripts.setActive(true);
        advanceCache(tracker, 20, 0.05);
        scripts.setActive(false);
        EXPECT_EQ(internal::GetCapturedStdout(),
            "Test[cacheactivity.lua]:\tstarted\n"
            "Test[cacheactivity.lua]:\tactive\n"
            "Test[cacheactivity.lua]:\tinactive\n"
            "Test[cacheactivity.lua]:\tactive\n"
            "Test[cacheactivity.lua]:\tinactive\n");
    }

    TEST_F(LuaScriptsContainerTest, InactiveCachePressureNeverEvictsActiveScripts)
    {
        LuaUtil::ScriptTracker tracker(30, 1);
        ActivityScriptsContainer scripts(&mLua, &tracker);

        testing::internal::CaptureStdout();
        EXPECT_TRUE(scripts.addCustomScript(getId(cacheActivityPath)));
        scripts.setActive(true);
        advanceCache(tracker, 600, 0.1);
        EXPECT_EQ(tracker.size(), 1);
        scripts.setActive(false);
        advanceCache(tracker, 600, 0.1);
        EXPECT_EQ(tracker.size(), 0);
        scripts.setActive(true);
        EXPECT_EQ(tracker.size(), 1);
        EXPECT_EQ(internal::GetCapturedStdout(),
            "Test[cacheactivity.lua]:\tstarted\n"
            "Test[cacheactivity.lua]:\tactive\n"
            "Test[cacheactivity.lua]:\tinactive\n"
            "Test[cacheactivity.lua]:\tsaved\n"
            "Test[cacheactivity.lua]:\tstarted\n"
            "Test[cacheactivity.lua]:\tloaded\n"
            "Test[cacheactivity.lua]:\tactive\n");
    }

    TEST_F(LuaScriptsContainerTest, InactiveCacheRefreshesAfterAnInactiveScriptEvent)
    {
        LuaUtil::ScriptTracker tracker(30, std::numeric_limits<std::uint64_t>::max());
        LuaUtil::ScriptsContainer scripts(&mLua, "Test", &tracker, false);
        ASSERT_TRUE(scripts.addCustomScript(getId(unloadPath)));
        setUnloadState(scripts, 3, 2, 1);

        testing::internal::CaptureStdout();
        advanceCache(tracker, 550, 0.05);
        EXPECT_EQ(tracker.size(), 1);
        setUnloadState(scripts, 10, 20, 30);
        advanceCache(tracker, 100, 0.05); // Beyond the first deadline, within thirty seconds of the event.
        EXPECT_EQ(tracker.size(), 1);
        advanceCache(tracker, 600, 0.05);
        EXPECT_EQ(tracker.size(), 0);
        EXPECT_EQ(internal::GetCapturedStdout(), "Test[unload.lua]:\tsaving\t10\t20\t30\n");
    }

    TEST_F(LuaScriptsContainerTest, InactiveCachePreservesTimerLifetimeAndSerializationRules)
    {
        using TimerType = LuaUtil::ScriptsContainer::TimerType;
        LuaUtil::ScriptTracker tracker(30, std::numeric_limits<std::uint64_t>::max());
        ActivityScriptsContainer scripts(&mLua, &tracker);
        const int scriptId = getId(emptyPath);
        ASSERT_TRUE(scripts.addCustomScript(scriptId));
        int savedCount = 0, unsavableCount = 0;
        sol::function savedCallback = sol::make_object(mLua.unsafeState(), [&](int increment) { savedCount += increment; });
        sol::function unsavableCallback = sol::make_object(mLua.unsafeState(), [&]() { ++unsavableCount; });
        auto schedule = [&](double when) {
            scripts.setupSerializableTimer(
                TimerType::SIMULATION_TIME, when, scriptId, "saved", sol::make_object(mLua.unsafeState(), 1));
            scripts.setupUnsavableTimer(TimerType::SIMULATION_TIME, when, scriptId, unsavableCallback);
        };
        scripts.registerTimerCallback(scriptId, "saved", savedCallback);
        scripts.setActive(true);
        schedule(5);
        scripts.setActive(false);
        advanceCache(tracker, 600, 1.0 / 60);
        EXPECT_EQ(tracker.size(), 1);
        EXPECT_EQ(savedCount, 0); // Cache maintenance does not run inactive scripts or their timers.
        EXPECT_EQ(unsavableCount, 0);
        scripts.setActive(true);
        scripts.processTimers(10, 10);
        EXPECT_EQ(savedCount, 1);
        EXPECT_EQ(unsavableCount, 1);

        schedule(20);
        ESM::LuaScripts data;
        scripts.save(data);
        ASSERT_EQ(data.mScripts.size(), 1);
        EXPECT_EQ(data.mScripts.front().mTimers.size(), 1);
        scripts.load(data);
        scripts.registerTimerCallback(scriptId, "saved", savedCallback);
        scripts.processTimers(30, 30);
        EXPECT_EQ(savedCount, 2);
        EXPECT_EQ(unsavableCount, 1); // Unsavable timers are still discarded by an explicit save/load.

        schedule(40);
        scripts.setActive(false);
        advanceCache(tracker, 800, 0.05);
        EXPECT_EQ(tracker.size(), 0);
        scripts.setActive(true);
        scripts.registerTimerCallback(scriptId, "saved", savedCallback);
        scripts.processTimers(50, 50);
        EXPECT_EQ(savedCount, 3);
        EXPECT_EQ(unsavableCount, 1); // Expiry uses the same serializer and discard rule.
    }

    TEST_F(LuaScriptsContainerTest, InactiveCacheIgnoresInvalidElapsedDurations)
    {
        LuaUtil::ScriptTracker tracker(30, std::numeric_limits<std::uint64_t>::max());
        LuaUtil::ScriptsContainer scripts(&mLua, "Test", &tracker, false);
        ASSERT_TRUE(scripts.addCustomScript(getId(unloadPath)));

        testing::internal::CaptureStdout();
        for (double duration : { 0.0, -1.0, std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::quiet_NaN() })
        {
            advanceCache(tracker, 600, duration);
            EXPECT_EQ(tracker.size(), 1);
        }
        advanceCache(tracker, 1, 31);
        EXPECT_EQ(tracker.size(), 0);
        EXPECT_EQ(internal::GetCapturedStdout(), "Test[unload.lua]:\tsaving\t0\t0\t0\n");
    }

    TEST_F(LuaScriptsContainerTest, InactiveCacheDoesNotRetainDestroyedContainers)
    {
        LuaUtil::ScriptTracker tracker(30, std::numeric_limits<std::uint64_t>::max());
        {
            LuaUtil::ScriptsContainer scripts(&mLua, "Test", &tracker, false);
            ASSERT_TRUE(scripts.addCustomScript(getId(unloadPath)));
            EXPECT_EQ(tracker.size(), 1);
        }
        advanceCache(tracker, 1, 0);
        EXPECT_EQ(tracker.size(), 0);
    }

    TEST_F(LuaScriptsContainerTest, LoadOrderChange)
    {
        LuaUtil::ScriptTracker tracker;
        LuaUtil::ScriptsContainer scripts1(&mLua, "Test", &tracker, false);
        LuaUtil::BasicSerializer serializer1;
        LuaUtil::BasicSerializer serializer2([](int contentFileIndex) -> int {
            if (contentFileIndex == 12)
                return 34;
            else if (contentFileIndex == 37)
                return 12;
            return contentFileIndex;
        });
        scripts1.setSerializer(&serializer1);
        scripts1.setSavedDataDeserializer(&serializer2);

        mLua.protectedCall([&](LuaUtil::LuaView& lua) {
            sol::object id1 = sol::make_object_userdata(lua.sol(), ESM::RefNum{ 42, 12 });
            sol::object id2 = sol::make_object_userdata(lua.sol(), ESM::RefNum{ 13, 37 });
            sol::table table = lua.newTable();
            table[id1] = id2;
            LuaUtil::BinaryData serialized = LuaUtil::serialize(table, &serializer1);

            EXPECT_TRUE(scripts1.addCustomScript(*mCfg.findId(customDataPath), serialized));
            EXPECT_EQ(tracker.size(), 1);
            for (int i = 0; i < 600; ++i)
                tracker.unloadInactiveScripts(lua);
            EXPECT_EQ(tracker.size(), 0);
            scripts1.receiveEvent("WakeUp", {});
            EXPECT_EQ(tracker.size(), 1);
        });

        ESM::LuaScripts data1;
        ESM::LuaScripts data2;
        scripts1.save(data1);
        scripts1.load(data1);
        scripts1.save(data2);
        EXPECT_NE(data1.mScripts[0].mData, data2.mScripts[0].mData);

        mLua.protectedCall([&](LuaUtil::LuaView& lua) {
            sol::object deserialized = LuaUtil::deserialize(lua.sol(), data2.mScripts[0].mData, &serializer1);
            EXPECT_TRUE(deserialized.is<sol::table>());
            sol::table table = deserialized;
            if (!table.empty())
            {
                const auto [key, value] = *table.cbegin();
                EXPECT_TRUE(key.is<ESM::RefNum>());
                EXPECT_TRUE(value.is<ESM::RefNum>());
                EXPECT_EQ(key.as<ESM::RefNum>(), (ESM::RefNum{ 42, 34 }));
                EXPECT_EQ(value.as<ESM::RefNum>(), (ESM::RefNum{ 13, 12 }));
                return;
            }
            EXPECT_FALSE(true);
        });
    }

    TEST_F(LuaScriptsContainerTest, ScriptOrderChange)
    {
        LuaUtil::ScriptsContainer scripts1(&mLua, "Test");
        scripts1.setAutoStartConf(mCfg.getLocalConf(ESM::REC_NPC_, ESM::RefId(), ESM::RefNum()));
        scripts1.addAutoStartedScripts();
        const int scriptId = getId(test1Path);
        EXPECT_TRUE(scripts1.addCustomScript(scriptId));

        ESM::LuaScripts data;
        scripts1.save(data);

        ESM::LuaScriptsCfg cfg;
        LuaUtil::parseOMWScripts(cfg, R"X(
CUSTOM, NPC: loadSave2.lua
NPC: loadSave1.lua
CUSTOM: test1.lua
)X");
        mCfg.init(std::move(cfg), true);

        LuaUtil::ScriptsContainer scripts2(&mLua, "Test");
        scripts2.setAutoStartConf(mCfg.getLocalConf(ESM::REC_NPC_, ESM::RefId(), ESM::RefNum()));
        scripts2.load(data);

        const int newScriptId = getId(test1Path);
        EXPECT_NE(scriptId, newScriptId);
        EXPECT_EQ(newScriptId, 2);
        EXPECT_TRUE(scripts2.hasScript(newScriptId));
    }
}
