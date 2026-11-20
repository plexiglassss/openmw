#ifndef MWGUI_SMARTMAPCONTROLS_H
#define MWGUI_SMARTMAPCONTROLS_H

#include <array>
#include <string_view>

namespace MyGUI
{
    class Button;
    class EditBox;
    class ScrollBar;
    class ScrollView;
    class TextBox;
    class Widget;
}

namespace MWGui
{
    // MyGUI owns the widgets; the form has no world access or per-frame work.
    struct SmartMapControls
    {
        static constexpr std::array<int, 3> sliderMinimum{ 1, 78, 1000 };
        static constexpr std::array<int, 3> sliderMaximum{ 50, 100, 7000 };

        MyGUI::ScrollView* mView = nullptr;
        MyGUI::Widget* mContent = nullptr;
        MyGUI::EditBox* mDescription = nullptr;
        std::array<MyGUI::EditBox*, 5> mToggleLabels{};
        std::array<MyGUI::Button*, 5> mToggles{};
        std::array<MyGUI::EditBox*, 3> mSliderLabels{};
        std::array<MyGUI::ScrollBar*, 3> mSliders{};
        std::array<MyGUI::TextBox*, 3> mValues{};
        MyGUI::Button* mReset = nullptr;
        MyGUI::EditBox* mHint = nullptr;
    };

    SmartMapControls createSmartMapControls(MyGUI::Widget* parent, std::string_view prefix);
    void layoutSmartMapControls(const SmartMapControls& controls);
}

#endif
