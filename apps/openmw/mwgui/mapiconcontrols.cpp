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

            const auto placeSize = [&](MyGUI::EditBox* label, MyGUI::ScrollBar* bar, MyGUI::TextBox* value) {
                const int valueWidth = std::max(60, value->getTextSize().width + margin);
                const int labelHeight = placeText(label, margin, y, inner - valueWidth - gap);
                const int valueHeight = std::max(24, value->getFontHeight() + 4);
                value->setCoord(width - margin - valueWidth, y, valueWidth, valueHeight);
                y += std::max(labelHeight, valueHeight) + 4;
                bar->setCoord(margin, y, inner, 14);
                y += 14 + gap;
            };
            placeSize(controls.mSizeLabel, controls.mSize, controls.mSizeValue);
            placeSize(controls.mLocalSizeLabel, controls.mLocalSize, controls.mLocalSizeValue);
            placeSize(controls.mMaximumLabel, controls.mMaximum, controls.mMaximumValue);

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
        constexpr std::array<std::string_view, 4> labels{
            "#{OMWEngine:MapIconsEnabled}", "#{OMWEngine:MapIconsEntrances}", "#{OMWEngine:MapIconsTooltips}",
            "#{OMWEngine:MapIconsLocalDoors}"
        };
        for (size_t i = 0; i < labels.size(); ++i)
        {
            controls.mToggleLabels[i] = text(labels[i]);
            controls.mToggles[i] = button("#{Interface:On}");
        }
        const auto makeSize = [&](MyGUI::EditBox*& label, MyGUI::ScrollBar*& bar, MyGUI::TextBox*& value,
                                  std::string_view caption, int minimum, int maximum, int initial) {
            label = text(caption);
            bar = controls.mContent->createWidget<MyGUI::ScrollBar>("MW_HScroll", {}, MyGUI::Align::Default);
            bar->setScrollRange(maximum - minimum + 1);
            bar->setScrollPage(1);
            bar->setScrollViewPage(1);
            bar->setScrollWheelPage(1);
            bar->setScrollPosition(initial - minimum);
            value = controls.mContent->createWidget<MyGUI::TextBox>("SandText", {}, MyGUI::Align::Default);
            value->setCaption(std::to_string(initial));
            value->setTextAlign(MyGUI::Align::Right | MyGUI::Align::VCenter);
        };
        makeSize(controls.mSizeLabel, controls.mSize, controls.mSizeValue, "#{OMWEngine:MapIconsSize}", 16, 40, 24);
        makeSize(controls.mLocalSizeLabel, controls.mLocalSize, controls.mLocalSizeValue, "#{OMWEngine:MapIconsLocalSize}", 8, 40, 15);
        makeSize(controls.mMaximumLabel, controls.mMaximum, controls.mMaximumValue, "#{OMWEngine:MapIconsMaximum}", 8, 64, 24);
        controls.mColourLabel = text("#{OMWEngine:MapIconsColours}");
        controls.mColours = combo();
        for (std::string_view caption : { "#{OMWEngine:MapIconsVanillaColours}", "#{OMWEngine:MapIconsCategoryColours}" })
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
