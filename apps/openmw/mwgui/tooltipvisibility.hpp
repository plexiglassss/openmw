#ifndef OPENMW_MWGUI_TOOLTIPVISIBILITY_H
#define OPENMW_MWGUI_TOOLTIPVISIBILITY_H

namespace MWGui
{
    // Dialogue camera mods may hide the gameplay HUD while leaving a menu open.
    // Its tooltips still need their root visible; loading always suppresses them.
    constexpr bool isTooltipRootVisible(bool hudEnabled, bool guiMode, bool loading)
    {
        return !loading && (hudEnabled || guiMode);
    }
}

#endif
