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

        bool contains(MyGUI::Widget* widget, std::string_view key, std::string_view value)
        {
            if (widget->getUserString(std::string(key)) == value)
                return true;
            for (auto* child : childrenOf(widget))
                if (contains(child, key, value))
                    return true;
            return false;
        }

        bool named(MyGUI::Widget* widget, std::string_view suffix)
        {
            if (std::string_view(widget->getName()).ends_with(suffix))
                return true;
            for (auto* child : childrenOf(widget))
                if (named(child, suffix))
                    return true;
            return false;
        }

        std::string pageId(MyGUI::TabItem* page)
        {
            if (const auto& id = page->getUserString("SettingsPage"); !id.empty())
                return std::string(id);
            // Older/custom layouts have no page IDs. Never classify translated
            // captions or tab indices: use their existing controls instead.
            if (contains(page, "SettingName", "difficulty"))
                return "Preferences";
            if (contains(page, "SettingName", "delevel creatures"))
                return "Gameplay";
            if (contains(page, "SettingCategory", "Sound"))
                return "Audio";
            if (named(page, "ControlsBox"))
                return "Controls";
            if (named(page, "ResolutionList"))
                return "Video";
            if (named(page, "ScriptList"))
                return "Mods";
            if (named(page, "PrimaryLanguage"))
                return "Language";
            if (named(page, "JournalFontSize") || named(page, "JournalTextAlignment")
                || named(page, "JournalPageNumberFontSize") || named(page, "JournalButtonScale")
                || contains(page, "SettingName", "enhanced world tooltips")
                || contains(page, "SettingName", "window snapping"))
                return "Journal";
            return {};
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

    SettingsCategories::SettingsCategories(MyGUI::TabControl* tabs)
    {
        std::vector<MyGUI::TabItem*> old;
        for (std::size_t i = 0; i < tabs->getItemCount(); ++i)
            old.push_back(tabs->getItemAt(i));
        constexpr std::array<std::string_view, 6> ids{ "Gameplay", "Interface", "Controls", "Video", "Audio", "Mods" };
        for (auto id : ids)
        {
            auto* tab = tabs->addItem(translated("#{OMWEngine:Settings" + std::string(id) + "}"));
            tab->setUserString("SettingsPage", std::string(id));
            auto* selector = tab->createWidget<MyGUI::ComboBox>("MW_ComboBox", {}, MyGUI::Align::Default);
            selector->setUserString("SettingsCategorySelector", "true");
            selector->setComboModeDrop(true);
            selector->eventComboChangePosition += MyGUI::newDelegate(this, &SettingsCategories::onCategoryChanged);
            mPages.push_back({ tab, selector, {} });
        }

        for (auto* tab : old)
        {
            const auto id = pageId(tab);
            const auto children = childrenOf(tab);
            if (id == "Preferences")
            {
                std::vector<MyGUI::Widget*> game, interface;
                for (auto* child : children)
                    (contains(child, "SettingCategory", "Game") || contains(child, "SettingCategory", "Saves")
                            ? game
                            : interface)
                        .push_back(child);
                addSection(0, "#{OMWEngine:SettingsGeneral}", tab, game, true, true);
                addSection(1, "#{OMWEngine:SettingsHud}", tab, interface, true, true);
            }
            else if (id == "Gameplay")
                addSection(0, "#{OMWEngine:Deleveling}", tab, children, true);
            else if (id == "Controls" || id == "Video" || id == "Audio" || id == "Mods")
            {
                const auto index = std::find(ids.begin(), ids.end(), id) - ids.begin();
                addSection(index, "#{OMWEngine:Settings" + id + "}", tab, children, false);
            }
            else
            {
                const std::array<std::pair<std::string_view, std::string_view>, 7> sections{ {
                    { "Journal", "#{OMWEngine:SettingsJournal}" },
                    { "Language", "#{OMWEngine:Language}" },
                    { "Dialogue", "#{OMWEngine:DialogueColours}" },
                    { "Messages", "#{OMWEngine:MessagePositionTab}" },
                    { "MapIcons", "#{OMWEngine:MapIconsTab}" },
                    { "SmartMap", "#{OMWEngine:SmartMapTab}" },
                    { "Tooltips", "#{OMWEngine:TooltipOptionsTab}" },
                } };
                const auto section = std::find_if(
                    sections.begin(), sections.end(), [&](const auto& value) { return value.first == id; });
                if (section != sections.end())
                    addSection(1, std::string(section->second), tab, children, id == "Journal" || id == "Language",
                        false, id == "Journal");
                else
                    // Keep unknown third-party layout pages accessible under Mods.
                    addSection(5, tab->getCaption().asUTF8(), tab, children, true);
            }
        }
        for (auto* tab : old)
            tabs->removeItem(tab);
        for (auto& page : mPages)
            if (!page.mSections.empty())
                page.mSelector->setIndexSelected(0);
        tabs->setIndexSelected(0);
        layout();
    }

    SettingsCategories::~SettingsCategories()
    {
        for (auto& page : mPages)
            page.mSelector->eventComboChangePosition
                -= MyGUI::newDelegate(this, &SettingsCategories::onCategoryChanged);
    }

    MyGUI::Widget* SettingsCategories::addSection(std::size_t index, const std::string& caption, MyGUI::Widget* source,
        const std::vector<MyGUI::Widget*>& children, bool scroll, bool pack, bool merge)
    {
        if (children.empty())
            return nullptr;
        auto& page = mPages.at(index);
        if (merge)
            for (std::size_t i = 0; i < page.mSections.size(); ++i)
                if (page.mSelector->getItemNameAt(i) == translated(caption))
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
            {}, { 0, 0, source->getWidth(), source->getHeight() }, MyGUI::Align::Default);
        host->setVisible(false);
        auto* view = scroll ? host->createWidget<MyGUI::ScrollView>("MW_ScrollViewVH",
                                  { 0, 0, host->getWidth(), host->getHeight() }, MyGUI::Align::Stretch)
                            : nullptr;
        auto* content = view ? view->createWidget<MyGUI::Widget>(
                                   {}, { 0, 0, source->getWidth(), source->getHeight() }, MyGUI::Align::Default)
                             : host;
        if (view)
            view->setCanvasAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        int bottom = 8, width = 0, top = 8;
        for (auto* child : children)
        {
            auto coord = child->getCoord();
            if (pack)
            {
                coord.top = top;
                top += coord.height + 8;
            }
            bottom = std::max(bottom, coord.bottom() + 8);
            width = std::max(width, coord.right() + 8);
            child->attachToWidget(content);
            child->setCoord(coord);
        }
        if (view)
            bindScrolling(content);
        page.mSelector->addItem(translated(caption));
        page.mSections.push_back({ host, view, content, width, bottom });
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
