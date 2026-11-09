#include "mapiconcontrols.hpp"

#include <components/mapmarkers/classification.hpp>

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

        int placeToggle(MyGUI::EditBox* label, MyGUI::Button* button, int y, int width)
        {
            const int buttonWidth = std::max(64, button->getTextSize().width + margin * 2);
            const int buttonHeight = std::max(28, button->getFontHeight() + margin);
            const int labelHeight = placeText(label, margin, y, width - margin * 2 - buttonWidth - gap);
            button->setCoord(width - margin - buttonWidth, y, buttonWidth, buttonHeight);
            return std::max(buttonHeight, labelHeight);
        }

        int layoutRows(const MapIconControls& controls, int width)
        {
            const int inner = width - margin * 2;
            int y = margin;
            y += placeText(controls.mDescription, margin, y, inner) + gap;
            for (size_t i = 0; i < controls.mToggles.size(); ++i)
                y += placeToggle(controls.mToggleLabels[i], controls.mToggles[i], y, width) + gap;

            const int valueWidth = std::max(60, controls.mSizeValue->getTextSize().width + margin);
            const int labelHeight = placeText(controls.mSizeLabel, margin, y, inner - valueWidth - gap);
            const int valueHeight = std::max(24, controls.mSizeValue->getFontHeight() + 4);
            controls.mSizeValue->setCoord(width - margin - valueWidth, y, valueWidth, valueHeight);
            y += std::max(labelHeight, valueHeight) + 4;
            controls.mSize->setCoord(margin, y, inner, 14);
            y += 14 + gap;

            y += placeText(controls.mColourLabel, margin, y, inner) + 4;
            int height = std::max(28, controls.mColours->getFontHeight() + margin);
            controls.mColours->setCoord(margin, y, inner, height);
            y += height + gap;
            y += placeText(controls.mCategoryLabel, margin, y, inner) + 4;
            height = std::max(28, controls.mCategory->getFontHeight() + margin);
            controls.mCategory->setCoord(margin, y, inner, height);
            y += height + gap;
            y += placeToggle(controls.mCategoryToggleLabel, controls.mCategoryToggle, y, width) + gap;
            height = std::max(28, controls.mReset->getFontHeight() + margin);
            const int resetWidth = std::max(160, controls.mReset->getTextSize().width + margin * 2);
            controls.mReset->setCoord(margin, y, resetWidth, height);
            y += height + gap;
            y += placeText(controls.mHint, margin, y, inner);
            return y + margin;
        }
    }

    MapIconControls createMapIconControls(MyGUI::Widget* parent, std::string_view prefix)
    {
        MapIconControls controls;
        controls.mView = parent->createWidget<MyGUI::ScrollView>("MW_ScrollViewVH",
            { 0, 0, parent->getWidth(), parent->getHeight() }, MyGUI::Align::Stretch,
            std::string(prefix) + "MapIconView");
        controls.mView->setCanvasAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        controls.mContent = controls.mView->createWidget<MyGUI::Widget>(
            {}, { 0, 0, minimumWidth, 1 }, MyGUI::Align::Default);
        const auto text = [&](std::string_view caption) {
            auto* widget = controls.mContent->createWidget<MyGUI::EditBox>("SandText", {}, MyGUI::Align::Default);
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
            auto* widget = controls.mContent->createWidget<MyGUI::Button>("MW_Button", {}, MyGUI::Align::Default);
            widget->setCaptionWithReplacing(caption);
            return widget;
        };
        const auto combo = [&]() {
            auto* widget = controls.mContent->createWidget<MyGUI::ComboBox>("MW_ComboBox", {}, MyGUI::Align::Default);
            widget->setComboModeDrop(true);
            return widget;
        };
        controls.mDescription = text("#{OMWEngine:MapIconsDescription}");
        constexpr std::array<std::string_view, 3> labels{
            "#{OMWEngine:MapIconsEnabled}", "#{OMWEngine:MapIconsEntrances}", "#{OMWEngine:MapIconsTooltips}"
        };
        for (size_t i = 0; i < labels.size(); ++i)
        {
            controls.mToggleLabels[i] = text(labels[i]);
            controls.mToggles[i] = button("#{Interface:On}");
        }
        controls.mSizeLabel = text("#{OMWEngine:MapIconsSize}");
        controls.mSize = controls.mContent->createWidget<MyGUI::ScrollBar>("MW_HScroll", {}, MyGUI::Align::Default);
        controls.mSize->setScrollRange(25);
        controls.mSize->setScrollPage(1);
        controls.mSize->setScrollViewPage(1);
        controls.mSize->setScrollWheelPage(1);
        controls.mSize->setScrollPosition(8);
        controls.mSizeValue = controls.mContent->createWidget<MyGUI::TextBox>("SandText", {}, MyGUI::Align::Default);
        controls.mSizeValue->setCaption("24");
        controls.mSizeValue->setTextAlign(MyGUI::Align::Right | MyGUI::Align::VCenter);
        controls.mColourLabel = text("#{OMWEngine:MapIconsColours}");
        controls.mColours = combo();
        for (std::string_view caption : { "#{OMWEngine:MapIconsMapColours}", "#{OMWEngine:MapIconsWhite}",
                 "#{OMWEngine:MapIconsCategoryColours}" })
            controls.mColours->addItem(MyGUI::LanguageManager::getInstance().replaceTags(MyGUI::UString(caption)));
        controls.mColours->setIndexSelected(0);
        controls.mCategoryLabel = text("#{OMWEngine:MapIconsCategory}");
        controls.mCategory = combo();
        for (size_t i = 0; i < MapMarkers::categoryCount; ++i)
            controls.mCategory->addItem(std::string(MapMarkers::definition(static_cast<MapMarkers::Category>(i)).label));
        controls.mCategory->setIndexSelected(0);
        controls.mCategoryToggleLabel = text("#{OMWEngine:MapIconsCategoryEnabled}");
        controls.mCategoryToggle = button("#{Interface:On}");
        controls.mReset = button("#{OMWEngine:MapIconsReset}");
        controls.mHint = text("#{OMWEngine:MapIconsHint}");
        layoutMapIconControls(controls);
        return controls;
    }

    void layoutMapIconControls(const MapIconControls& controls)
    {
        if (!controls.mContent)
            return;
        int minWidth = std::max(minimumWidth, controls.mReset->getTextSize().width + margin * 4);
        for (const auto* button : controls.mToggles)
            minWidth = std::max(minWidth, button->getTextSize().width + margin * 4 + 160);
        minWidth = std::max(minWidth, controls.mCategoryToggle->getTextSize().width + margin * 4 + 160);
        for (auto* choice : { controls.mColours, controls.mCategory })
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
            const int available = std::max(minWidth, controls.mView->getViewCoord().width);
            if (available == width)
                break;
            width = available;
        }
        controls.mView->setCanvasSize(controls.mContent->getSize());
        controls.mView->setViewOffset(offset);
    }
}
