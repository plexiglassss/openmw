#ifndef OPENMW_LUAUI_PROPERTIES
#define OPENMW_LUAUI_PROPERTIES

#include <MyGUI_Colour.h>
#include <MyGUI_Types.h>
#include <cstdlib>
#include <osg/Vec2>
#include <sol/sol.hpp>
#include <string_view>

#include <components/lua/luastate.hpp>
#include <components/misc/color.hpp>

namespace LuaUi
{
    template <typename T>
    constexpr bool isMyGuiIntVector()
    {
        return std::is_same<T, MyGUI::IntPoint>() || std::is_same<T, MyGUI::IntSize>();
    }

    template <typename T>
    constexpr bool isMyGuiVector()
    {
        return isMyGuiIntVector<T>() || std::is_same<T, MyGUI::FloatPoint>() || std::is_same<T, MyGUI::FloatSize>();
    }

    template <typename T>
    constexpr bool isMyGuiColor()
    {
        return std::is_same<T, MyGUI::Colour>();
    }

    template <typename T, typename LuaT, typename Object>
    sol::optional<T> convertPropertyValue(const Object& opt, std::string_view field, std::string_view errorPrefix)
    {
        if (opt != sol::nil && !opt.template is<LuaT>())
        {
            std::string error(errorPrefix);
            error += " \"";
            error += field;
            error += "\" has an invalid value \"";
            error += LuaUtil::toString(opt);
            error += "\"";
            throw std::logic_error(error);
        }
        if (!opt.template is<LuaT>())
            return sol::nullopt;

        LuaT luaT = opt.template as<LuaT>();
        if constexpr (isMyGuiIntVector<T>())
            return T(static_cast<int>(luaT.x()), static_cast<int>(luaT.y()));
        else if constexpr (isMyGuiVector<T>())
            return T(luaT.x(), luaT.y());
        else if constexpr (isMyGuiColor<T>())
            return T(luaT.r(), luaT.g(), luaT.b(), luaT.a());
        else
            return luaT;
    }

    template <typename T, typename LuaT>
    sol::optional<T> parseValue(const sol::object& table, std::string_view field, std::string_view errorPrefix)
    {
        const sol::object value = LuaUtil::getFieldOrNil(table, field);
        return convertPropertyValue<T, LuaT>(value, field, errorPrefix);
    }

    template <typename T>
    sol::optional<T> parseValue(const sol::object& table, std::string_view field, std::string_view errorPrefix)
    {
        if constexpr (isMyGuiVector<T>())
            return parseValue<T, osg::Vec2f>(table, field, errorPrefix);
        else if constexpr (isMyGuiColor<T>())
            return parseValue<T, Misc::Color>(table, field, errorPrefix);
        else
            return parseValue<T, T>(table, field, errorPrefix);
    }

    template <typename T>
    T parseProperty(sol::object props, sol::object templateProps, std::string_view field, const T& defaultValue)
    {
        auto propOptional = parseValue<T>(props, field, "Property");
        auto templateOptional = parseValue<T>(templateProps, field, "Template property");

        if (propOptional.has_value())
            return propOptional.value();
        else if (templateOptional.has_value())
            return templateOptional.value();
        else
            return defaultValue;
    }

    template <typename T>
    T parseExternal(sol::object external, std::string_view field, const T& defaultValue)
    {
        auto optional = parseValue<T>(external, field, "External value");

        return optional.value_or(defaultValue);
    }

    namespace Detail
    {
        inline bool useLegacyPropertyReads()
        {
            static const bool enabled = [] {
                const char* value = std::getenv("OPENMW_LUA_UI_LEGACY_LAYOUT");
                return value != nullptr && std::string_view(value) == "1";
            }();
            return enabled;
        }

        struct RestoreStack
        {
            lua_State* mState;
            int mTop;
            explicit RestoreStack(lua_State* state)
                : mState(state)
                , mTop(lua_gettop(state))
            {
            }
            ~RestoreStack() { lua_settop(mState, mTop); }
        };

        template <typename T, typename LuaT>
        sol::optional<T> parseStoredValue(
            const sol::stack_object& source, std::string_view field, std::string_view errorPrefix)
        {
            lua_State* state = source.lua_state();
            {
                RestoreStack restore(state);
                // Check at the time of this read: the previous property lookup
                // may have changed the template's metatable. Every metatable
                // retains the established protected lookup and its semantics.
                const int index = source.stack_index();
                if (lua_type(state, index) == LUA_TTABLE && lua_getmetatable(state, index) == 0)
                {
                    lua_pushlstring(state, field.data(), field.size());
                    lua_rawget(state, index);
                    const sol::stack_object value(state, -1);
                    // Registry and stack objects accept different C++ types for
                    // nil (notably pointers). Preserve the legacy nil checks so
                    // a missing property cannot suppress its template/default.
                    if (value == sol::nil)
                        return convertPropertyValue<T, LuaT>(sol::object(value), field, errorPrefix);
                    return convertPropertyValue<T, LuaT>(value, field, errorPrefix);
                }
            }
            return parseValue<T, LuaT>(source, field, errorPrefix);
        }

        template <typename T>
        sol::optional<T> parseStoredValue(
            const sol::stack_object& source, std::string_view field, std::string_view errorPrefix)
        {
            if constexpr (isMyGuiVector<T>())
                return parseStoredValue<T, osg::Vec2f>(source, field, errorPrefix);
            else if constexpr (isMyGuiColor<T>())
                return parseStoredValue<T, Misc::Color>(source, field, errorPrefix);
            else
                return parseStoredValue<T, T>(source, field, errorPrefix);
        }
    }

    template <typename T>
    T parseProperty(const sol::main_object& props, const sol::main_object& templateProps,
        std::string_view field, const T& defaultValue)
    {
        lua_State* state = props.lua_state();
        lua_State* templateState = templateProps.lua_state();
        if (state == nullptr)
            state = templateState;
        if (Detail::useLegacyPropertyReads() || state == nullptr
            || (templateState != nullptr && state != templateState) || lua_checkstack(state, 8) == 0)
            return parseProperty<T>(sol::object(props), sol::object(templateProps), field, defaultValue);

        // Preserve the legacy by-value ownership snapshot, without registry
        // references. Both owners remain rooted if __index, __tostring or GC
        // replaces a caller's stored reference during either lookup.
        Detail::RestoreStack restore(state);
        props.push(state);
        const sol::stack_object propOwner(state, lua_gettop(state));
        templateProps.push(state);
        const sol::stack_object templateOwner(state, lua_gettop(state));
        const auto prop = Detail::parseStoredValue<T>(propOwner, field, "Property");
        // Read and validate the template even when the property overrides it.
        // Its metatable may have side effects or report an invalid value.
        const auto fallback = Detail::parseStoredValue<T>(templateOwner, field, "Template property");
        return prop.has_value() ? prop.value() : fallback.value_or(defaultValue);
    }

    template <typename T>
    T parseExternal(const sol::main_object& external, std::string_view field, const T& defaultValue)
    {
        lua_State* state = external.lua_state();
        if (Detail::useLegacyPropertyReads() || state == nullptr || lua_checkstack(state, 8) == 0)
            return parseExternal<T>(sol::object(external), field, defaultValue);
        Detail::RestoreStack restore(state);
        external.push(state);
        const sol::stack_object owner(state, lua_gettop(state));
        return Detail::parseStoredValue<T>(owner, field, "External value").value_or(defaultValue);
    }
}

#endif // !OPENMW_LUAUI_PROPERTIES
