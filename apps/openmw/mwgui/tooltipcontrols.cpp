#include "tooltipcontrols.hpp"

#include <algorithm>
#include <string>

#include <MyGUI_Button.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_Widget.h>

namespace MWGui
{
    namespace
    {
        constexpr int margin = 8;
        constexpr int gap = 12;
        constexpr int minimumWidth = 320;

        int placeText(MyGUI::EditBox* text, int x, int y, int width)
        {
            text->setCoord(x, y, width, std::max(1, text->getFontHeight()));
            const int height = std::max(text->getFontHeight(), text->getTextSize().height) + 4;
            text->setSize(width, height);
            return height;
        }

        int placeToggle(const TooltipControls& controls, size_t index, int y, int width)
        {
            auto* button = controls.mToggles[index];
            const int buttonWidth = std::max(64, button->getTextSize().width + margin * 2);
            const int buttonHeight = std::max(28, button->getFontHeight() + margin);
            const int labelHeight = placeText(
                controls.mToggleLabels[index], margin, y, width - margin * 2 - buttonWidth - gap);
            button->setCoord(width - margin - buttonWidth, y, buttonWidth, buttonHeight);
            return std::max(buttonHeight, labelHeight);
        }

        int layoutRows(const TooltipControls& c, int width)
        {
            const int inner = width - margin * 2;
            int y = margin;
            y += placeText(c.mDescription, margin, y, inner) + gap;
            y += placeText(c.mSections[0], margin, y, inner) + gap;
            for (size_t i = 0; i < 5; ++i)
                y += placeToggle(c, i, y, width) + gap;
            for (size_t i = 0; i < c.mChoices.size(); ++i)
            {
                y += placeText(c.mChoiceLabels[i], margin, y, inner) + 4;
                const int height = std::max(28, c.mChoices[i]->getFontHeight() + margin);
                c.mChoices[i]->setCoord(margin, y, inner, height);
                y += height + gap;
            }
            const int valueWidth = std::max(64, c.mScaleValue->getTextSize().width + margin);
            const int labelHeight = placeText(c.mScaleLabel, margin, y, inner - valueWidth - gap);
            const int valueHeight = std::max(24, c.mScaleValue->getFontHeight() + 4);
            c.mScaleValue->setCoord(width - margin - valueWidth, y, valueWidth, valueHeight);
            y += std::max(labelHeight, valueHeight) + 4;
            c.mScale->setCoord(margin, y, inner, 14);
            y += 14 + gap * 2;
            y += placeText(c.mSections[1], margin, y, inner) + gap;
            y += placeToggle(c, 5, y, width) + gap * 2;
            y += placeText(c.mSections[2], margin, y, inner) + gap;
            for (size_t i = 6; i < c.mToggles.size(); ++i)
                y += placeToggle(c, i, y, width) + gap;
            const int height = std::max(28, c.mReset->getFontHeight() + margin);
            c.mReset->setCoord(margin, y, std::max(180, c.mReset->getTextSize().width + margin * 2), height);
            y += height + gap;
            y += placeText(c.mHint, margin, y, inner);
            return y + margin;
        }
    }

    TooltipControls createTooltipControls(MyGUI::Widget* parent, std::string_view prefix)
    {
        TooltipControls c;
        c.mView = parent->createWidget<MyGUI::ScrollView>("MW_ScrollViewVH",
            { 0, 0, parent->getWidth(), parent->getHeight() }, MyGUI::Align::Stretch,
            std::string(prefix) + "TooltipOptionsView");
        c.mView->setCanvasAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        c.mContent = c.mView->createWidget<MyGUI::Widget>({}, { 0, 0, minimumWidth, 1 }, MyGUI::Align::Default);
        const auto text = [&](std::string_view caption) {
            auto* widget = c.mContent->createWidget<MyGUI::EditBox>("SandText", {}, MyGUI::Align::Default);
            widget->setEditStatic(true);
            widget->setEditMultiLine(true);
            widget->setEditWordWrap(true);
            widget->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
            widget->setNeedKeyFocus(false);
            widget->setNeedMouseFocus(false);
            widget->setCaptionWithReplacing(caption);
            return widget;
        };
        const auto button = [&](std::string_view caption) {
            auto* widget = c.mContent->createWidget<MyGUI::Button>("MW_Button", {}, MyGUI::Align::Default);
            widget->setCaptionWithReplacing(caption);
            return widget;
        };
        c.mDescription = text("#{OMWEngine:TooltipOptionsDescription}");
        constexpr std::array<std::string_view, 3> sections{
            "#{OMWEngine:TravelTooltipSection}", "#{OMWEngine:KeyTooltipSection}", "#{OMWEngine:ClearedDungeonSection}" };
        for (size_t i = 0; i < sections.size(); ++i)
            c.mSections[i] = text(sections[i]);
        constexpr std::array<std::string_view, 8> toggleLabels{
            "#{OMWEngine:TravelTooltipEnabled}", "#{OMWEngine:TravelTooltipOverview}",
            "#{OMWEngine:TravelTooltipGondoliers}", "#{OMWEngine:TravelTooltipFallback}",
            "#{OMWEngine:TravelTooltipWide}", "#{OMWEngine:KeyTooltipEnabled}",
            "#{OMWEngine:ClearedDungeonTooltips}", "#{OMWEngine:ClearedDungeonMapTitle}" };
        for (size_t i = 0; i < toggleLabels.size(); ++i)
        {
            c.mToggleLabels[i] = text(toggleLabels[i]);
            c.mToggles[i] = button("#{Interface:On}");
        }
        constexpr std::array<std::string_view, 5> choiceLabels{
            "#{OMWEngine:TravelTooltipPack}", "#{OMWEngine:TravelTooltipOverviewMap}",
            "#{OMWEngine:TravelTooltipColour}", "#{OMWEngine:TravelTooltipTitleFont}", "#{OMWEngine:TravelTooltipTextFont}" };
        for (size_t i = 0; i < choiceLabels.size(); ++i)
        {
            c.mChoiceLabels[i] = text(choiceLabels[i]);
            c.mChoices[i] = c.mContent->createWidget<MyGUI::ComboBox>("MW_ComboBox", {}, MyGUI::Align::Default);
            c.mChoices[i]->setComboModeDrop(true);
        }
        for (std::string_view name : { "Stuporstar", "ComradeRaven", "Vehk's Ink" })
            c.mChoices[0]->addItem(std::string(name));
        for (std::string_view caption : { "#{OMWEngine:TravelTooltipRoutes}", "#{OMWEngine:TravelTooltipVvardenfell}",
                 "#{OMWEngine:TravelTooltipBookMap}" })
            c.mChoices[1]->addItem(MyGUI::LanguageManager::getInstance().replaceTags(MyGUI::UString(caption)));
        for (std::string_view name : { "Indoril", "Velothi", "Redoran", "Telvanni", "Dres", "Hlaalu", "Argonian",
                 "Cyrodiil", "Khajiit", "Ayleid", "Reman" })
            c.mChoices[2]->addItem(std::string(name));
        for (size_t i = 3; i < 5; ++i)
            for (std::string_view caption : { "#{OMWEngine:TravelTooltipDefaultFont}", "#{OMWEngine:TravelTooltipMonoFont}",
                     "#{OMWEngine:TravelTooltipDaedricFont}" })
                c.mChoices[i]->addItem(MyGUI::LanguageManager::getInstance().replaceTags(MyGUI::UString(caption)));
        for (auto* choice : c.mChoices)
            choice->setIndexSelected(0);
        c.mChoices[0]->setIndexSelected(2);
        c.mChoices[3]->setIndexSelected(2);
        c.mScaleLabel = text("#{OMWEngine:TravelTooltipScale}");
        c.mScale = c.mContent->createWidget<MyGUI::ScrollBar>("MW_HScroll", {}, MyGUI::Align::Default);
        c.mScale->setScrollRange(151);
        c.mScale->setScrollPage(1);
        c.mScale->setScrollViewPage(1);
        c.mScale->setScrollWheelPage(1);
        c.mScale->setScrollPosition(50);
        c.mScaleValue = c.mContent->createWidget<MyGUI::TextBox>("SandText", {}, MyGUI::Align::Default);
        c.mScaleValue->setCaption("100%");
        c.mScaleValue->setTextAlign(MyGUI::Align::Right | MyGUI::Align::VCenter);
        c.mReset = button("#{OMWEngine:TooltipOptionsReset}");
        c.mHint = text("#{OMWEngine:TooltipOptionsHint}");
        layoutTooltipControls(c);
        return c;
    }

    void layoutTooltipControls(const TooltipControls& c)
    {
        if (!c.mContent)
            return;
        int minWidth = std::max(minimumWidth, c.mReset->getTextSize().width + margin * 4);
        for (const auto* button : c.mToggles)
            minWidth = std::max(minWidth, button->getTextSize().width + margin * 4 + 160);
        for (auto* choice : c.mChoices)
        {
            const MyGUI::UString caption = choice->getCaption();
            int textWidth = 0;
            for (size_t i = 0; i < choice->getItemCount(); ++i)
            {
                choice->setCaption(choice->getItemNameAt(i));
                textWidth = std::max(textWidth, choice->getTextSize().width);
            }
            choice->setCaption(caption);
            const int inset = std::max(0, choice->getWidth() - choice->getTextRegion().width);
            minWidth = std::max(minWidth, textWidth + inset + margin * 2);
        }
        const MyGUI::IntPoint offset = c.mView->getViewOffset();
        c.mView->setVisibleVScroll(false);
        c.mView->setVisibleHScroll(false);
        int width = std::max(minWidth, c.mView->getViewCoord().width);
        for (int pass = 0; pass < 3; ++pass)
        {
            c.mContent->setSize(width, c.mContent->getHeight());
            c.mContent->setSize(width, layoutRows(c, width));
            c.mView->setCanvasSize(c.mContent->getSize());
            c.mView->setVisibleVScroll(true);
            c.mView->setVisibleHScroll(true);
            const int available = std::max(minWidth, c.mView->getViewCoord().width);
            if (available == width)
                break;
            width = available;
        }
        c.mView->setCanvasSize(c.mContent->getSize());
        c.mView->setViewOffset(offset);
    }
}
