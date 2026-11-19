#ifndef COMPONENTS_LUAUI_CONTENT
#define COMPONENTS_LUAUI_CONTENT

#include <sol/sol.hpp>

#include <components/lua/luastate.hpp>

namespace LuaUi
{
    bool layoutReuseInProgress();
    void invalidateLayoutReuse();
    void preserveContentLayoutReads(const sol::main_table& table);
    sol::protected_function loadContentConstructor(LuaUtil::LuaState* state);

    bool isValidContent(const sol::object& object);

    class ContentView
    {
    public:
        // accepts only Lua tables returned by ui.content
        explicit ContentView(sol::main_table table)
            : mTable(std::move(table))
        {
            // Validation itself may invoke a metatable's __index. Disable
            // reuse before that Lua code can change or remove the metatable.
            preserveContentLayoutReads(mTable);
            if (!isValidContent(mTable))
                throw std::domain_error("Invalid UI Content");
        }

        size_t size() const { return mTable.size(); }

        sol::object at(size_t index) const
        {
            if (index < size())
            {
                // Dense content reads are ordinary raw table entries. A
                // custom __index can execute Lua when an entry is absent;
                // retain every later layout read in that action in that case.
                if (layoutReuseInProgress())
                {
                    lua_State* lua = mTable.lua_state();
                    mTable.push(lua);
                    lua_rawgeti(lua, -1, static_cast<int>(toLua(index)));
                    const bool missing = lua_isnil(lua, -1);
                    lua_pop(lua, 2);
                    if (missing)
                        invalidateLayoutReuse();
                }
                return mTable.get<sol::object>(toLua(index));
            }
            else
                throw std::range_error("Invalid Content index");
        }

    private:
        sol::main_table mTable;

        static inline size_t toLua(size_t index) { return index + 1; }
    };
}

#endif // COMPONENTS_LUAUI_CONTENT
