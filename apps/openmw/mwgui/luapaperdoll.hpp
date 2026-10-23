#ifndef OPENMW_MWGUI_LUAPAPERDOLL_H
#define OPENMW_MWGUI_LUAPAPERDOLL_H
#include <MyGUI_ImageBox.h>
#include <components/lua_ui/widget.hpp>
namespace MWGui
{
    // Native preview in any player Lua UI layout, sharing the engine's preview.
    class LuaPaperDoll : public MyGUI::ImageBox, public LuaUi::WidgetExtension
    {
        MYGUI_RTTI_DERIVED(LuaPaperDoll)
    public:
        ~LuaPaperDoll() override;

    protected:
        void initialize() override;
        void updateProperties() override;

    private:
        void hovered(MyGUI::Widget*, int, int);
        void clicked(MyGUI::Widget*);
        void frameStarted(float);
    };
}
#endif
