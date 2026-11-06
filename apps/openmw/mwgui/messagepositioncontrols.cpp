#include "messagepositioncontrols.hpp"

#include <algorithm>
#include <string>

#include <MyGUI_Button.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_EditBox.h>
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

        void setHint(MyGUI::Widget* widget, std::string_view caption)
        {
            widget->setUserString("ToolTipType", "Layout");
            widget->setUserString("ToolTipLayout", "TextToolTip");
            widget->setUserString("Caption_Text", std::string(caption));
        }

        int placeWrappedText(MyGUI::EditBox* text, int left, int top, int width)
        {
            text->setCoord(left, top, width, std::max(1, text->getFontHeight()));
            const int height = std::max(text->getFontHeight(), text->getTextSize().height) + 4;
            text->setSize(width, height);
            return height;
        }

        int layoutRows(const MessagePositionControls& controls, int width)
        {
            const int innerWidth = width - margin * 2;
            int top = margin;
            top += placeWrappedText(controls.mDescription, margin, top, innerWidth) + gap;
            top += placeWrappedText(controls.mPositionLabel, margin, top, innerWidth) + 4;
            const int positionHeight = std::max(28, controls.mPosition->getFontHeight() + margin);
            controls.mPosition->setCoord(margin, top, innerWidth, positionHeight);
            top += positionHeight + gap;

            for (size_t i = 0; i < controls.mSliders.size(); ++i)
            {
                // Reserve space for the signed four-digit limits so the track
                // and label do not jump when the current value changes.
                const int valueWidth = std::max(76, controls.mValues[i]->getFontHeight() * 5);
                const int valueHeight = std::max(24, controls.mValues[i]->getFontHeight() + 4);
                const int labelHeight = placeWrappedText(
                    controls.mOffsetLabels[i], margin, top, innerWidth - valueWidth - gap);
                controls.mValues[i]->setCoord(width - margin - valueWidth, top, valueWidth, valueHeight);
                top += std::max(labelHeight, valueHeight) + 4;
                controls.mSliders[i]->setCoord(margin, top, innerWidth, 14);
                top += 18;
                top += placeWrappedText(controls.mOffsetHints[i], margin, top, innerWidth) + gap;
            }

            const int resetWidth = std::max(128, controls.mReset->getTextSize().width + margin * 2);
            const int testWidth = std::max(160, controls.mTest->getTextSize().width + margin * 2);
            const int buttonHeight = std::max(
                { 28, controls.mReset->getFontHeight() + margin, controls.mTest->getFontHeight() + margin });
            controls.mReset->setCoord(margin, top, resetWidth, buttonHeight);
            if (resetWidth + gap + testWidth <= innerWidth)
                controls.mTest->setCoord(margin + resetWidth + gap, top, testWidth, buttonHeight);
            else
            {
                top += buttonHeight + gap;
                controls.mTest->setCoord(margin, top, testWidth, buttonHeight);
            }
            top += buttonHeight + gap;
            top += placeWrappedText(controls.mHint, margin, top, innerWidth);
            return top + margin;
        }
    }

    MessagePositionControls createMessagePositionControls(MyGUI::Widget* parent, std::string_view prefix)
    {
        MessagePositionControls controls;
        controls.mView = parent->createWidget<MyGUI::ScrollView>("MW_ScrollViewVH",
            { 0, 0, parent->getWidth(), parent->getHeight() }, MyGUI::Align::Stretch,
            std::string(prefix) + "MessagePositionView");
        controls.mView->setCanvasAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        controls.mContent = controls.mView->createWidget<MyGUI::Widget>(
            {}, { 0, 0, minimumWidth, 1 }, MyGUI::Align::Default);

        const MyGUI::Align rowAlign = MyGUI::Align::HStretch | MyGUI::Align::Top;
        const auto addWrappedText = [&](std::string_view caption) {
            auto* text = controls.mContent->createWidget<MyGUI::EditBox>("SandText", {}, rowAlign);
            configureWrappedText(text);
            text->setCaptionWithReplacing(caption);
            return text;
        };
        controls.mDescription = addWrappedText("#{OMWEngine:MessagePositionDescription}");
        controls.mPositionLabel = addWrappedText("#{OMWEngine:MessagePositionAnchor}");
        controls.mPosition = controls.mContent->createWidget<MyGUI::ComboBox>(
            "MW_ComboBox", {}, rowAlign, std::string(prefix) + "MessagePositionAnchor");
        controls.mPosition->setComboModeDrop(true);
        for (std::string_view caption : { "#{OMWEngine:MessagePositionTopLeft}",
                 "#{OMWEngine:MessagePositionTopCentre}", "#{OMWEngine:MessagePositionTopRight}",
                 "#{OMWEngine:MessagePositionMiddleLeft}", "#{OMWEngine:MessagePositionCentre}",
                 "#{OMWEngine:MessagePositionMiddleRight}", "#{OMWEngine:MessagePositionBottomLeft}",
                 "#{OMWEngine:MessagePositionBottomCentre}", "#{OMWEngine:MessagePositionBottomRight}" })
            controls.mPosition->addItem(translated(caption));
        controls.mPosition->setIndexSelected(7);
        setHint(controls.mPosition, "#{OMWEngine:MessagePositionAnchorHint}");

        constexpr std::array<std::string_view, 2> labels{
            "#{OMWEngine:MessagePositionHorizontalOffset}", "#{OMWEngine:MessagePositionVerticalOffset}"
        };
        constexpr std::array<std::string_view, 2> hints{
            "#{OMWEngine:MessagePositionHorizontalHint}", "#{OMWEngine:MessagePositionVerticalHint}"
        };
        for (size_t i = 0; i < controls.mSliders.size(); ++i)
        {
            controls.mOffsetLabels[i] = addWrappedText(labels[i]);
            auto* slider = controls.mContent->createWidget<MyGUI::ScrollBar>("MW_HScroll", {}, rowAlign);
            slider->setScrollRange(2001);
            slider->setScrollPage(1);
            slider->setScrollViewPage(1);
            slider->setScrollWheelPage(1);
            slider->setScrollPosition(1000);
            setHint(slider, hints[i]);
            controls.mSliders[i] = slider;
            auto* value = controls.mContent->createWidget<MyGUI::TextBox>(
                "SandText", {}, MyGUI::Align::Right | MyGUI::Align::Top);
            value->setTextAlign(MyGUI::Align::Right | MyGUI::Align::VCenter);
            value->setCaption("0");
            controls.mValues[i] = value;
            controls.mOffsetHints[i] = addWrappedText(hints[i]);
        }

        controls.mReset = controls.mContent->createWidget<MyGUI::Button>("MW_Button", {}, MyGUI::Align::Default);
        controls.mReset->setCaptionWithReplacing("#{OMWEngine:MessagePositionReset}");
        controls.mTest = controls.mContent->createWidget<MyGUI::Button>("MW_Button", {}, MyGUI::Align::Default);
        controls.mTest->setCaptionWithReplacing("#{OMWEngine:MessagePositionShowTest}");
        controls.mHint = addWrappedText("#{OMWEngine:MessagePositionHint}");

        layoutMessagePositionControls(controls);
        return controls;
    }

    void layoutMessagePositionControls(const MessagePositionControls& controls)
    {
        if (!controls.mContent)
            return;

        int minWidth = minimumWidth;
        for (const auto* button : { controls.mReset, controls.mTest })
            minWidth = std::max(minWidth, button->getTextSize().width + margin * 4);
        for (const auto* value : controls.mValues)
            minWidth = std::max(minWidth, value->getFontHeight() * 5 + gap + margin * 2 + 160);

        const MyGUI::UString caption = controls.mPosition->getCaption();
        int choiceWidth = 0;
        for (size_t i = 0; i < controls.mPosition->getItemCount(); ++i)
        {
            controls.mPosition->setCaption(controls.mPosition->getItemNameAt(i));
            choiceWidth = std::max(choiceWidth, controls.mPosition->getTextSize().width);
        }
        controls.mPosition->setCaption(caption);
        const int inset = std::max(0, controls.mPosition->getWidth() - controls.mPosition->getTextRegion().width);
        minWidth = std::max(minWidth, choiceWidth + inset + margin * 2);

        const MyGUI::IntPoint offset = controls.mView->getViewOffset();
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
