#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_GUI_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_GUI_H

#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

#include <osg/Math>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <MyGUI_Colour.h>

#include <memory>
#include <string_view>

namespace Settings
{
    struct MessageBoxPositionSanitizer final : Sanitizer<int>
    {
        int apply(const int& value) const override
        {
            // Invalid numeric values preserve the original bottom-centre position.
            return value >= 0 && value <= 8 ? value : 7;
        }
    };

    struct GUICategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<float> mScalingFactor{ mIndex, "GUI", "scaling factor", makeClampSanitizerFloat(0.5f, 8) };
        SettingValue<int> mFontSize{ mIndex, "GUI", "font size", makeClampSanitizerInt(12, 18) };
        SettingValue<int> mJournalFontSize{ mIndex, "GUI", "journal font size",
            makeEnumSanitizerInt({ 0, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 }) };
        SettingValue<int> mJournalTextAlignment{ mIndex, "GUI", "journal text alignment",
            makeEnumSanitizerInt({ 0, 1, 2, 3 }) };
        SettingValue<int> mJournalPageNumberFontSize{ mIndex, "GUI", "journal page number font size",
            makeEnumSanitizerInt({ 0, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 }) };
        SettingValue<int> mJournalButtonScale{ mIndex, "GUI", "journal button scale", makeClampSanitizerInt(50, 100) };
        SettingValue<int> mMessageBoxPosition{ mIndex, "GUI", "message box position", 7,
            std::make_unique<MessageBoxPositionSanitizer>() };
        SettingValue<int> mMessageBoxHorizontalOffset{ mIndex, "GUI", "message box horizontal offset", 0,
            makeClampSanitizerInt(-1000, 1000) };
        SettingValue<int> mMessageBoxVerticalOffset{ mIndex, "GUI", "message box vertical offset", 0,
            makeClampSanitizerInt(-1000, 1000) };
        SettingValue<bool> mDialogueColouring{ mIndex, "GUI", "dialogue colouring" };
        SettingValue<bool> mDialogueUseSystemColour{ mIndex, "GUI", "dialogue use system colour" };
        SettingValue<bool> mDialogueClassifyNarration{ mIndex, "GUI", "dialogue classify narration" };
        SettingValue<bool> mJournalColourNarration{ mIndex, "GUI", "journal colour narration" };
        SettingValue<MyGUI::Colour> mColorDialogueNormal{ mIndex, "GUI", "color dialogue normal" };
        SettingValue<MyGUI::Colour> mColorDialogueTopic{ mIndex, "GUI", "color dialogue topic" };
        SettingValue<MyGUI::Colour> mColorDialogueInventory{ mIndex, "GUI", "color dialogue inventory" };
        SettingValue<MyGUI::Colour> mColorDialogueStat{ mIndex, "GUI", "color dialogue stat" };
        SettingValue<MyGUI::Colour> mColorDialogueNarration{ mIndex, "GUI", "color dialogue narration" };
        SettingValue<MyGUI::Colour> mColorDialogueJournal{ mIndex, "GUI", "color dialogue journal" };
        SettingValue<MyGUI::Colour> mColorJournalNarration{ mIndex, "GUI", "color journal narration" };
        SettingValue<float> mMenuTransparency{ mIndex, "GUI", "menu transparency", makeClampSanitizerFloat(0, 1) };
        SettingValue<float> mTooltipDelay{ mIndex, "GUI", "tooltip delay", makeMaxSanitizerFloat(0) };
        SettingValue<bool> mEnhancedWorldTooltips{ mIndex, "GUI", "enhanced world tooltips" };
        SettingValue<bool> mKeyTooltips{ mIndex, "GUI", "key tooltips", true };
        SettingValue<bool> mClearedDungeonTooltips{ mIndex, "GUI", "cleared dungeon tooltips", true };
        SettingValue<bool> mClearedDungeonMapTitle{ mIndex, "GUI", "cleared dungeon map title", true };
        SettingValue<bool> mTravelEnabled{ mIndex, "GUI", "travel tooltips", true };
        SettingValue<bool> mTravelOverview{ mIndex, "GUI", "travel tooltip overview", true };
        SettingValue<bool> mTravelGondoliers{ mIndex, "GUI", "travel tooltip gondoliers", true };
        SettingValue<bool> mTravelFallback{ mIndex, "GUI", "travel tooltip fallback", false };
        SettingValue<bool> mTravelWide{ mIndex, "GUI", "travel tooltip wide", true };
        SettingValue<int> mTravelPack{ mIndex, "GUI", "travel tooltip pack", 2,
            makeClampSanitizerInt(0, 2) };
        SettingValue<int> mTravelOverviewMap{ mIndex, "GUI", "travel tooltip overview map", 0,
            makeClampSanitizerInt(0, 2) };
        SettingValue<int> mTravelScale{ mIndex, "GUI", "travel tooltip scale", 100,
            makeClampSanitizerInt(50, 200) };
        SettingValue<int> mTravelColour{ mIndex, "GUI", "travel tooltip colour", 0,
            makeClampSanitizerInt(0, 10) };
        SettingValue<int> mTravelTitleFont{ mIndex, "GUI", "travel tooltip title font", 2,
            makeClampSanitizerInt(0, 2) };
        SettingValue<int> mTravelTextFont{ mIndex, "GUI", "travel tooltip text font", 0,
            makeClampSanitizerInt(0, 2) };
        SettingValue<bool> mWindowSnapping{ mIndex, "GUI", "window snapping" };
        SettingValue<bool> mStretchMenuBackground{ mIndex, "GUI", "stretch menu background" };
        SettingValue<bool> mControllerMenus{ mIndex, "GUI", "controller menus" };
        SettingValue<int> mControllerTriggerPressThreshold{ mIndex, "GUI", "controller trigger press",
            makeClampSanitizerInt(1, 32767) };
        SettingValue<int> mControllerTriggerReleaseThreshold{ mIndex, "GUI", "controller trigger release",
            makeClampSanitizerInt(0, 32766) };
        SettingValue<bool> mControllerTooltips{ mIndex, "GUI", "controller tooltips" };
        SettingValue<bool> mSubtitles{ mIndex, "GUI", "subtitles" };
        SettingValue<bool> mHitFader{ mIndex, "GUI", "hit fader" };
        SettingValue<bool> mWerewolfOverlay{ mIndex, "GUI", "werewolf overlay" };
        SettingValue<MyGUI::Colour> mColorBackgroundOwned{ mIndex, "GUI", "color background owned" };
        SettingValue<MyGUI::Colour> mColorCrosshairOwned{ mIndex, "GUI", "color crosshair owned" };
        SettingValue<bool> mKeyboardNavigation{ mIndex, "GUI", "keyboard navigation" };
        SettingValue<bool> mColorTopicEnable{ mIndex, "GUI", "color topic enable" };
        SettingValue<MyGUI::Colour> mColorTopicSpecific{ mIndex, "GUI", "color topic specific" };
        SettingValue<MyGUI::Colour> mColorTopicSpecificOver{ mIndex, "GUI", "color topic specific over" };
        SettingValue<MyGUI::Colour> mColorTopicSpecificPressed{ mIndex, "GUI", "color topic specific pressed" };
        SettingValue<MyGUI::Colour> mColorTopicExhausted{ mIndex, "GUI", "color topic exhausted" };
        SettingValue<MyGUI::Colour> mColorTopicExhaustedOver{ mIndex, "GUI", "color topic exhausted over" };
        SettingValue<MyGUI::Colour> mColorTopicExhaustedPressed{ mIndex, "GUI", "color topic exhausted pressed" };
    };
}

#endif
