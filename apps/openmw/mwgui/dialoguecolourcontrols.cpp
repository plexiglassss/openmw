#include "dialoguecolourcontrols.hpp"

#include <algorithm>
#include <string>

#include <MyGUI_Button.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_UString.h>
#include <MyGUI_Widget.h>

namespace MWGui
{
    namespace
    {
        constexpr int margin = 8;
        constexpr int gap = 12;
        constexpr int minimumWidth = 364;

        MyGUI::UString translated(std::string_view text)
        {
            return MyGUI::LanguageManager::getInstance().replaceTags(MyGUI::UString(text));
        }

        void configureWrappedText(MyGUI::EditBox* text)
        {
            text->setEditStatic(true);
            text->setEditMultiLine(true);
            text->setEditWordWrap(true);
            text->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
            text->setNeedKeyFocus(false);
            text->setNeedMouseFocus(false);
        }

        int placeWrappedText(MyGUI::EditBox* text, int left, int top, int width)
        {
            // Set the wrapping width before measuring. The measurement is the
            // complete text height, including lines outside the initial box.
            text->setCoord(left, top, width, std::max(1, text->getFontHeight()));
            const int height = std::max(text->getFontHeight(), text->getTextSize().height) + 4;
            text->setSize(width, height);
            return height;
        }

        int layoutRows(const DialogueColourControls& controls, int width)
        {
            const int innerWidth = width - margin * 2;
            int top = margin;
            int toggleWidth = 64;
            for (const auto* button : controls.mToggles)
                toggleWidth = std::max(toggleWidth, button->getTextSize().width + margin * 2);

            for (size_t i = 0; i < controls.mToggles.size(); ++i)
            {
                auto* button = controls.mToggles[i];
                const int height = std::max(28, button->getFontHeight() + margin);
                button->setCoord(margin, top, toggleWidth, height);
                const int labelLeft = margin + toggleWidth + gap;
                const int labelHeight = placeWrappedText(
                    controls.mToggleLabels[i], labelLeft, top, width - margin - labelLeft);
                top += std::max(height, labelHeight) + gap;
            }

            top += margin;
            const int roleLabelHeight = std::max(24, controls.mRoleLabel->getFontHeight() + 4);
            controls.mRoleLabel->setCoord(margin, top, innerWidth, roleLabelHeight);
            top += roleLabelHeight + 4;
            const int roleHeight = std::max(28, controls.mRole->getFontHeight() + margin);
            controls.mRole->setCoord(margin, top, innerWidth, roleHeight);
            top += roleHeight + gap;

            int channelWidth = 50;
            int valueWidth = 44;
            for (size_t i = 0; i < controls.mSliders.size(); ++i)
            {
                channelWidth = std::max(channelWidth, controls.mChannelLabels[i]->getTextSize().width);
                // Reserve three full-height glyphs so the track does not move
                // when a channel changes between one, two and three digits.
                valueWidth = std::max(valueWidth, controls.mValues[i]->getFontHeight() * 3);
            }
            const int sliderLeft = margin + channelWidth + gap;
            const int valueLeft = width - margin - valueWidth;
            const int sliderWidth = valueLeft - gap - sliderLeft;
            for (size_t i = 0; i < controls.mSliders.size(); ++i)
            {
                const int height = std::max(
                    { 28, controls.mChannelLabels[i]->getFontHeight() + 4, controls.mValues[i]->getFontHeight() + 4 });
                controls.mChannelLabels[i]->setCoord(margin, top, channelWidth, height);
                controls.mSliders[i]->setCoord(sliderLeft, top + (height - 14) / 2, sliderWidth, 14);
                controls.mValues[i]->setCoord(valueLeft, top, valueWidth, height);
                top += height + 4;
            }

            top += gap;
            controls.mPreviewBackground->setCoord(margin, top, innerWidth, 1);
            const int previewHeight = placeWrappedText(controls.mPreview, margin, margin, innerWidth - margin * 2);
            const int backgroundHeight = std::max(60, previewHeight + margin * 2);
            controls.mPreviewBackground->setSize(innerWidth, backgroundHeight);
            top += backgroundHeight + gap;

            const int resetHeight = std::max(28, controls.mReset->getFontHeight() + margin);
            controls.mReset->setCoord(margin, top,
                std::max(196, controls.mReset->getTextSize().width + margin * 2), resetHeight);
            top += resetHeight + gap;
            top += placeWrappedText(controls.mHint, margin, top, innerWidth);
            return top + margin;
        }
    }

    DialogueColourControls createDialogueColourControls(MyGUI::Widget* parent, std::string_view prefix)
    {
        DialogueColourControls controls;
        controls.mView = parent->createWidget<MyGUI::ScrollView>("MW_ScrollViewVH",
            { 0, 0, parent->getWidth(), parent->getHeight() }, MyGUI::Align::Stretch,
            std::string(prefix) + "DialogueColourView");
        controls.mView->setCanvasAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        controls.mContent = controls.mView->createWidget<MyGUI::Widget>(
            {}, { 0, 0, minimumWidth, 1 }, MyGUI::Align::Default);

        // HStretch without Top means VCenter in MyGUI. Row positions must stay
        // anchored to the top when the scroll canvas is sized or resized.
        const MyGUI::Align rowAlign = MyGUI::Align::HStretch | MyGUI::Align::Top;
        constexpr std::array<std::string_view, 4> toggles{ "#{OMWEngine:DialogueColouring}",
            "#{OMWEngine:DialogueUseSystemColour}", "#{OMWEngine:DialogueClassifyNarration}",
            "#{OMWEngine:JournalColourNarration}" };
        for (size_t i = 0; i < controls.mToggles.size(); ++i)
        {
            controls.mToggles[i] = controls.mContent->createWidget<MyGUI::Button>(
                "MW_Button", {}, MyGUI::Align::Default);
            auto* label = controls.mContent->createWidget<MyGUI::EditBox>("SandText", {}, rowAlign);
            configureWrappedText(label);
            label->setNeedMouseFocus(true);
            label->setCaptionWithReplacing(toggles[i]);
            controls.mToggleLabels[i] = label;
            if (i != 0)
            {
                const std::string hint(i == 1 ? "#{OMWEngine:DialogueUseSystemColourHint}"
                                             : "#{OMWEngine:DialogueNarrationHint}");
                for (MyGUI::Widget* widget : std::array<MyGUI::Widget*, 2>{ controls.mToggles[i], label })
                {
                    widget->setUserString("ToolTipType", "Layout");
                    widget->setUserString("ToolTipLayout", "TextToolTip");
                    widget->setUserString("Caption_Text", hint);
                }
            }
        }

        controls.mRoleLabel = controls.mContent->createWidget<MyGUI::TextBox>("SandText", {}, rowAlign);
        controls.mRoleLabel->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
        controls.mRoleLabel->setCaptionWithReplacing("#{OMWEngine:DialogueColourRole}");
        controls.mRole = controls.mContent->createWidget<MyGUI::ComboBox>(
            "MW_ComboBox", {}, rowAlign, std::string(prefix) + "DialogueColourRole");
        controls.mRole->setComboModeDrop(true);
        for (std::string_view caption : { "#{OMWEngine:DialogueColourNormal}", "#{OMWEngine:DialogueColourInventory}",
                 "#{OMWEngine:DialogueColourStat}", "#{OMWEngine:DialogueColourNarration}",
                 "#{OMWEngine:DialogueColourJournal}", "#{OMWEngine:DialogueColourJournalNarration}",
                 "#{OMWEngine:DialogueColourTopic}" })
            controls.mRole->addItem(translated(caption));
        controls.mRole->setIndexSelected(0);

        constexpr std::array<std::string_view, 3> channelNames{
            "#{OMWEngine:DialogueColourRed}", "#{OMWEngine:DialogueColourGreen}", "#{OMWEngine:DialogueColourBlue}"
        };
        for (size_t i = 0; i < controls.mSliders.size(); ++i)
        {
            auto* label = controls.mContent->createWidget<MyGUI::TextBox>("SandText", {}, MyGUI::Align::Default);
            label->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
            label->setCaptionWithReplacing(channelNames[i]);
            controls.mChannelLabels[i] = label;
            auto* slider = controls.mContent->createWidget<MyGUI::ScrollBar>("MW_HScroll", {}, rowAlign);
            slider->setScrollRange(256);
            slider->setScrollPage(1);
            slider->setScrollViewPage(1);
            slider->setScrollWheelPage(1);
            controls.mSliders[i] = slider;
            auto* value = controls.mContent->createWidget<MyGUI::TextBox>(
                "SandText", {}, MyGUI::Align::Right | MyGUI::Align::Top);
            value->setTextAlign(MyGUI::Align::Right | MyGUI::Align::VCenter);
            controls.mValues[i] = value;
        }

        controls.mPreviewBackground = controls.mContent->createWidget<MyGUI::ImageBox>("ImageBox", {}, rowAlign);
        controls.mPreviewBackground->setImageTexture("white");
        controls.mPreviewBackground->setNeedMouseFocus(false);
        controls.mPreview = controls.mPreviewBackground->createWidget<MyGUI::EditBox>("SandText", {}, rowAlign);
        configureWrappedText(controls.mPreview);

        controls.mReset = controls.mContent->createWidget<MyGUI::Button>("MW_Button", {}, MyGUI::Align::Default);
        controls.mReset->setCaptionWithReplacing("#{OMWEngine:DialogueColourReset}");
        controls.mHint = controls.mContent->createWidget<MyGUI::EditBox>("SandText", {}, rowAlign);
        configureWrappedText(controls.mHint);
        controls.mHint->setCaptionWithReplacing("#{OMWEngine:DialogueColourHint}");

        layoutDialogueColourControls(controls);
        return controls;
    }

    void layoutDialogueColourControls(const DialogueColourControls& controls)
    {
        if (!controls.mContent)
            return;

        int minWidth = std::max(minimumWidth, controls.mReset->getTextSize().width + margin * 4);
        minWidth = std::max(minWidth, controls.mRoleLabel->getTextSize().width + margin * 2);
        // Measure every choice using the combo's actual font and skin client.
        // Keep its selected item and displayed caption unchanged afterwards.
        const MyGUI::UString roleCaption = controls.mRole->getCaption();
        int roleTextWidth = 0;
        for (size_t i = 0; i < controls.mRole->getItemCount(); ++i)
        {
            controls.mRole->setCaption(controls.mRole->getItemNameAt(i));
            roleTextWidth = std::max(roleTextWidth, controls.mRole->getTextSize().width);
        }
        controls.mRole->setCaption(roleCaption);
        const int roleInset = std::max(0, controls.mRole->getWidth() - controls.mRole->getTextRegion().width);
        minWidth = std::max(minWidth, roleTextWidth + roleInset + margin * 2);
        int channelWidth = 50;
        int valueWidth = 44;
        for (size_t i = 0; i < controls.mSliders.size(); ++i)
        {
            channelWidth = std::max(channelWidth, controls.mChannelLabels[i]->getTextSize().width);
            valueWidth = std::max(valueWidth, controls.mValues[i]->getFontHeight() * 3);
        }
        minWidth = std::max(minWidth, margin * 2 + channelWidth + valueWidth + gap * 2 + 160);
        for (const auto* toggle : controls.mToggles)
            minWidth = std::max(minWidth, toggle->getTextSize().width + margin * 4 + gap + 180);

        const MyGUI::IntPoint offset = controls.mView->getViewOffset();
        // Begin from the full skin client area, then account for any scrollbar
        // introduced by the measured content. This also removes unnecessary
        // scrollbars when a narrow window is widened again.
        controls.mView->setVisibleVScroll(false);
        controls.mView->setVisibleHScroll(false);
        int width = std::max(minWidth, controls.mView->getViewCoord().width);
        for (int pass = 0; pass < 3; ++pass)
        {
            controls.mContent->setSize(width, controls.mContent->getHeight());
            const int height = layoutRows(controls, width);
            controls.mContent->setSize(width, height);
            controls.mView->setCanvasSize(width, height);
            controls.mView->setVisibleVScroll(true);
            controls.mView->setVisibleHScroll(true);
            const int clientWidth = std::max(minWidth, controls.mView->getViewCoord().width);
            if (clientWidth == width)
                break;
            width = clientWidth;
        }
        controls.mView->setViewOffset(offset);
    }
}
