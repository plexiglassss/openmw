#ifndef OPENMW_MWRENDER_LOCATIONRENDERINGPROFILES_H
#define OPENMW_MWRENDER_LOCATIONRENDERINGPROFILES_H

#include <compare>
#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace SceneUtil
{
    struct LightCommon;
}

namespace MWRender
{
    struct LocationRenderingShaderSettings
    {
        bool mClampLighting = false;
        bool mForcePerPixelLighting = false;
        bool mAutoUseObjectNormalMaps = false;
        bool mAutoUseObjectSpecularMaps = false;

        auto operator<=>(const LocationRenderingShaderSettings&) const = default;
    };

    struct LocationRenderingProfile
    {
        std::string mId;
        std::string mName;
        bool mEnabled = true;
        int mPriority = 0;
        std::vector<std::string> mRequiredContent;
        std::vector<std::string> mInteriors;
        bool mContainsNegativeLight = false;
        LocationRenderingShaderSettings mShaders;
    };

    // Profiles are external data. The engine contains no mod names or cell lists.
    // Matching is literal and case-insensitive; wildcards and regular expressions
    // are intentionally not interpreted.
    LocationRenderingProfile parseLocationRenderingProfile(std::istream& stream);
    bool isQualifyingNegativeLight(const SceneUtil::LightCommon& light);

    class LocationRenderingProfiles
    {
    public:
        void clear();
        void load(const std::filesystem::path& file, const std::vector<std::string>& activeContent);
        bool add(const LocationRenderingProfile& profile, const std::vector<std::string>& activeContent);
        const LocationRenderingProfile* find(
            std::string_view cellName, bool exterior, bool containsNegativeLight = false) const;
        std::size_t size() const { return mCells.size(); }
        const std::vector<LocationRenderingProfile>& profiles() const { return mProfiles; }

    private:
        std::vector<LocationRenderingProfile> mProfiles;
        using Priorities = std::map<int, std::vector<std::size_t>>;
        std::map<std::string, Priorities, std::less<>> mCells;
        Priorities mNegativeLights;
        std::set<std::string, std::less<>> mIds;
        void reportConflicts() const;
    };
}

#endif
