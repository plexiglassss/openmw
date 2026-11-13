#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwlua/context.hpp"
#include "apps/openmw/mwlua/vfsbindings.hpp"

#include <components/resource/resourcesystem.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/archive.hpp>
#include <components/vfs/manager.hpp>

#include <gtest/gtest.h>

namespace
{
    class IteratorArchive : public VFS::Archive
    {
        void listResources(VFS::FileMap& files) override
        {
            files[VFS::Path::Normalized("root/needle/a")] = nullptr;
            files[VFS::Path::Normalized("root/needle/b")] = nullptr;
        }
        bool contains(VFS::Path::NormalizedView) const override { return false; }
        std::string getDescription() const override { return "iterator fixture"; }
    };

    TEST(MWLuaVfs, InterleavedSnapshotAndPrefixIteratorsSurviveCollection)
    {
        MWBase::Environment environment;
        VFS::Manager vfs;
        vfs.addArchive(std::make_unique<IteratorArchive>());
        vfs.buildIndex();
        ToUTF8::Utf8Encoder encoder{ ToUTF8::WINDOWS_1252 };
        Resource::ResourceSystem resources(&vfs, 1.0, &encoder.getStatelessEncoder());
        environment.setResourceSystem(resources);
        LuaUtil::LuaState state{ &vfs, nullptr };
        MWLua::Context context{ MWLua::Context::Global, nullptr, &state };
        sol::state_view lua(state.unsafeState());
        lua["vfs"] = MWLua::initVFSPackage(context);
        const auto run = [&](const char* script) {
            LuaUtil::LuaState::throwIfError(lua.safe_script(script, sol::script_pass_on_error));
        };
        for (int i = 0; i < 3; ++i)
        {
            run("a = vfs.pathsInDirectory('needle'); b = vfs.pathsWithPrefix('root/'); "
                "assert(a() == b()); a = nil");
            lua_gc(state.unsafeState(), LUA_GCCOLLECT, 0);
            run("assert(b() == 'root/needle/b' and b() == nil); b = nil");
            lua_gc(state.unsafeState(), LUA_GCCOLLECT, 0);
        }
        run("a = vfs.pathsInDirectory('needle')");
        vfs.reset();
        run("assert(a() == 'root/needle/a' and a() == 'root/needle/b' and a() == nil); a = nil");
        lua_gc(state.unsafeState(), LUA_GCCOLLECT, 0);
    }
}
