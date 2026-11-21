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
    class Window;
}

namespace MWGui
{
    void applySettingsWindowMinimumSize(MyGUI::Window* window, MyGUI::TabControl* tabs);

    // Group explicitly marked custom controls; stock and third-party pages stay intact.
    // Retain moved controls, their values and delegates.
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
            std::string mId;
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

        MyGUI::Widget* addSection(std::size_t page, const std::string& id, const std::string& caption,
            MyGUI::Widget* source, const std::vector<MyGUI::Widget*>& children, bool scroll, bool merge = false);
        void onCategoryChanged(MyGUI::ComboBox* sender, std::size_t index);
    };
}

#endif
