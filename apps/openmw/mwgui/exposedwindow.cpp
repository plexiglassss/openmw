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
        const auto size = getSize();
        if (size == mLastSnapSize)
            setPosition(Gui::snapWindow(this, getCoord()));
        mLastSnapSize = size;
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
