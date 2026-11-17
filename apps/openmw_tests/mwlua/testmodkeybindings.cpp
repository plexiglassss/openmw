#include <gtest/gtest.h>
#include <sol/sol.hpp>
#include <filesystem>
TEST(ModKeybindings, RuntimeDiscoveryWritebackConflictSemanticsAndCaptureLifecycle)
{
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math);
    const std::filesystem::path root{ OPENMW_PROJECT_SOURCE_DIR };
    auto args = lua.create_table();
    args[1] = root.generic_string();
    lua["arg"] = args;
    auto result = lua.safe_script_file((root / "custom/unified-controls/test-bindings.lua").string(), sol::script_pass_on_error);
    if (!result.valid())
    {
        sol::error error = result;
        FAIL() << error.what();
    }
}
