#ifndef OPENMW_LUAUI_TEXTEDIT
#define OPENMW_LUAUI_TEXTEDIT

#include <vector>

#include <MyGUI_EditBox.h>

#include "widget.hpp"

namespace LuaUi
{
    class LuaTextEdit : public MyGUI::Widget, public WidgetExtension
    {
        MYGUI_RTTI_DERIVED(LuaTextEdit)

    public:
        bool isTextInput() override { return mEditBox->getEditStatic(); }
        // Measuring an autosized editor can reformat text and move its scroll
        // position. Keep the established pass sequence for editable controls.
        bool allowsLayoutReuse() const override { return false; }

    protected:
        void initialize() override;
        void deinitialize() override;
        void updateProperties() override;
        void updateCoord() override;
        void updateChildren() override;
        MyGUI::IntSize calculateSize() const override;

        const std::vector<std::string_view>& allUsedProperties() const override;

    private:
        void textChange(MyGUI::EditBox*);

        MyGUI::EditBox* mEditBox = nullptr;
        bool mMultiline{ false };
        bool mAutoSize{ false };
    };
}

#endif // OPENMW_LUAUI_TEXTEDIT
