#ifndef MWGUI_MAPICONCONTROLS_H
#define MWGUI_MAPICONCONTROLS_H

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
    // MyGUI owns these widgets. The form has no world access or update callback.
    struct MapIconControls
    {
        MyGUI::ScrollView* mView = nullptr;
        MyGUI::Widget* mContent = nullptr;
        MyGUI::EditBox* mDescription = nullptr;
        std::array<MyGUI::EditBox*, 4> mToggleLabels{};
        std::array<MyGUI::Button*, 4> mToggles{};
        MyGUI::EditBox* mSizeLabel = nullptr;
        MyGUI::ScrollBar* mSize = nullptr;
        MyGUI::TextBox* mSizeValue = nullptr;
        MyGUI::EditBox* mLocalSizeLabel = nullptr;
        MyGUI::ScrollBar* mLocalSize = nullptr;
        MyGUI::TextBox* mLocalSizeValue = nullptr;
        MyGUI::EditBox* mMaximumLabel = nullptr;
        MyGUI::ScrollBar* mMaximum = nullptr;
        MyGUI::TextBox* mMaximumValue = nullptr;
        MyGUI::EditBox* mColourLabel = nullptr;
        MyGUI::ComboBox* mColours = nullptr;
        MyGUI::EditBox* mCategoryLabel = nullptr;
        MyGUI::ComboBox* mCategory = nullptr;
        MyGUI::EditBox* mCategoryToggleLabel = nullptr;
        MyGUI::Button* mCategoryToggle = nullptr;
        MyGUI::Button* mReset = nullptr;
        MyGUI::EditBox* mHint = nullptr;
    };

    MapIconControls createMapIconControls(MyGUI::Widget* parent, std::string_view prefix);
    void layoutMapIconControls(const MapIconControls& controls);
}

#endif
