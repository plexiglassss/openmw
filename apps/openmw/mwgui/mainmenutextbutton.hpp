#ifndef OPENMW_MWGUI_MAINMENUTEXTBUTTON_H
#define OPENMW_MWGUI_MAINMENUTEXTBUTTON_H

#include <MyGUI_Button.h>

namespace MWGui
{
    /// A title-menu text entry whose mouse hover remains visible while it has key focus.
    class MainMenuTextButton final : public MyGUI::Button
    {
        MYGUI_RTTI_DERIVED(MainMenuTextButton)

    protected:
        void onMouseSetFocus(MyGUI::Widget* oldWidget) override
        {
            mMouseFocus = true;
            Base::onMouseSetFocus(oldWidget);
            updateMenuState();
        }

        void onMouseLostFocus(MyGUI::Widget* newWidget) override
        {
            mMouseFocus = false;
            Base::onMouseLostFocus(newWidget);
            updateMenuState();
        }

        void onKeySetFocus(MyGUI::Widget* oldWidget) override
        {
            mKeyFocus = true;
            Base::onKeySetFocus(oldWidget);
            updateMenuState();
        }

        void onKeyLostFocus(MyGUI::Widget* newWidget) override
        {
            mKeyFocus = false;
            Base::onKeyLostFocus(newWidget);
            updateMenuState();
        }

        void onMouseButtonPressed(int left, int top, MyGUI::MouseButton id) override
        {
            if (id == MyGUI::MouseButton::Left)
                mMousePressed = true;
            Base::onMouseButtonPressed(left, top, id);
            updateMenuState();
        }

        void onMouseButtonReleased(int left, int top, MyGUI::MouseButton id) override
        {
            if (id == MyGUI::MouseButton::Left)
                mMousePressed = false;
            Base::onMouseButtonReleased(left, top, id);
            updateMenuState();
        }

        void baseUpdateEnable() override
        {
            Base::baseUpdateEnable();
            if (!getInheritedEnabled())
            {
                mMouseFocus = false;
                mKeyFocus = false;
                mMousePressed = false;
            }
            updateMenuState();
        }

    private:
        void updateMenuState()
        {
            // MyGUI::Button combines mouse and key focus into "highlighted".
            // Continue starts with key focus, so it needs a separate focused
            // state for moving the pointer over it to produce a visible change.
            if (!getInheritedEnabled())
                _setWidgetState("disabled");
            else if (mMousePressed)
                _setWidgetState("pushed");
            else if (mMouseFocus)
                _setWidgetState("highlighted");
            else if (mKeyFocus)
                _setWidgetState("focused");
            else
                _setWidgetState("normal");
        }

        bool mMouseFocus = false;
        bool mKeyFocus = false;
        bool mMousePressed = false;
    };
}

#endif
