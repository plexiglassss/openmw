#ifndef OPENMW_MWLUA_JOBBINDINGS_H
#define OPENMW_MWLUA_JOBBINDINGS_H

#include "nativequeryservice.hpp"
#include <components/lua/luastate.hpp>

namespace MWLua
{
    inline std::size_t rawArraySize(const sol::table& array)
    {
        lua_State* state = array.lua_state();
        array.push();
#if LUA_VERSION_NUM < 502
        const auto n = lua_objlen(state, -1);
#else
        const auto n = lua_rawlen(state, -1);
#endif
        lua_pop(state, 1);
        return n;
    }

    template <class T>
    struct JobTicket
    {
        std::shared_ptr<Misc::OwnedJob<T>> mState;
    };

    template <class T, class Convert>
    void bindTicket(sol::usertype<JobTicket<T>>& type, NativeQueryService& service, Convert convert)
    {
        type["cancel"] = [&service](JobTicket<T>& ticket) {
            if (ticket.mState)
                ticket.mState->cancel();
            service.mAsync.publish();
            ticket.mState.reset();
        };
        type["poll"] = [&service, convert](const JobTicket<T>& ticket, sol::this_state state)
            -> std::tuple<std::string, sol::object> {
            sol::state_view lua(state);
            if (!ticket.mState)
                return { "cancelled", sol::make_object(lua, sol::nil) };
            auto& job = *ticket.mState;
            service.mAsync.publish();
            if (job.mCancelled.load())
                return { "cancelled", sol::make_object(lua, sol::nil) };
            if (job.mConsumed)
                return { "consumed", sol::make_object(lua, sol::nil) };
            if (!job.mPublished)
                return { "pending", sol::make_object(lua, sol::nil) };
            job.mConsumed = true;
            if (job.mError)
            {
                try { std::rethrow_exception(job.mError); }
                catch (const std::exception& error) { return { "error", sol::make_object(lua, std::string(error.what())) }; }
                catch (...) { return { "error", sol::make_object(lua, std::string("worker failed")) }; }
            }
            auto value = std::move(job.mResult.value());
            job.mResult.reset();
            return { "ready", convert(lua, std::move(value)) };
        };
    }
}
#endif
