#ifndef OPENMW_MWGUI_MAPIMAGEMARKER_H
#define OPENMW_MWGUI_MAPIMAGEMARKER_H

#include <MyGUI_ImageBox.h>

namespace MWGui
{
    // World locations and teleport doors draw the entire glyph with the same hover handling.
    class MapImageMarker final : public MyGUI::ImageBox
    {
        MYGUI_RTTI_DERIVED(MapImageMarker)

    public:
        void setMarkerImage(const std::string& path, int pixels)
        {
            // A recycled ImageBox otherwise keeps the previous atlas tile size.
            setImageInfo(path, { 0, 0, pixels, pixels }, { pixels, pixels });
            setImageIndex(0);
        }

        void setMarkerColours(const MyGUI::Colour& normal, const MyGUI::Colour& hover)
        {
            mNormalColour = normal;
            mHoverColour = hover;
            setColour(mHovered ? hover : normal);
        }

    private:
        MyGUI::Colour mNormalColour;
        MyGUI::Colour mHoverColour;
        bool mHovered = false;

        void onMouseLostFocus(MyGUI::Widget*) override
        {
            mHovered = false;
            setColour(mNormalColour);
        }
        void onMouseSetFocus(MyGUI::Widget*) override
        {
            mHovered = true;
            setColour(mHoverColour);
        }
    };
}
#endif
