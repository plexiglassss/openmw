#ifndef OPENMW_COMPONENTS_MISC_COMBINEDPERFORMANCE_H
#define OPENMW_COMPONENTS_MISC_COMBINEDPERFORMANCE_H

#include <cstdlib>
#include <string_view>

namespace Misc
{
    // Process-local rollback/comparison switch for the combined native work.
    // Earlier accepted engine features and performance changes are unaffected.
    inline bool useCombinedPerformance()
    {
        static const bool enabled = [] {
            const char* value = std::getenv("OPENMW_COMBINED_PERFORMANCE");
            return value == nullptr || std::string_view(value) != "0";
        }();
        return enabled;
    }
}

#endif
