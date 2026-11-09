#ifndef MWGUI_TOOLTIPCONTROLS_H
#define MWGUI_TOOLTIPCONTROLS_H

#include <array>
#include <string_view>

namespace MyGUI
{
    class Button;
    class ComboBox;
    class EditBox;
    class ScrollBar;
    class ScrollView;
    class TextBox;
    class Widget;
}

namespace MWGui
{
    // Only native widgets and layout. The form has no world or save access.
    struct TooltipControls
    {
        MyGUI::ScrollView* mView = nullptr;
        MyGUI::Widget* mContent = nullptr;
        MyGUI::EditBox* mDescription = nullptr;
        std::array<MyGUI::EditBox*, 3> mSections{};
        // Travel enabled/overview/gondoliers/fallback/wide, key, cleared door/map.
        std::array<MyGUI::EditBox*, 8> mToggleLabels{};
        std::array<MyGUI::Button*, 8> mToggles{};
        // Pack, overview map, tint, title font, description font.
        std::array<MyGUI::EditBox*, 5> mChoiceLabels{};
        std::array<MyGUI::ComboBox*, 5> mChoices{};
        MyGUI::EditBox* mScaleLabel = nullptr;
        MyGUI::ScrollBar* mScale = nullptr;
        MyGUI::TextBox* mScaleValue = nullptr;
        MyGUI::Button* mReset = nullptr;
        MyGUI::EditBox* mHint = nullptr;
    };

    TooltipControls createTooltipControls(MyGUI::Widget* parent, std::string_view prefix);
    void layoutTooltipControls(const TooltipControls& controls);
}

#endif
