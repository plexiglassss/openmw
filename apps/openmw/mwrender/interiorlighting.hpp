#ifndef OPENMW_MWRENDER_INTERIORLIGHTING_H
#define OPENMW_MWRENDER_INTERIORLIGHTING_H

#include <cstddef>
#include <iosfwd>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace MWRender
{
    struct InteriorLightingProfile
    {
        std::string mName;
        std::vector<std::string> mRequiredContent;
        std::vector<std::string> mInteriors;
    };

    // A profile is a literal allowlist. It cannot match an exterior, a prefix,
    // a regex, or every cell modified by a content file.
    InteriorLightingProfile parseInteriorLightingProfile(std::istream& stream);

    class InteriorLightingProfiles
    {
    public:
        void clear();
        bool add(const InteriorLightingProfile& profile, const std::vector<std::string>& activeContent);
        std::string_view find(std::string_view cellName, bool exterior) const;
        std::size_t size() const { return mCells.size(); }

    private:
        std::map<std::string, std::string, std::less<>> mCells;
    };
}

#endif
