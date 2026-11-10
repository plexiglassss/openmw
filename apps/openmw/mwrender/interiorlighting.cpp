#include "interiorlighting.hpp"

#include <components/misc/strings/lower.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <istream>
#include <set>
#include <stdexcept>

namespace MWRender
{
    namespace
    {
        void validateName(std::string_view value, std::string_view field)
        {
            if (value.empty() || value.find('\0') != std::string_view::npos
                || value.find('\n') != std::string_view::npos || value.find('\r') != std::string_view::npos
                || value.find_first_not_of(" \t") == std::string_view::npos)
                throw std::runtime_error("Invalid empty or multiline interior lighting " + std::string(field));
        }

        std::vector<std::string> readList(const YAML::Node& root, const char* key)
        {
            const YAML::Node values = root[key];
            if (!values || !values.IsSequence() || values.size() == 0)
                throw std::runtime_error("Interior lighting profile requires a nonempty list: " + std::string(key));
            std::vector<std::string> result;
            result.reserve(values.size());
            for (const YAML::Node& item : values)
            {
                if (!item.IsScalar())
                    throw std::runtime_error("Interior lighting list entries must be strings: " + std::string(key));
                std::string value = item.as<std::string>();
                validateName(value, key);
                result.push_back(std::move(value));
            }
            return result;
        }

        void validate(const InteriorLightingProfile& profile)
        {
            validateName(profile.mName, "name");
            if (profile.mRequiredContent.empty() || profile.mInteriors.empty())
                throw std::runtime_error("Interior lighting profile requires content files and interiors");
            for (const std::string& name : profile.mRequiredContent)
            {
                validateName(name, "required-content");
                if (name.find_first_of("/\\:") != std::string::npos || name == "." || name == "..")
                    throw std::runtime_error("Interior lighting required-content must contain filenames, not paths");
            }
            for (const std::string& name : profile.mInteriors)
                validateName(name, "interiors");
        }
    }

    InteriorLightingProfile parseInteriorLightingProfile(std::istream& stream)
    {
        const YAML::Node root = YAML::Load(stream);
        if (!root.IsMap())
            throw std::runtime_error("Interior lighting profile must be a mapping");
        const std::set<std::string, std::less<>> allowed{ "format", "name", "mode", "required-content", "interiors" };
        std::set<std::string> seen;
        for (const auto& entry : root)
        {
            const std::string key = entry.first.as<std::string>();
            if (!allowed.contains(key) || !seen.insert(key).second)
                throw std::runtime_error("Unknown or repeated interior lighting key: " + key);
        }
        if (!root["format"] || root["format"].as<int>() != 1)
            throw std::runtime_error("Unsupported interior lighting profile format (expected 1)");
        if (!root["mode"] || root["mode"].as<std::string>() != "classic")
            throw std::runtime_error("Unsupported interior lighting profile mode (expected classic)");
        if (!root["name"] || !root["name"].IsScalar())
            throw std::runtime_error("Interior lighting profile requires a name");
        InteriorLightingProfile result{ root["name"].as<std::string>(), readList(root, "required-content"),
            readList(root, "interiors") };
        validate(result);
        return result;
    }

    void InteriorLightingProfiles::clear()
    {
        mCells.clear();
    }

    bool InteriorLightingProfiles::add(
        const InteriorLightingProfile& profile, const std::vector<std::string>& activeContent)
    {
        validate(profile);
        std::set<std::string> active;
        for (std::string_view name : activeContent)
        {
            // Content arguments are usually basenames. Handle an explicit path
            // consistently on Windows and POSIX without changing file discovery.
            const std::size_t separator = name.find_last_of("/\\");
            if (separator != std::string_view::npos)
                name.remove_prefix(separator + 1);
            active.insert(Misc::StringUtils::lowerCase(name));
        }
        for (const std::string& name : profile.mRequiredContent)
            if (!active.contains(Misc::StringUtils::lowerCase(name)))
                return false;

        // Validate the complete addition before publishing any of its cells.
        // One overlapping rule must not leave a half-installed profile behind.
        std::vector<std::string> normalized;
        normalized.reserve(profile.mInteriors.size());
        for (const std::string& name : profile.mInteriors)
        {
            std::string key = Misc::StringUtils::lowerCase(name);
            const auto found = mCells.find(key);
            if (found != mCells.end() && found->second != profile.mName)
                throw std::runtime_error("Interior lighting profiles overlap at cell: " + name);
            normalized.push_back(std::move(key));
        }
        for (const std::string& name : normalized)
            mCells.emplace(name, profile.mName);
        return true;
    }

    std::string_view InteriorLightingProfiles::find(std::string_view cellName, bool exterior) const
    {
        if (exterior || mCells.empty())
            return {};
        const auto found = mCells.find(Misc::StringUtils::lowerCase(cellName));
        return found == mCells.end() ? std::string_view{} : std::string_view(found->second);
    }
}
