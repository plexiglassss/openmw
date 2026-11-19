#include "icons.hpp"

#include <algorithm>
#include <cmath>
#include <istream>
#include <unordered_map>

#include <components/misc/strings/lower.hpp>

namespace MapMarkers
{
    namespace
    {
        IconTextures loadTextures(std::string_view group, const std::string& id, const IconSet::Exists& exists)
        {
            const std::string path = std::string(group) + '/' + id;
            IconTextures result{ "textures/mapmarkers/vanilla/" + path + ".dds", "textures/mapmarkers/type/" + path + ".png" };
            if (!exists(result.vanilla)) return {};
            if (!exists(result.byType)) result.byType.clear();
            return result;
        }
    }

    IconSet IconSet::read(std::istream& input, const Exists& exists)
    {
        IconSet set;
        std::unordered_map<std::string, std::size_t> localIds;
        std::string line;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#') continue;
            const auto tab = line.find('\t');
            if (tab == std::string::npos) continue;
            const auto second = line.find('\t', tab + 1);
            const std::string_view kind(line.data(), tab);
            const std::string id = line.substr(tab + 1, second == std::string::npos ? second : second - tab - 1);
            if (id.empty() || id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos) continue;
            if (kind == "world" && second == std::string::npos)
            {
                for (std::size_t i = 0; i < categoryCount; ++i)
                    if (definition(static_cast<Category>(i)).id == id)
                        set.mWorld[i] = loadTextures("world", id, exists);
            }
            else if (kind == "local" && second == std::string::npos && !localIds.contains(id))
            {
                auto paths = loadTextures("local", id, exists);
                if (paths.vanilla.empty()) continue;
                localIds.emplace(id, set.mDoors.size());
                if (id == "active_door") set.mDefaultDoor = set.mDoors.size();
                set.mDoors.push_back(std::move(paths));
            }
            else if ((kind == "name" || kind == "door") && second != std::string::npos && second + 1 < line.size())
            {
                const auto it = localIds.find(id);
                if (it != localIds.end())
                    (kind == "name" ? set.mNames : set.mDoorIds).push_back({ Misc::StringUtils::lowerCase(std::string_view(line).substr(second + 1)), it->second });
            }
        }
        return set;
    }

    const IconTextures& IconSet::world(Category category) const
    {
        return mWorld[static_cast<std::size_t>(isValid(category) ? category : Category::Misc)];
    }

    const IconTextures& IconSet::door(std::size_t index) const
    {
        return mDoors[index < mDoors.size() ? index : 0];
    }

    std::size_t IconSet::classifyDoor(std::string_view destination, std::string_view doorId, bool exteriorLike) const
    {
        // Later name rules win, so scan backwards and stop on the first match.
        if (exteriorLike && !mNames.empty())
        {
            const auto text = Misc::StringUtils::lowerCase(destination);
            for (auto it = mNames.rbegin(); it != mNames.rend(); ++it)
                if (text.find(it->text) != std::string::npos) return it->icon;
        }
        if (!mDoorIds.empty())
        {
            const auto id = Misc::StringUtils::lowerCase(doorId);
            for (auto it = mDoorIds.rbegin(); it != mDoorIds.rend(); ++it)
                if (id.find(it->text) != std::string::npos) return it->icon;
        }
        return mDefaultDoor;
    }

    float locationIconExtent(float base, float zoom, std::size_t weight, bool entrance, int maximum)
    {
        float size = base * zoom;
        if (!entrance)
        {
            if (zoom < 1.f) size *= static_cast<float>(std::sqrt(weight));
            else if (weight != 0) return 0.f;
        }
        return std::min(size, static_cast<float>(maximum));
    }

    int localIconExtent(int base, float zoom, int maximum)
    {
        return std::clamp(static_cast<int>(std::lround(base * zoom)), 1, maximum);
    }
}
