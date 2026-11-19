#include "input.hpp"

#include "widget.hpp"

namespace LuaUi
{
    namespace { std::map<std::string, LuaUtil::Callback> actionHandlers; }

    void registerUiActionHandler(std::string id, LuaUtil::Callback callback)
    {
        actionHandlers.insert_or_assign(std::move(id), std::move(callback));
    }
    void clearUiActionHandlers() { actionHandlers.clear(); }
    bool dispatchUiAction(std::string_view action)
    {
        // UI callbacks run synchronously, just like mouseClick. Claim one physical press
        // before MyGUI text/Return processing or player triggers can see it.
        for (const auto& [id, callback] : actionHandlers)
        {
            try
            {
                const auto result = callback.call(action);
                if (result.is<bool>() && result.as<bool>())
                    return true;
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "UI action " << id << " failed: " << e.what();
                return true;
            }
        }
        return false;
    }

    void dispatchMouseWheel(MyGUI::Widget* widget, float left, float top, float x, float y)
    {
        while (widget)
        {
            if (auto* ext = dynamic_cast<WidgetExtension*>(widget))
            {
                ext->mouseWheel(widget, left, top, x, y);
                break;
            }
            widget = widget->getParent();
        }
    }
}
