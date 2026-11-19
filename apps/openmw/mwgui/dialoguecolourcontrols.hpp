#ifndef MWGUI_DIALOGUECOLOURCONTROLS_H
#define MWGUI_DIALOGUECOLOURCONTROLS_H

#include <array>
#include <string_view>

namespace MyGUI
{
    class Button;
    class ComboBox;
    class EditBox;
    class ImageBox;
    class ScrollBar;
    class ScrollView;
    class TextBox;
    class Widget;
}

namespace MWGui
{
    // MyGUI owns these widgets through their parent. Keeping construction and
    // layout separate from SettingsWindow lets the native form be exercised
    // without a running game or settings callbacks.
    struct DialogueColourControls
    {
        MyGUI::ScrollView* mView = nullptr;
        MyGUI::Widget* mContent = nullptr;
        std::array<MyGUI::Button*, 4> mToggles{};
        std::array<MyGUI::EditBox*, 4> mToggleLabels{};
        MyGUI::TextBox* mRoleLabel = nullptr;
        MyGUI::ComboBox* mRole = nullptr;
        std::array<MyGUI::TextBox*, 3> mChannelLabels{};
        std::array<MyGUI::ScrollBar*, 3> mSliders{};
        std::array<MyGUI::TextBox*, 3> mValues{};
        MyGUI::ImageBox* mPreviewBackground = nullptr;
        MyGUI::EditBox* mPreview = nullptr;
        MyGUI::Button* mReset = nullptr;
        MyGUI::EditBox* mHint = nullptr;
    };

    DialogueColourControls createDialogueColourControls(MyGUI::Widget* parent, std::string_view prefix);
    void layoutDialogueColourControls(const DialogueColourControls& controls);
}

#endif
