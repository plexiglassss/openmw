#include "traveltooltip.hpp"

#include "traveltooltipview.hpp"

#include <cstdint>
#include <exception>

#include <MyGUI_LanguageManager.h>
#include <MyGUI_Widget.h>

#include <components/debug/debuglog.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>

#include "../mwbase/environment.hpp"

namespace MWGui
{
    TravelTooltip::TravelTooltip(MyGUI::Widget* parent)
        : mView(std::make_unique<TravelTooltipView>(parent))
    {
    }

    TravelTooltip::~TravelTooltip() = default;

    void TravelTooltip::loadData()
    {
        if (mLoaded)
            return;
        mLoaded = true;
        try
        {
            const auto* vfs = MWBase::Environment::get().getResourceSystem()->getVFS();
            const VFS::Path::Normalized path(TravelTooltips::manifestPath);
            const auto stream = vfs->find(path);
            if (!stream)
            {
                Log(Debug::Warning) << "Illustrated travel tooltips: missing " << TravelTooltips::manifestPath
                                    << "; install the travel tooltip data directory.";
                return;
            }
            mData = TravelTooltips::readData(*stream);
            Log(Debug::Info) << "Illustrated travel tooltips: loaded " << mData.mImages.size() << " destination images, "
                             << mData.mDescriptions.size() << " descriptions and " << mData.mOverviews.size()
                             << " overview maps.";
        }
        catch (const std::exception& error)
        {
            // A broken optional art pack must not prevent dialogue or travel from opening.
            Log(Debug::Error) << "Illustrated travel tooltips: " << error.what();
        }
    }

    std::optional<MyGUI::IntSize> TravelTooltip::show(
        const MyGUI::Widget& source, bool overview, MyGUI::IntSize viewport)
    {
        const auto& settings = Settings::gui();
        if (!settings.mTravelEnabled || (overview && !settings.mTravelOverview)
            || (!overview && source.getUserString("TravelGondolier") == "y" && !settings.mTravelGondoliers)
            || (overview && !TravelTooltips::hasVvardenfellOverview(source.getUserString("TravelProvider")))
            || viewport.width < 80 || viewport.height < 80)
            return std::nullopt;

        Request request;
        request.mDestination = source.getUserString("Destination");
        request.mProvider = source.getUserString("TravelProvider");
        request.mVariant = source.getUserString("TravelVariant");
        request.mGondolier = source.getUserString("TravelGondolier") == "y";
        request.mOverview = overview;
        request.mFallback = settings.mTravelFallback;
        request.mWide = settings.mTravelWide;
        request.mPack = settings.mTravelPack;
        request.mOverviewMap = settings.mTravelOverviewMap;
        request.mScale = settings.mTravelScale;
        request.mColour = settings.mTravelColour;
        request.mTitleFont = settings.mTravelTitleFont;
        request.mTextFont = settings.mTravelTextFont;
        request.mFontSize = settings.mFontSize;
        request.mViewWidth = viewport.width;
        request.mViewHeight = viewport.height;
        if (!mRequest || *mRequest != request)
        {
            mRequest = request;
            mSize = MyGUI::IntSize();
            loadData();
            try
            {
                rebuild(request);
            }
            catch (const std::exception& error)
            {
                Log(Debug::Error) << "Cannot show illustrated travel tooltip: " << error.what();
                mSize = MyGUI::IntSize();
            }
        }
        if (mSize.width <= 0 || mSize.height <= 0)
            return std::nullopt;
        mView->setVisible(true);
        return mSize;
    }

    void TravelTooltip::rebuild(const Request& request)
    {
        const auto* vfs = MWBase::Environment::get().getResourceSystem()->getVFS();
        const auto available = [vfs](std::string_view path) { return vfs->exists(VFS::Path::Normalized(path)); };
        std::uint64_t seed = 14695981039346656037ull;
        for (char ch : request.mProvider + request.mVariant)
            seed = (seed ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
        const TravelTooltips::Image* image = request.mOverview
            ? TravelTooltips::overviewImage(mData, request.mOverviewMap, available)
            : TravelTooltips::destinationImage(
                mData, request.mDestination, request.mPack, request.mFallback, seed, available);
        if (!image)
            return;

        const TravelTooltipStyle style{ request.mWide, request.mScale, request.mColour,
            request.mTitleFont, request.mTextFont, request.mFontSize, request.mViewWidth, request.mViewHeight };
        const std::string title = MyGUI::LanguageManager::getInstance().replaceTags("#{sCell=" + request.mDestination + "}");
        mSize = mView->render(*image, title,
            TravelTooltips::destinationDescription(mData, request.mDestination, request.mGondolier), request.mOverview, style);
    }

    void TravelTooltip::clear()
    {
        mView->setVisible(false);
        mRequest.reset();
        mSize = MyGUI::IntSize();
    }
}
