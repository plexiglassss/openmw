#include "smartmapcontrols.hpp"

#include <algorithm>
#include <string>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
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

        int layoutRows(const SmartMapControls& controls, int width)
        {
            const int inner = width - margin * 2;
            int y = margin;
            y += placeText(controls.mDescription, margin, y, inner) + gap;
            const auto toggle = [&](size_t index) {
                auto* button = controls.mToggles[index];
                const int buttonWidth = std::max(64, button->getTextSize().width + margin * 2);
                const int buttonHeight = std::max(28, button->getFontHeight() + margin);
                const int labelHeight
                    = placeText(controls.mToggleLabels[index], margin, y, inner - buttonWidth - gap);
                button->setCoord(width - margin - buttonWidth, y, buttonWidth, buttonHeight);
                y += std::max(labelHeight, buttonHeight) + gap;
            };
            const auto slider = [&](size_t index) {
                auto* value = controls.mValues[index];
                const int valueWidth = std::max(60, value->getTextSize().width + margin);
                const int labelHeight
                    = placeText(controls.mSliderLabels[index], margin, y, inner - valueWidth - gap);
                const int valueHeight = std::max(24, value->getFontHeight() + 4);
                value->setCoord(width - margin - valueWidth, y, valueWidth, valueHeight);
                y += std::max(labelHeight, valueHeight) + 4;
                controls.mSliders[index]->setCoord(margin, y, inner, 14);
                y += 14 + gap;
            };
            toggle(0);
            toggle(1);
            toggle(2);
            slider(0);
            toggle(3);
            slider(1);
            slider(2);
            toggle(4);
            const int resetHeight = std::max(28, controls.mReset->getFontHeight() + margin);
            const int resetWidth = std::max(160, controls.mReset->getTextSize().width + margin * 2);
            controls.mReset->setCoord(margin, y, resetWidth, resetHeight);
            y += resetHeight + gap;
            y += placeText(controls.mHint, margin, y, inner);
            return y + margin;
        }

        void scrollForm(MyGUI::Widget* sender, int delta)
        {
            for (auto* parent = sender->getParent(); parent; parent = parent->getParent())
            {
                if (auto* view = parent->castType<MyGUI::ScrollView>(false))
                {
                    view->getClientWidget()->_riseMouseWheel(delta);
                    return;
                }
            }
        }

        void setHelp(MyGUI::Widget* widget, std::string_view caption)
        {
            widget->setUserString("ToolTipType", "Layout");
            widget->setUserString("ToolTipLayout", "TextToolTip");
            widget->setUserString("Caption_Text", std::string(caption));
            widget->setNeedMouseFocus(true);
            if (!widget->castType<MyGUI::ScrollBar>(false))
                widget->eventMouseWheel += MyGUI::newDelegate(&scrollForm);
        }

        void revealFocusedControl(MyGUI::Widget* sender, MyGUI::Widget* /*previous*/)
        {
            // Native Tab/controller navigation changes key focus without scrolling
            // its ancestor. Keep this adjustment local to the Smart Map form.
            for (auto* parent = sender->getParent(); parent; parent = parent->getParent())
            {
                auto* view = parent->castType<MyGUI::ScrollView>(false);
                if (!view)
                    continue;
                const auto offset = view->getViewOffset();
                const auto origin = view->getClientWidget()->getAbsolutePosition() - offset;
                const auto position = sender->getAbsolutePosition() - origin;
                const auto viewport = view->getViewCoord();
                const auto adjustment = [](int start, int extent, int available) {
                    constexpr int inset = 4;
                    if (start < inset || extent > available - inset * 2)
                        return inset - start;
                    if (start + extent > available - inset)
                        return available - inset - start - extent;
                    return 0;
                };
                view->setViewOffset({ offset.left + adjustment(position.left, sender->getWidth(), viewport.width),
                    offset.top + adjustment(position.top, sender->getHeight(), viewport.height) });
                return;
            }
        }
    }

    SmartMapControls createSmartMapControls(MyGUI::Widget* parent, std::string_view prefix)
    {
        SmartMapControls controls;
        controls.mView = parent->createWidget<MyGUI::ScrollView>("MW_ScrollViewVH",
            { 0, 0, parent->getWidth(), parent->getHeight() }, MyGUI::Align::Stretch,
            std::string(prefix) + "SmartMapView");
        controls.mView->setCanvasAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        controls.mContent = controls.mView->createWidget<MyGUI::Widget>(
            {}, { 0, 0, minimumWidth, 1 }, MyGUI::Align::Default);
        controls.mContent->setNeedMouseFocus(false);
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
        controls.mDescription = text("#{OMWEngine:SmartMapDescription}");
        constexpr std::array<std::string_view, 5> labels{ "#{OMWEngine:SmartMapEnabled}",
            "#{OMWEngine:SmartMapInteriors}", "#{OMWEngine:SmartMapExteriors}", "#{OMWEngine:SmartMapHostiles}",
            "#{OMWEngine:SmartMapLocationNames}" };
        constexpr std::array<std::string_view, 5> help{ "#{OMWEngine:SmartMapEnabledHelp}",
            "#{OMWEngine:SmartMapInteriorsHelp}", "#{OMWEngine:SmartMapExteriorsHelp}",
            "#{OMWEngine:SmartMapHostilesHelp}", "#{OMWEngine:SmartMapLocationNamesHelp}" };
        for (size_t i = 0; i < labels.size(); ++i)
        {
            controls.mToggleLabels[i] = text(labels[i]);
            controls.mToggles[i] = controls.mContent->createWidget<MyGUI::Button>(
                "MW_Button", {}, MyGUI::Align::Default);
            controls.mToggles[i]->setCaptionWithReplacing("#{Interface:On}");
            controls.mToggles[i]->eventKeySetFocus += MyGUI::newDelegate(&revealFocusedControl);
            setHelp(controls.mToggleLabels[i], help[i]);
            setHelp(controls.mToggles[i], help[i]);
        }
        constexpr std::array<std::string_view, 3> sliderLabels{ "#{OMWEngine:SmartMapDoorThreshold}",
            "#{OMWEngine:SmartMapFightThreshold}", "#{OMWEngine:SmartMapHostileDistance}" };
        constexpr std::array<std::string_view, 3> sliderHelp{ "#{OMWEngine:SmartMapDoorThresholdHelp}",
            "#{OMWEngine:SmartMapFightThresholdHelp}", "#{OMWEngine:SmartMapHostileDistanceHelp}" };
        constexpr std::array<int, 3> initial{ 4, 82, 3500 };
        for (size_t i = 0; i < sliderLabels.size(); ++i)
        {
            controls.mSliderLabels[i] = text(sliderLabels[i]);
            auto* bar = controls.mContent->createWidget<MyGUI::ScrollBar>("MW_HScroll", {}, MyGUI::Align::Default);
            bar->setScrollRange(SmartMapControls::sliderMaximum[i] - SmartMapControls::sliderMinimum[i] + 1);
            bar->setScrollPage(1);
            bar->setScrollViewPage(1);
            bar->setScrollWheelPage(i == 2 ? 100 : 1);
            bar->setScrollPosition(initial[i] - SmartMapControls::sliderMinimum[i]);
            controls.mSliders[i] = bar;
            bar->eventKeySetFocus += MyGUI::newDelegate(&revealFocusedControl);
            controls.mValues[i] = controls.mContent->createWidget<MyGUI::TextBox>(
                "SandText", {}, MyGUI::Align::Default);
            controls.mValues[i]->setCaption(std::to_string(initial[i]));
            controls.mValues[i]->setTextAlign(MyGUI::Align::Right | MyGUI::Align::VCenter);
            controls.mValues[i]->setNeedMouseFocus(false);
            controls.mValues[i]->setNeedKeyFocus(false);
            setHelp(controls.mSliderLabels[i], sliderHelp[i]);
            setHelp(bar, sliderHelp[i]);
        }
        controls.mReset = controls.mContent->createWidget<MyGUI::Button>("MW_Button", {}, MyGUI::Align::Default);
        controls.mReset->setCaptionWithReplacing("#{OMWEngine:SmartMapReset}");
        controls.mReset->eventKeySetFocus += MyGUI::newDelegate(&revealFocusedControl);
        controls.mReset->eventMouseWheel += MyGUI::newDelegate(&scrollForm);
        controls.mHint = text("#{OMWEngine:SmartMapHint}");
        layoutSmartMapControls(controls);
        return controls;
    }

    void layoutSmartMapControls(const SmartMapControls& controls)
    {
        if (!controls.mContent)
            return;
        int minWidth = std::max(minimumWidth, controls.mReset->getTextSize().width + margin * 4);
        for (const auto* button : controls.mToggles)
            minWidth = std::max(minWidth, button->getTextSize().width + margin * 4 + 160);
        for (const auto* value : controls.mValues)
            minWidth = std::max(minWidth, value->getTextSize().width + margin * 4 + 160);
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
