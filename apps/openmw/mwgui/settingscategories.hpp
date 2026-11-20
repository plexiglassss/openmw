#ifndef MWGUI_SETTINGSCATEGORIES_H
#define MWGUI_SETTINGSCATEGORIES_H

#include <memory>
#include <string>
#include <vector>

namespace MyGUI
{
    class ComboBox;
    class ScrollView;
    class TabControl;
    class TabItem;
    class Widget;
}

namespace MWGui
{
    // Move existing controls, retaining their identities, values and delegates.
    // MyGUI owns the widgets; this object owns only the category navigation.
    class SettingsCategories
    {
    public:
        explicit SettingsCategories(MyGUI::TabControl* tabs);
        ~SettingsCategories();
        void layout();

    private:
        struct Section
        {
            MyGUI::Widget* mHost;
            MyGUI::ScrollView* mScroll;
            MyGUI::Widget* mContent;
            int mMinWidth;
            int mMinHeight;
        };
        struct Page
        {
            MyGUI::TabItem* mTab;
            MyGUI::ComboBox* mSelector;
            std::vector<Section> mSections;
        };
        std::vector<Page> mPages;

        MyGUI::Widget* addSection(std::size_t page, const std::string& caption, MyGUI::Widget* source,
            const std::vector<MyGUI::Widget*>& children, bool scroll, bool pack = false, bool merge = false);
        void onCategoryChanged(MyGUI::ComboBox* sender, std::size_t index);
    };
}

#endif
