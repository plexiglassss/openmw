#ifndef OPENMW_MWRENDER_LOCATIONRENDERINGPROFILES_H
#define OPENMW_MWRENDER_LOCATIONRENDERINGPROFILES_H

#include <compare>
#include <cstddef>
#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace VFS
{
    class Manager;
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
        LocationRenderingShaderSettings mShaders;
    };

    // Profiles are external data. The engine contains no mod names or cell lists.
    // Matching is literal and case-insensitive; wildcards and regular expressions
    // are intentionally not interpreted.
    LocationRenderingProfile parseLocationRenderingProfile(std::istream& stream);

    class LocationRenderingProfiles
    {
    public:
        void clear();
        void load(const VFS::Manager& vfs, const std::vector<std::string>& activeContent);
        bool add(const LocationRenderingProfile& profile, const std::vector<std::string>& activeContent);
        const LocationRenderingProfile* find(std::string_view cellName, bool exterior) const;
        std::size_t size() const { return mCells.size(); }
        const std::vector<LocationRenderingProfile>& profiles() const { return mProfiles; }

    private:
        std::vector<LocationRenderingProfile> mProfiles;
        std::map<std::string, std::map<int, std::size_t>, std::less<>> mCells;
        std::set<std::string, std::less<>> mIds;
    };
}

#endif
