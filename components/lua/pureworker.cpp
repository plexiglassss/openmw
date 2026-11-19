#include "pureworker.hpp"

#include "luastateptr.hpp"

#ifndef NO_LUAJIT
#include <luajit.h>
#endif

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace LuaUtil
{
    namespace
    {
        constexpr std::size_t memoryLimit = 8 * 1024 * 1024;
        struct Call
        {
            lua_Alloc allocator;
            void* allocatorData;
            std::string_view source;
            const std::vector<double>& input;
            const std::atomic_bool& cancelled;
            std::size_t outputCount = 0;
            std::size_t bytes;
            int remaining = 2000;
        };

        void* allocate(void* data, void* ptr, std::size_t oldSize, std::size_t size)
        {
            auto& call = *static_cast<Call*>(data);
            const auto retained = call.bytes - std::min(call.bytes, ptr ? oldSize : 0);
            if (size > memoryLimit - retained)
                return nullptr;
            void* result = call.allocator(call.allocatorData, ptr, oldSize, size);
            if (result || size == 0)
                call.bytes = retained + size;
            return result;
        }

        void instructionHook(lua_State* state, lua_Debug*)
        {
            void* data = nullptr;
            lua_getallocf(state, &data);
            auto& call = *static_cast<Call*>(data);
            if (call.cancelled.load() || --call.remaining == 0)
                luaL_error(state, "worker cancelled or instruction budget exhausted");
        }

        void library(lua_State* state, lua_CFunction open, const char* name, const char* const* fields)
        {
            lua_pushcfunction(state, open);
            lua_pushstring(state, name);
            lua_call(state, 1, 1);
            lua_newtable(state);
            for (; *fields; ++fields)
            {
                lua_getfield(state, -2, *fields);
                lua_setfield(state, -2, *fields);
            }
            lua_setglobal(state, name);
            lua_pop(state, 1);
        }

        int run(lua_State* state)
        {
            auto& call = *static_cast<Call*>(lua_touserdata(state, 1));
            static const char* const tableFields[] = { "sort", nullptr };
            static const char* const mathFields[] = { "abs", "ceil", "floor", "huge", "max", "min", "sqrt", nullptr };
            library(state, luaopen_table, "table", tableFields);
            library(state, luaopen_math, "math", mathFields);
            if (luaL_loadbuffer(state, call.source.data(), call.source.size(), "mod worker") != 0)
                return lua_error(state);
            lua_call(state, 0, 1);
            if (!lua_isfunction(state, -1))
                return luaL_error(state, "worker chunk must return a function");
            lua_createtable(state, static_cast<int>(call.input.size()), 0);
            int index = 0;
            for (const double value : call.input)
            {
                lua_pushnumber(state, value);
                lua_rawseti(state, -2, ++index);
            }
            lua_call(state, 1, 1);
            if (lua_type(state, -1) != LUA_TTABLE)
                return luaL_error(state, "worker must return a number array");
#if LUA_VERSION_NUM < 502
            const auto count = lua_objlen(state, -1);
#else
            const auto count = lua_rawlen(state, -1);
#endif
            if (count > maxWorkerValues)
                return luaL_error(state, "worker output exceeds item limit");
            lua_pushnil(state);
            std::size_t keys = 0;
            while (lua_next(state, -2))
            {
                const auto key = lua_tonumber(state, -2);
                if (lua_type(state, -2) != LUA_TNUMBER || key < 1 || key > count || key != std::floor(key)
                    || lua_type(state, -1) != LUA_TNUMBER)
                    return luaL_error(state, "worker output must contain only a dense number array");
                ++keys;
                lua_pop(state, 1);
            }
            if (keys != count)
                return luaL_error(state, "worker output has missing array entries");
            call.outputCount = count;
            return 1;
        }
    }

    std::vector<double> runPureWorker(std::string_view source, const std::vector<double>& input,
        const std::atomic_bool& cancelled)
    {
        if (source.empty() || source.size() > maxWorkerSource || source.front() == '\x1b' || input.size() > maxWorkerValues)
            throw std::invalid_argument("invalid worker source or input size");
        if (cancelled.load())
            throw std::runtime_error("worker cancelled");
        LuaStatePtr vm(luaL_newstate());
        if (!vm)
            throw std::runtime_error("failed to create isolated Lua worker");
        lua_State* state = vm.get();
#ifndef NO_LUAJIT
        luaJIT_setmode(state, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
#endif
        void* allocatorData = nullptr;
        const auto allocator = lua_getallocf(state, &allocatorData);
        Call call{ allocator, allocatorData, source, input, cancelled, 0,
            static_cast<std::size_t>(lua_gc(state, LUA_GCCOUNT, 0)) * 1024 + 1024 };
        lua_pushcfunction(state, run);
        lua_pushlightuserdata(state, &call);
        lua_setallocf(state, allocate, &call);
        lua_sethook(state, instructionHook, LUA_MASKCOUNT, 1000);
        const int status = lua_pcall(state, 1, 1, 0);
        lua_sethook(state, nullptr, 0, 0);
        lua_setallocf(state, allocator, allocatorData);
        if (status != 0)
        {
            const char* message = lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : nullptr;
            throw std::runtime_error(message ? std::string(message).substr(0, 512) : "Lua worker failed");
        }
        std::vector<double> output(call.outputCount);
        for (std::size_t i = 0; i < output.size(); ++i)
        {
            lua_rawgeti(state, -1, static_cast<int>(i + 1));
            output[i] = lua_tonumber(state, -1);
            lua_pop(state, 1);
        }
        return output;
    }
}
