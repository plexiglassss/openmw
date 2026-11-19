#include "input.hpp"

#include "widget.hpp"
#include "adapter.hpp"

#include <MyGUI_ScrollView.h>
#include <MyGUI_EditBox.h>
#include <cmath>

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
        bool nativeEditorOwnsWheel = false;
        while (widget)
        {
            if (auto* edit = dynamic_cast<MyGUI::EditBox*>(widget))
                nativeEditorOwnsWheel = nativeEditorOwnsWheel
                    || (y != 0 && (edit->getVScrollRange() > 1 || edit->getHScrollRange() > 1));
            if (auto* ext = dynamic_cast<WidgetExtension*>(widget))
            {
                if (ext->mouseWheel(widget, left, top, x, y) || nativeEditorOwnsWheel)
                    return;
                // Native MyGUI wheel events do not bubble from Lua children.
                // Only bridge an unconsumed Lua event through an adapter. Native
                // scroll clients and scrollbars already handle the SDL wheel's
                // preceding injectMouseMove, and must not be scrolled twice.
                bool hosted = false;
                for (auto* parent = widget->getParent(); parent; parent = parent->getParent())
                {
                    hosted = hosted || dynamic_cast<LuaAdapter*>(parent) != nullptr;
                    if (auto* view = dynamic_cast<MyGUI::ScrollView*>(parent); hosted && view)
                    {
                        auto offset = view->getViewOffset();
                        offset.left -= static_cast<int>(std::round(x * 50));
                        offset.top += static_cast<int>(std::round(y * 50));
                        view->setViewOffset(offset);
                        return;
                    }
                }
                return;
            }
            widget = widget->getParent();
        }
    }
}
