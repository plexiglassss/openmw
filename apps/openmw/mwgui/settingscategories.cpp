#include "settingscategories.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include <MyGUI_ComboBox.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TabControl.h>
#include <MyGUI_TabItem.h>
#include <MyGUI_Widget.h>
#include <MyGUI_Window.h>

namespace MWGui
{
    namespace
    {
        MyGUI::UString translated(std::string_view text)
        {
            return MyGUI::LanguageManager::getInstance().replaceTags(MyGUI::UString(text));
        }

        std::vector<MyGUI::Widget*> childrenOf(MyGUI::Widget* parent)
        {
            std::vector<MyGUI::Widget*> result;
            for (std::size_t i = 0; i < parent->getChildCount(); ++i)
                result.push_back(parent->getChildAt(i));
            return result;
        }

        void scrollSection(MyGUI::Widget* sender, int delta)
        {
            for (auto* parent = sender->getParent(); parent; parent = parent->getParent())
                if (auto* view = parent->castType<MyGUI::ScrollView>(false))
                {
                    view->getClientWidget()->_riseMouseWheel(delta);
                    return;
                }
        }

        void revealSectionControl(MyGUI::Widget* sender, MyGUI::Widget*)
        {
            for (auto* parent = sender->getParent(); parent; parent = parent->getParent())
                if (auto* view = parent->castType<MyGUI::ScrollView>(false))
                {
                    const auto offset = view->getViewOffset();
                    const auto position
                        = sender->getAbsolutePosition() - view->getClientWidget()->getAbsolutePosition() + offset;
                    const auto viewport = view->getViewCoord();
                    const auto adjust = [](int start, int size, int available) {
                        if (start < 4 || size > available - 8)
                            return 4 - start;
                        if (start + size > available - 4)
                            return available - 4 - start - size;
                        return 0;
                    };
                    view->setViewOffset({ offset.left + adjust(position.left, sender->getWidth(), viewport.width),
                        offset.top + adjust(position.top, sender->getHeight(), viewport.height) });
                    return;
                }
        }

        void bindScrolling(MyGUI::Widget* widget)
        {
            // Nested native/Lua views retain their own wheel and focus policies.
            if (widget->castType<MyGUI::ScrollView>(false))
                return;
            const auto* edit = widget->castType<MyGUI::EditBox>(false);
            if (!widget->castType<MyGUI::ScrollBar>(false) && !widget->castType<MyGUI::ComboBox>(false)
                && !widget->castType<MyGUI::ListBox>(false) && (!edit || edit->getEditStatic())
                && widget->eventMouseWheel.empty())
                widget->eventMouseWheel += MyGUI::newDelegate(&scrollSection);
            widget->eventKeySetFocus += MyGUI::newDelegate(&revealSectionControl);
            for (auto* child : childrenOf(widget))
                bindScrolling(child);
        }

        bool isWithin(MyGUI::Widget* widget, MyGUI::Widget* ancestor)
        {
            for (; widget; widget = widget->getParent())
                if (widget == ancestor)
                    return true;
            return false;
        }
    }

    void applySettingsWindowMinimumSize(MyGUI::Window* window, MyGUI::TabControl* tabs)
    {
        int tabBarWidth = 0;
        for (std::size_t i = 0; i < tabs->getItemCount(); ++i)
            if (tabs->getItemAt(i)->getUserString("AllowTabOverflow") != "true")
                tabBarWidth += tabs->getButtonWidthAt(i);
        auto minimum = window->getMinSize();
        minimum.width = std::max(minimum.width, tabBarWidth + window->getWidth() - tabs->getWidth());
        window->setMinSize(minimum);
        // MyGUI applies MinSize on setSize/setCoord, not on setMinSize. Do this
        // before centering even when the layout's existing minimum is sufficient.
        window->setSize(window->getSize());
    }

    SettingsCategories::SettingsCategories(MyGUI::TabControl* tabs)
    {
        // Only pages created by our extensions opt in. Stock and replacement
        // layout pages retain their original identities, parents and geometry.
        constexpr std::array<std::pair<std::string_view, std::string_view>, 7> sections{ {
            { "Gameplay", "#{OMWEngine:Deleveling}" },
            { "Journal", "#{OMWEngine:SettingsJournal}" },
            { "Dialogue", "#{OMWEngine:DialogueColours}" },
            { "Messages", "#{OMWEngine:MessagePositionTab}" },
            { "MapIcons", "#{OMWEngine:MapIconsTab}" },
            { "SmartMap", "#{OMWEngine:SmartMapTab}" },
            { "Tooltips", "#{OMWEngine:TooltipOptionsTab}" },
        } };
        std::vector<MyGUI::TabItem*> owned;
        for (std::size_t i = 0; i < tabs->getItemCount(); ++i)
        {
            auto* tab = tabs->getItemAt(i);
            const auto& id = tab->getUserString("CustomSettingsPage");
            if (std::any_of(sections.begin(), sections.end(), [&](const auto& section) { return section.first == id; }))
                owned.push_back(tab);
        }
        if (owned.empty())
            return;

        auto* selected = tabs->getIndexSelected() == MyGUI::ITEM_NONE ? nullptr : tabs->getItemSelected();
        const bool selectedCustom = std::find(owned.begin(), owned.end(), selected) != owned.end();
        auto* tab = tabs->addItem(translated("#{OMWEngine:SettingsCustom}"));
        tab->setUserString("SettingsPage", "Custom");
        // Additional controls must not enlarge the stock window to fit the tab
        // strip. MyGUI already provides overflow navigation when it is needed.
        tab->setUserString("AllowTabOverflow", "true");
        auto* selector = tab->createWidget<MyGUI::ComboBox>("MW_ComboBox", {}, MyGUI::Align::Default);
        selector->setUserString("SettingsCategorySelector", "true");
        selector->setComboModeDrop(true);
        selector->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsCategories::onCategoryChanged);
        mPages.push_back({ tab, selector, {} });
        for (const auto& [id, caption] : sections)
            for (auto* source : owned)
                if (source->getUserString("CustomSettingsPage") == id)
                    addSection(0, std::string(id), std::string(caption), source, childrenOf(source),
                        id == "Gameplay" || id == "Journal", id == "Journal");
        for (auto* source : owned)
            tabs->removeItem(source);
        if (!mPages.front().mSections.empty())
            selector->setIndexSelected(0);
        if (selectedCustom)
            tabs->setItemSelected(tab);
        else if (selected)
            tabs->setItemSelected(selected);
        layout();
    }

    SettingsCategories::~SettingsCategories()
    {
        for (auto& page : mPages)
            page.mSelector->eventComboChangePosition
                -= MyGUI::newDelegate(this, &SettingsCategories::onCategoryChanged);
    }

    MyGUI::Widget* SettingsCategories::addSection(std::size_t index, const std::string& id, const std::string& caption,
        MyGUI::Widget* source, const std::vector<MyGUI::Widget*>& children, bool scroll, bool merge)
    {
        if (children.empty())
            return nullptr;
        auto& page = mPages.at(index);
        if (merge)
            for (std::size_t i = 0; i < page.mSections.size(); ++i)
                if (page.mSections[i].mId == id)
                {
                    auto& section = page.mSections[i];
                    const int offset = section.mMinHeight;
                    for (auto* child : children)
                    {
                        auto coord = child->getCoord();
                        coord.top += offset;
                        child->attachToWidget(section.mContent);
                        child->setCoord(coord);
                        bindScrolling(child);
                        section.mMinWidth = std::max(section.mMinWidth, coord.right() + 8);
                        section.mMinHeight = std::max(section.mMinHeight, coord.bottom() + 8);
                    }
                    return section.mContent;
                }
        auto* host = page.mTab->createWidget<MyGUI::Widget>(
            {}, { 0, 0, source->getWidth(), source->getHeight() }, MyGUI::Align::Stretch);
        host->setVisible(false);
        auto* view = scroll ? host->createWidget<MyGUI::ScrollView>("MW_ScrollViewVH",
                                  { 0, 0, host->getWidth(), host->getHeight() }, MyGUI::Align::Stretch)
                            : nullptr;
        auto* content = view ? view->createWidget<MyGUI::Widget>(
                                   {}, { 0, 0, source->getWidth(), source->getHeight() }, MyGUI::Align::Default)
                             : host;
        if (view)
            view->setCanvasAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        int bottom = 8, width = 0;
        for (auto* child : children)
        {
            auto coord = child->getCoord();
            bottom = std::max(bottom, coord.bottom() + 8);
            width = std::max(width, coord.right() + 8);
            child->attachToWidget(content);
            child->setCoord(coord);
        }
        if (view)
            bindScrolling(content);
        page.mSelector->addItem(translated(caption));
        page.mSections.push_back({ id, host, view, content, width, bottom });
        return content;
    }

    void SettingsCategories::layout()
    {
        for (auto& page : mPages)
        {
            const bool categories = page.mSections.size() > 1;
            const int header = categories ? std::max(28, page.mSelector->getFontHeight() + 12) + 12 : 0;
            page.mSelector->setVisible(categories);
            page.mSelector->setCoord(8, 4, std::max(1, page.mTab->getWidth() - 16), std::max(1, header - 12));
            for (std::size_t i = 0; i < page.mSections.size(); ++i)
            {
                auto& section = page.mSections[i];
                section.mHost->setCoord(0, header, page.mTab->getWidth(), std::max(1, page.mTab->getHeight() - header));
                section.mHost->setVisible(i == page.mSelector->getIndexSelected());
                if (section.mScroll)
                {
                    const auto client = section.mScroll->getViewCoord();
                    const int width = std::max(section.mMinWidth, client.width);
                    const int height = std::max(section.mMinHeight, client.height);
                    section.mContent->setSize(width, height);
                    section.mScroll->setCanvasSize(width, height);
                }
            }
        }
    }

    void SettingsCategories::onCategoryChanged(MyGUI::ComboBox* sender, std::size_t)
    {
        // A hidden text field must not keep receiving input after navigation.
        auto& input = MyGUI::InputManager::getInstance();
        for (const auto& page : mPages)
            if (page.mSelector == sender)
                for (const auto& section : page.mSections)
                    if (isWithin(input.getKeyFocusWidget(), section.mHost))
                        input.setKeyFocusWidget(sender);
        layout();
    }
}
