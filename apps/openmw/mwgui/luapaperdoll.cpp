#include <osg/Vec2f>
#include "luapaperdoll.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "inventorywindow.hpp"
#include <MyGUI_ISubWidgetRect.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_Gui.h>

namespace MWGui
{
    void LuaPaperDoll::initialize()
    {
        changeWidgetSkin("ImageBox");
        WidgetExtension::initialize();
        eventMouseMove += MyGUI::newDelegate(this, &LuaPaperDoll::hovered);
        eventMouseButtonPressed += MyGUI::newDelegate(this, &LuaPaperDoll::clicked);
        MyGUI::Gui::getInstance().eventFrameStart += MyGUI::newDelegate(this, &LuaPaperDoll::frameStarted);
    }

    LuaPaperDoll::~LuaPaperDoll()
    {
        MyGUI::Gui::getInstance().eventFrameStart -= MyGUI::newDelegate(this, &LuaPaperDoll::frameStarted);
    }

    void LuaPaperDoll::frameStarted(float)
    {
        if (!getInheritedVisible())
            return;
        auto* inventory = MWBase::Environment::get().getWindowManager()->getInventoryWindow();
        if (!inventory)
            return;
        inventory->syncPaperDoll(getWidth(), getHeight());
        setRenderItemTexture(inventory->paperDollTexture());
        getSubWidgetMain()->_setUVSet(inventory->paperDollUV());
    }

    void LuaPaperDoll::updateProperties()
    {
        WidgetExtension::updateProperties();
        auto* inventory = MWBase::Environment::get().getWindowManager()->getInventoryWindow();
        if (!inventory)
            return;
        setRenderItemTexture(inventory->paperDollTexture());
        getSubWidgetMain()->_setUVSet(inventory->paperDollUV());
    }

    void LuaPaperDoll::hovered(MyGUI::Widget*, int left, int top)
    {
        auto* inventory = MWBase::Environment::get().getWindowManager()->getInventoryWindow();
        if (!inventory) return;
        const auto pos = MyGUI::IntPoint(left, top) - getAbsolutePosition();
        const int slot = inventory->paperDollSlot(pos.left, pos.top, getWidth(), getHeight());
        protectedCall([this, slot, left, top](LuaUtil::LuaView& view) {
            auto event = view.newTable();
            event["slot"] = slot;
            event["position"] = osg::Vec2f(float(left), float(top));
            triggerEvent("paperDollHover", event);
        });
    }

    void LuaPaperDoll::clicked(MyGUI::Widget*, int left, int top, MyGUI::MouseButton button)
    {
        if (button != MyGUI::MouseButton::Left) return;
        const auto pos = MyGUI::IntPoint(left, top) - getAbsolutePosition();
        auto* inventory = MWBase::Environment::get().getWindowManager()->getInventoryWindow();
        if (!inventory)
            return;
        const int slot = inventory->paperDollSlot(pos.left, pos.top, getWidth(), getHeight());
        protectedCall([this, slot](LuaUtil::LuaView& view) {
            auto event = view.newTable();
            event["slot"] = slot; // same zero-based slots as types.Actor.getEquipment
            triggerEvent("paperDollClick", event);
        });
    }
}
