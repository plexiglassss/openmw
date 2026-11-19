#include <components/lua/luastate.hpp>
#include <components/testing/util.hpp>

#include <gtest/gtest.h>

namespace
{
    constexpr VFS::Path::NormalizedView scriptPath("cache/script.lua");
    constexpr VFS::Path::NormalizedView modulePath("cache/module.lua");

    TEST(LuaScriptCache, RepeatedSandboxesKeepGlobalsLocalsAndModulesIndependent)
    {
        TestingOpenMW::VFSTestFile script(R"(
            local count = 0
            globalCount = 0
            local module = require('cache.module')
            assert(select('#', ...) == 0)
            return function()
                count = count + 1
                globalCount = globalCount + 1
                return count + globalCount, module()
            end
        )");
        TestingOpenMW::VFSTestFile module(R"(
            local name = ...
            assert(name == 'cache.module')
            local count = 0
            return function() count = count + 1; return count end
        )");
        auto vfs = TestingOpenMW::createTestVFS({ { scriptPath, &script }, { modulePath, &module } });
        LuaUtil::ScriptsConfiguration configuration;
        LuaUtil::LuaState lua(vfs.get(), &configuration);
        std::vector<sol::function> instances;
        for (int i = 0; i < 512; ++i)
            instances.push_back(lua.runInNewSandbox(VFS::Path::Normalized(scriptPath)));
        for (int round = 1; round <= 5; ++round)
            for (const auto& instance : instances)
            {
                auto result = LuaUtil::call(instance);
                EXPECT_EQ(result.get<int>(0), round * 2);
                EXPECT_EQ(result.get<int>(1), round);
            }
    }

    TEST(LuaScriptCache, VfsRevisionAndExplicitDropReplaceCodeWithoutChangingExistingClosures)
    {
        TestingOpenMW::VFSTestFile oldScript("return function() return 'old' end");
        TestingOpenMW::VFSTestFile newScript("return function() return 'new' end");
        auto vfs = TestingOpenMW::createTestVFS({ { scriptPath, &oldScript } });
        LuaUtil::ScriptsConfiguration configuration;
        LuaUtil::LuaState lua(vfs.get(), &configuration);
        sol::function old = lua.runInNewSandbox(VFS::Path::Normalized(scriptPath));
        sol::function promotedOld = lua.runInNewSandbox(VFS::Path::Normalized(scriptPath));
        vfs->addArchive(std::make_unique<TestingOpenMW::VFSTestData>(
            VFS::FileMap{ { VFS::Path::Normalized(scriptPath), &newScript } }));
        vfs->buildIndex();
        sol::function replacement = lua.runInNewSandbox(VFS::Path::Normalized(scriptPath));
        sol::function promotedReplacement = lua.runInNewSandbox(VFS::Path::Normalized(scriptPath));
        lua.dropScriptCache();
        sol::function afterDrop = lua.runInNewSandbox(VFS::Path::Normalized(scriptPath));
        EXPECT_EQ(LuaUtil::call(old).get<std::string>(), "old");
        EXPECT_EQ(LuaUtil::call(promotedOld).get<std::string>(), "old");
        EXPECT_EQ(LuaUtil::call(replacement).get<std::string>(), "new");
        EXPECT_EQ(LuaUtil::call(promotedReplacement).get<std::string>(), "new");
        EXPECT_EQ(LuaUtil::call(afterDrop).get<std::string>(), "new");
    }

    TEST(LuaScriptCache, InvalidStandaloneSourceCannotExecuteOutsideTheSandbox)
    {
        TestingOpenMW::VFSTestFile script("end; escapedFromFactory = true; return function(...)");
        auto vfs = TestingOpenMW::createTestVFS({ { scriptPath, &script } });
        LuaUtil::ScriptsConfiguration configuration;
        LuaUtil::LuaState lua(vfs.get(), &configuration);
        EXPECT_THROW(lua.runInNewSandbox(VFS::Path::Normalized(scriptPath)), std::runtime_error);
        const sol::object escaped = sol::state_view(lua.unsafeState())["escapedFromFactory"];
        EXPECT_EQ(escaped, sol::nil);
    }

    TEST(LuaScriptCache, TrailingCommentsAndEmptyChunksWorkOnCacheHits)
    {
        for (const std::string source : { "", "-- empty", "return 42 -- trailing comment" })
        {
            TestingOpenMW::VFSTestFile script(source);
            auto vfs = TestingOpenMW::createTestVFS({ { scriptPath, &script } });
            LuaUtil::ScriptsConfiguration configuration;
            LuaUtil::LuaState lua(vfs.get(), &configuration);
            for (int pass = 0; pass < 2; ++pass)
            {
                auto result = lua.runInNewSandbox(VFS::Path::Normalized(scriptPath));
                if (source.starts_with("return"))
                    EXPECT_EQ(result.get<int>(), 42);
                else
                    EXPECT_EQ(result.return_count(), 0);
            }
        }
    }

#ifndef NO_LUAJIT
    TEST(LuaScriptCache, LuaJitSourceDirectivesKeepOriginalErrorLocations)
    {
        TestingOpenMW::VFSTestFile script("# 200 \"original.lua\"\nerror('location')");
        auto vfs = TestingOpenMW::createTestVFS({ { scriptPath, &script } });
        LuaUtil::ScriptsConfiguration configuration;
        LuaUtil::LuaState lua(vfs.get(), &configuration);
        std::string original;
        try
        {
            LuaUtil::call(lua.loadFromVFS(VFS::Path::Normalized(scriptPath)));
        }
        catch (const std::runtime_error& error)
        {
            original = error.what();
            original = original.substr(0, original.find('\n'));
        }
        ASSERT_FALSE(original.empty());
        for (int pass = 0; pass < 2; ++pass)
        {
            try
            {
                lua.runInNewSandbox(VFS::Path::Normalized(scriptPath));
                FAIL() << "Expected the script error";
            }
            catch (const std::runtime_error& error)
            {
                EXPECT_TRUE(std::string_view(error.what()).starts_with(original)) << error.what();
            }
        }
    }

    TEST(LuaScriptCache, LuaJitSourcePreamblesPreserveRuntimeErrorLineNumbers)
    {
        for (const std::string prefix : { "", "\xEF\xBB\xBF", "#!/lua\n", "\xEF\xBB\xBF#!/lua\r\n" })
        {
            TestingOpenMW::VFSTestFile script(prefix + "local x = 1\nerror('line check')");
            auto vfs = TestingOpenMW::createTestVFS({ { scriptPath, &script } });
            LuaUtil::ScriptsConfiguration configuration;
            LuaUtil::LuaState lua(vfs.get(), &configuration);
            const std::string expected = prefix.find('\n') == std::string::npos ? ":2:" : ":3:";
            for (int pass = 0; pass < 2; ++pass)
            {
                try
                {
                    lua.runInNewSandbox(VFS::Path::Normalized(scriptPath));
                    FAIL() << "Expected the script error";
                }
                catch (const std::runtime_error& error)
                {
                    EXPECT_NE(std::string(error.what()).find(expected), std::string::npos) << error.what();
                }
            }
        }
    }

    TEST(LuaScriptCache, LuaJitNestingLimitKeepsTheBytecodeFallback)
    {
        LuaUtil::ScriptsConfiguration configuration;
        LuaUtil::LuaState probe(nullptr, &configuration);
        std::string lastValid;
        for (int depth = 1; depth <= 300; ++depth)
        {
            std::string source;
            for (int level = 0; level < depth; ++level)
                source += "return function() ";
            source += "return 42 ";
            for (int level = 0; level < depth; ++level)
                source += "end ";
            if (!sol::state_view(probe.unsafeState()).load(source).valid())
                break;
            lastValid = std::move(source);
        }
        ASSERT_FALSE(lastValid.empty());
        ASSERT_FALSE(sol::state_view(probe.unsafeState()).load("return function(...) " + lastValid + " end").valid());
        TestingOpenMW::VFSTestFile script(lastValid);
        auto vfs = TestingOpenMW::createTestVFS({ { scriptPath, &script } });
        LuaUtil::LuaState lua(vfs.get(), &configuration);
        for (int pass = 0; pass < 2; ++pass)
            EXPECT_EQ(lua.runInNewSandbox(VFS::Path::Normalized(scriptPath)).get<sol::object>().get_type(),
                sol::type::function);
    }
#endif
}
