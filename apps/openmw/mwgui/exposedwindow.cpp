#include "exposedwindow.hpp"
#include <components/widgets/windowsnap.hpp>

namespace MWGui
{
    Window::Window()
    {
        Gui::registerSnapWindow(this);
        eventWindowChangeCoord += MyGUI::newDelegate(this, &Window::snapMovedWindow);
    }

    Window::~Window()
    {
        Gui::unregisterSnapWindow(this);
    }

    void Window::snapMovedWindow(MyGUI::Window*)
    {
        if (mSnapping) return;
        mSnapping = true;
        const auto r = getCoord();
        const auto action = getActionScale();
        if (action.width == 0 && action.height == 0)
            setPosition(Gui::snapWindow(this, r));
        else
        {
            int edges = 0;
            if (action.width != 0) edges |= action.left != 0 ? 1 : 2;
            if (action.height != 0) edges |= action.top != 0 ? 4 : 8;
            const auto minimum = getMinSize();
            setCoord(Gui::snapResizeWindow(this, r, edges, minimum.width, minimum.height));
        }
        mSnapping = false;
    }

    MyGUI::VectorWidgetPtr Window::getSkinWidgetsByName(const std::string& name)
    {
        return MyGUI::Widget::getSkinWidgetsByName(name);
    }

    MyGUI::Widget* Window::getSkinWidget(const std::string& name)
    {
        MyGUI::VectorWidgetPtr widgets = getSkinWidgetsByName(name);

        if (widgets.empty())
        {
            MYGUI_ASSERT(false, "widget name '" << name << "' not found in skin of layout '" << getName() << "'");
            return nullptr;
        }
        else
        {
            return widgets[0];
        }
    }
}
