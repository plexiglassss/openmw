#ifndef OPENMW_MWGUI_TRAVELTOOLTIPVIEW_HPP
#define OPENMW_MWGUI_TRAVELTOOLTIPVIEW_HPP

#include "traveltooltipdata.hpp"

#include <MyGUI_Types.h>

#include <string>
#include <string_view>

namespace MyGUI
{
    class Widget;
    class ImageBox;
    class EditBox;
    class ITexture;
}

namespace MWGui
{
    struct TravelTooltipStyle
    {
        bool mWide = true;
        int mScale = 100;
        int mColour = 0;
        int mTitleFont = 2;
        int mTextFont = 0;
        int mFontSize = 16;
        int mViewWidth = 1280;
        int mViewHeight = 720;
    };

    // Only MyGUI is needed to render this view. Content resolution, engine settings
    // and VFS access belong to TravelTooltip; this permits real widget layout tests.
    class TravelTooltipView
    {
    public:
        explicit TravelTooltipView(MyGUI::Widget* parent);
        ~TravelTooltipView();
        TravelTooltipView(const TravelTooltipView&) = delete;
        TravelTooltipView& operator=(const TravelTooltipView&) = delete;
        TravelTooltipView(TravelTooltipView&&) = delete;
        TravelTooltipView& operator=(TravelTooltipView&&) = delete;

        MyGUI::IntSize render(const TravelTooltips::Image& image, std::string_view title,
            std::string_view description, bool overview, const TravelTooltipStyle& style);
        void setVisible(bool visible);

    private:
        MyGUI::Widget* mPanel = nullptr;
        MyGUI::ImageBox* mImage = nullptr;
        MyGUI::EditBox* mTitle = nullptr;
        MyGUI::EditBox* mDescription = nullptr;
        MyGUI::ITexture* mTexture = nullptr;
        std::string mLoadedImage;
    };
}

#endif
