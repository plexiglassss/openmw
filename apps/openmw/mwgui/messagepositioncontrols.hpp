#ifndef MWGUI_MESSAGEPOSITIONCONTROLS_H
#define MWGUI_MESSAGEPOSITIONCONTROLS_H

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
    // MyGUI owns the widgets. Keeping the form separate from its callbacks lets
    // its real text wrapping, skins and scroll behaviour be tested in isolation.
    struct MessagePositionControls
    {
        MyGUI::ScrollView* mView = nullptr;
        MyGUI::Widget* mContent = nullptr;
        MyGUI::EditBox* mDescription = nullptr;
        MyGUI::EditBox* mPositionLabel = nullptr;
        MyGUI::ComboBox* mPosition = nullptr;
        std::array<MyGUI::EditBox*, 2> mOffsetLabels{};
        std::array<MyGUI::ScrollBar*, 2> mSliders{};
        std::array<MyGUI::TextBox*, 2> mValues{};
        std::array<MyGUI::EditBox*, 2> mOffsetHints{};
        MyGUI::Button* mReset = nullptr;
        MyGUI::Button* mTest = nullptr;
        MyGUI::EditBox* mHint = nullptr;
    };

    MessagePositionControls createMessagePositionControls(MyGUI::Widget* parent, std::string_view prefix);
    void layoutMessagePositionControls(const MessagePositionControls& controls);
}

#endif
