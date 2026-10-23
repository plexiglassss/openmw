#ifndef OPENMW_COMPONENTS_WIDGETS_WINDOWSNAP_H
#define OPENMW_COMPONENTS_WIDGETS_WINDOWSNAP_H
#include "snaprect.hpp"
#include <MyGUI_RenderManager.h>
#include <MyGUI_Widget.h>
#include <algorithm>
#include <cmath>
#include <components/settings/values.hpp>
#include <vector>

namespace Gui
{
    // Only registered native/Lua windows participate, never tooltip/pop-up children.
    inline std::vector<MyGUI::Widget*> snapWindows;
    inline void registerSnapWindow(MyGUI::Widget* w)
    {
        snapWindows.push_back(w);
    }
    inline void unregisterSnapWindow(MyGUI::Widget* w)
    {
        std::erase(snapWindows, w);
    }

    inline MyGUI::IntPoint snapWindow(MyGUI::Widget* moving, MyGUI::IntCoord proposed)
    {
        if (!Settings::gui().mWindowSnapping)
            return proposed.point();
        std::vector<SnapRect> targets;
        for (auto* other : snapWindows)
        {
            if (other == moving || !other->getVisible())
                continue;
            const auto r = other->getAbsoluteCoord();
            targets.push_back({ r.left, r.top, r.width, r.height });
        }
        const auto screen = MyGUI::RenderManager::getInstance().getViewSize();
        // Coordinates are logical GUI pixels, so the threshold scales with UI.
        const auto result = snapRect(
            { proposed.left, proposed.top, proposed.width, proposed.height }, targets, screen.width, screen.height, 12);
        return { result.left, result.top };
    }
}
#endif
