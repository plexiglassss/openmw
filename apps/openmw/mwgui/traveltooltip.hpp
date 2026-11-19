#ifndef OPENMW_MWGUI_TRAVELTOOLTIP_HPP
#define OPENMW_MWGUI_TRAVELTOOLTIP_HPP

#include "traveltooltipdata.hpp"

#include <MyGUI_Types.h>

#include <memory>
#include <optional>
#include <string>

namespace MyGUI
{
    class Widget;
}

namespace MWGui
{
    class TravelTooltipView;
    // One persistent panel: large destination textures and text are not rebuilt every frame.
    // The parent owns the MyGUI widgets; this object only manages their content and cache.
    class TravelTooltip
    {
    public:
        explicit TravelTooltip(MyGUI::Widget* parent);
        ~TravelTooltip();
        std::optional<MyGUI::IntSize> show(const MyGUI::Widget& source, bool overview, MyGUI::IntSize viewport);
        void clear();

    private:
        struct Request
        {
            std::string mDestination;
            std::string mProvider;
            std::string mVariant;
            bool mGondolier = false;
            bool mOverview = false;
            bool mFallback = false;
            bool mWide = true;
            int mPack = 0;
            int mOverviewMap = 0;
            int mScale = 100;
            int mColour = 0;
            int mTitleFont = 0;
            int mTextFont = 0;
            int mFontSize = 16;
            int mViewWidth = 0;
            int mViewHeight = 0;

            bool operator==(const Request&) const = default;
        };

        void loadData();
        void rebuild(const Request& request);

        std::unique_ptr<TravelTooltipView> mView;
        TravelTooltips::Data mData;
        bool mLoaded = false;
        std::optional<Request> mRequest;
        MyGUI::IntSize mSize;
    };
}

#endif
