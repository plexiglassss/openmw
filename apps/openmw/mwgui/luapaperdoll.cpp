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
        eventMouseButtonClick += MyGUI::newDelegate(this, &LuaPaperDoll::clicked);
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

    void LuaPaperDoll::clicked(MyGUI::Widget*)
    {
        const auto pos = MyGUI::InputManager::getInstance().getMousePosition() - getAbsolutePosition();
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
