#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_MAP_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_MAP_H

#include <components/misc/constants.hpp>
#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

#include <osg/Math>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <cstdint>
#include <string>
#include <string_view>

namespace Settings
{
    struct MapCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<int> mGlobalMapCellSize{ mIndex, "Map", "global map cell size", makeClampSanitizerInt(1, 50) };
        SettingValue<bool> mLocalMapHudFogOfWar{ mIndex, "Map", "local map hud fog of war" };
        SettingValue<int> mLocalMapResolution{ mIndex, "Map", "local map resolution", makeMaxSanitizerInt(1) };
        SettingValue<int> mLocalMapWidgetSize{ mIndex, "Map", "local map widget size", makeMaxSanitizerInt(1) };
        SettingValue<bool> mGlobal{ mIndex, "Map", "global" };
        SettingValue<bool> mAllowZooming{ mIndex, "Map", "allow zooming" };
        SettingValue<bool> mLocationIcons{ mIndex, "Map", "location icons" };
        SettingValue<bool> mLocationEntranceIcons{ mIndex, "Map", "location entrance icons" };
        SettingValue<bool> mLocationIconTooltips{ mIndex, "Map", "location icon tooltips" };
        SettingValue<int> mLocationIconSize{ mIndex, "Map", "location icon size", makeClampSanitizerInt(16, 40) };
        SettingValue<bool> mLocalDoorIcons{ mIndex, "Map", "location local door icons" };
        SettingValue<int> mLocalDoorIconSize{ mIndex, "Map", "location local icon size", makeClampSanitizerInt(8, 40) };
        SettingValue<int> mLocationIconMaximumSize{ mIndex, "Map", "location icon maximum size", makeClampSanitizerInt(8, 64) };
        SettingValue<int> mLocationIconColours{ mIndex, "Map", "location icon colours", makeClampSanitizerInt(0, 2) };
        SettingValue<int> mLocationIconCategories{
            mIndex, "Map", "location icon categories", makeClampSanitizerInt(0, (1 << 19) - 1) };
        SettingValue<int> mMaxLocalViewingDistance{ mIndex, "Map", "max local viewing distance",
            makeMaxSanitizerInt(Constants::CellGridRadius) };
    };
}

#endif
