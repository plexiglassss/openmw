#ifndef OPENMW_COMPONENTS_LUA_UI_INPUT_H
#define OPENMW_COMPONENTS_LUA_UI_INPUT_H

#include <string>
#include <components/lua/asyncpackage.hpp>

namespace MyGUI
{
    class Widget;
}

namespace LuaUi
{
    void registerUiActionHandler(std::string id, LuaUtil::Callback callback);
    void clearUiActionHandlers();
    bool dispatchUiAction(std::string_view action);
    void dispatchMouseWheel(MyGUI::Widget* widget, float left, float top, float x, float y);
}

#endif
