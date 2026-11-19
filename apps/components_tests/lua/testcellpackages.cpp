#include <components/lua/luastate.hpp>
#include <components/testing/util.hpp>

namespace
{
    constexpr VFS::Path::NormalizedView mainPath("main.lua"), modulePath("module.lua");
    struct CellPackagesTest : testing::Test
    {
        TestingOpenMW::VFSTestFile main{ R"(
            local api, state, module = require('api'), require('state'), require('module')
            return {
                value = function() return api.value end,
                next = function() state.n = state.n + 1; return state.id, state.n end,
                module = module,
                readonly = function() api.value = 999 end
            }
        )" };
        TestingOpenMW::VFSTestFile module{ "local n = 0; return function() n = n + 1; return n end" };
        std::unique_ptr<VFS::Manager> vfs
            = TestingOpenMW::createTestVFS({ { mainPath, &main }, { modulePath, &module } });
        LuaUtil::ScriptsConfiguration configuration;
        LuaUtil::LuaState lua{ vfs.get(), &configuration };
    };

    TEST_F(CellPackagesTest, SharedBindingsKeepEnvironmentsModulesAndFactoryResultsPrivate)
    {
        lua.protectedCall([&](LuaUtil::LuaView& view) {
            auto& sol = view.sol();
            int factories = 0;
            sol.set_function("factory", [&](sol::table hidden) {
                ++factories;
                return LuaUtil::tableFromPairs<std::string, int>(sol, { { "id", hidden["id"] }, { "n", 0 } });
            });
            lua.addCommonPackage("state", sol["factory"]);
            std::map<std::string, sol::main_object> packages;
            packages["api"] = sol::main_object(
                LuaUtil::makeReadOnly(LuaUtil::tableFromPairs<std::string, int>(sol, { { "value", 7 } })));
            LuaUtil::LuaState::PreparedPackages prepared;
            auto hidden = LuaUtil::tableFromPairs<std::string, int>(sol, { { "id", 1 } });
            sol::table first
                = lua.runInNewSandbox(VFS::Path::Normalized(mainPath), "first", packages, hidden, &prepared);
            hidden["id"] = 2;
            sol::table second
                = lua.runInNewSandbox(VFS::Path::Normalized(mainPath), "second", packages, hidden, &prepared);
            EXPECT_EQ(factories, 2);
            auto a = LuaUtil::call(first["next"]);
            EXPECT_EQ(a.get<int>(0), 1);
            EXPECT_EQ(a.get<int>(1), 1);
            auto b = LuaUtil::call(second["next"]);
            EXPECT_EQ(b.get<int>(0), 2);
            EXPECT_EQ(b.get<int>(1), 1);
            EXPECT_EQ(LuaUtil::call(first["module"]).get<int>(), 1);
            EXPECT_EQ(LuaUtil::call(first["module"]).get<int>(), 2);
            EXPECT_EQ(LuaUtil::call(second["module"]).get<int>(), 1);
            EXPECT_EQ(LuaUtil::call(first["value"]).get<int>(), 7);
            EXPECT_THROW(LuaUtil::call(first["readonly"]), std::runtime_error);
            // The C++ callable must not outlive its captured counter.
            lua.addCommonPackage("state", view.newTable());
            sol["factory"] = sol::nil;
        });
    }

    TEST_F(CellPackagesTest, OverridesPreserveEagerFactoryOrderAndOldSandboxBindings)
    {
        TestingOpenMW::VFSTestFile script{ "local api = require('api'); return function() return api.value end" };
        auto source = TestingOpenMW::createTestVFS({ { mainPath, &script } });
        LuaUtil::LuaState state{ source.get(), &configuration };
        state.protectedCall([&](LuaUtil::LuaView& view) {
            auto& sol = view.sol();
            std::vector<int> order;
            sol.set_function("a", [&] {
                order.push_back(1);
                return view.newTable();
            });
            sol.set_function("z", [&] {
                order.push_back(2);
                return view.newTable();
            });
            sol.set_function("local", [&] {
                order.push_back(3);
                return view.newTable();
            });
            state.addCommonPackage("api", sol["a"]);
            state.addCommonPackage("z", sol["z"]);
            std::map<std::string, sol::main_object> packages;
            packages["api"] = sol::main_object(
                LuaUtil::makeReadOnly(LuaUtil::tableFromPairs<std::string, int>(sol, { { "value", 10 } })));
            packages["local"] = sol["local"];
            LuaUtil::LuaState::PreparedPackages prepared;
            sol::function first
                = state.runInNewSandbox(VFS::Path::Normalized(mainPath), "first", packages, sol::nil, &prepared);
            EXPECT_EQ(order, (std::vector<int>{ 1, 2, 3 }));
            packages["api"] = sol::main_object(
                LuaUtil::makeReadOnly(LuaUtil::tableFromPairs<std::string, int>(sol, { { "value", 20 } })));
            prepared = {}; // ScriptsContainer invalidates after changing its local API.
            sol::function second
                = state.runInNewSandbox(VFS::Path::Normalized(mainPath), "second", packages, sol::nil, &prepared);
            EXPECT_EQ(LuaUtil::call(first).get<int>(), 10);
            EXPECT_EQ(LuaUtil::call(second).get<int>(), 20);
            EXPECT_EQ(order, (std::vector<int>{ 1, 2, 3, 1, 2, 3 }));
            state.addCommonPackage("api", view.newTable());
            state.addCommonPackage("z", view.newTable());
            sol["a"] = sol["z"] = sol["local"] = sol::nil;
        });
    }

    TEST_F(CellPackagesTest, CommonBindingChangesRebuildSnapshotWithoutChangingExistingSandbox)
    {
        TestingOpenMW::VFSTestFile script{ "local api = require('api'); return function() return api.value end" };
        auto source = TestingOpenMW::createTestVFS({ { mainPath, &script } });
        LuaUtil::LuaState state{ source.get(), &configuration };
        state.protectedCall([&](LuaUtil::LuaView& view) {
            LuaUtil::LuaState::PreparedPackages prepared;
            auto set = [&](int value) {
                state.addCommonPackage(
                    "api", LuaUtil::tableFromPairs<std::string, int>(view.sol(), { { "value", value } }));
            };
            set(1);
            sol::function first
                = state.runInNewSandbox(VFS::Path::Normalized(mainPath), "first", {}, sol::nil, &prepared);
            set(2);
            sol::function second
                = state.runInNewSandbox(VFS::Path::Normalized(mainPath), "second", {}, sol::nil, &prepared);
            EXPECT_EQ(LuaUtil::call(first).get<int>(), 1);
            EXPECT_EQ(LuaUtil::call(second).get<int>(), 2);
        });
    }

    TEST_F(CellPackagesTest, VfsRebuildInvalidatesResolvedPathsAndBytecode)
    {
        TestingOpenMW::VFSTestFile script{ "return require('module')" }, first{ "return 1" }, second{ "return 2" };
        auto source = TestingOpenMW::createTestVFS({ { mainPath, &script }, { modulePath, &first } });
        LuaUtil::LuaState state{ source.get(), &configuration };
        EXPECT_EQ(state.runInNewSandbox(VFS::Path::Normalized(mainPath)).get<int>(), 1);
        source->reset();
        source->addArchive(std::make_unique<TestingOpenMW::VFSTestData>(VFS::FileMap{
            { VFS::Path::Normalized(mainPath), &script }, { VFS::Path::Normalized("module/init.lua"), &second } }));
        source->buildIndex();
        EXPECT_EQ(state.runInNewSandbox(VFS::Path::Normalized(mainPath)).get<int>(), 2);
        source->reset();
        source->addArchive(std::make_unique<TestingOpenMW::VFSTestData>(VFS::FileMap{
            { VFS::Path::Normalized(mainPath), &script }, { VFS::Path::Normalized("module/init.lua"), &first } }));
        source->buildIndex();
        EXPECT_EQ(state.runInNewSandbox(VFS::Path::Normalized(mainPath)).get<int>(), 1);
    }
}
