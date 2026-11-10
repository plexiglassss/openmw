#include "locationrenderingprofiles.hpp"

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <istream>
#include <set>
#include <stdexcept>

namespace MWRender
{
    namespace
    {
        void validateText(std::string_view value, std::string_view field)
        {
            if (value.empty() || value.find('\0') != std::string_view::npos
                || value.find('\n') != std::string_view::npos || value.find('\r') != std::string_view::npos
                || value.find_first_not_of(" \t") == std::string_view::npos)
                throw std::runtime_error("Invalid empty or multiline location rendering profile " + std::string(field));
        }

        void validateId(std::string_view value)
        {
            validateText(value, "id");
            if (!std::all_of(value.begin(), value.end(), [](unsigned char c) {
                    return std::isalnum(c) || c == '-' || c == '_' || c == '.';
                }))
                throw std::runtime_error("Location rendering profile id may contain only letters, digits, '.', '-' and '_'");
        }

        void validateKeys(const YAML::Node& node, std::initializer_list<std::string_view> allowed,
            std::string_view context)
        {
            if (!node || !node.IsMap())
                throw std::runtime_error("Location rendering profile requires a mapping: " + std::string(context));
            std::set<std::string, std::less<>> seen;
            for (const auto& entry : node)
            {
                if (!entry.first.IsScalar())
                    throw std::runtime_error("Location rendering profile keys must be strings: " + std::string(context));
                const std::string key = entry.first.as<std::string>();
                if (std::find(allowed.begin(), allowed.end(), key) == allowed.end() || !seen.insert(key).second)
                    throw std::runtime_error(
                        "Unknown or repeated location rendering profile key in " + std::string(context) + ": " + key);
            }
        }

        std::vector<std::string> readStringList(const YAML::Node& root, const char* key, bool required)
        {
            const YAML::Node values = root[key];
            if (!values)
            {
                if (required)
                    throw std::runtime_error("Location rendering profile requires a nonempty list: " + std::string(key));
                return {};
            }
            if (!values.IsSequence() || (required && values.size() == 0))
                throw std::runtime_error("Location rendering profile requires a list: " + std::string(key));
            std::vector<std::string> result;
            result.reserve(values.size());
            std::set<std::string, std::less<>> normalized;
            for (const YAML::Node& item : values)
            {
                if (!item.IsScalar())
                    throw std::runtime_error("Location rendering profile list entries must be strings: " + std::string(key));
                std::string value = item.as<std::string>();
                validateText(value, key);
                const std::string folded = Misc::StringUtils::lowerCase(value);
                if (!normalized.insert(folded).second)
                    throw std::runtime_error("Repeated location rendering profile entry in " + std::string(key) + ": " + value);
                result.push_back(std::move(value));
            }
            return result;
        }

        bool readRequiredBool(const YAML::Node& root, const char* key)
        {
            const YAML::Node value = root[key];
            if (!value || !value.IsScalar())
                throw std::runtime_error("Location rendering profile requires a boolean shader value: " + std::string(key));
            try
            {
                return value.as<bool>();
            }
            catch (const YAML::Exception&)
            {
                throw std::runtime_error("Location rendering profile shader value is not boolean: " + std::string(key));
            }
        }

        void validateProfile(const LocationRenderingProfile& profile)
        {
            validateId(profile.mId);
            validateText(profile.mName, "name");
            if (profile.mInteriors.empty())
                throw std::runtime_error("Location rendering profile requires at least one interior cell");
            for (const std::string& name : profile.mRequiredContent)
            {
                validateText(name, "required-content");
                if (name.find_first_of("/\\:") != std::string::npos || name == "." || name == "..")
                    throw std::runtime_error("Location rendering required-content must contain filenames, not paths");
            }
            for (const std::string& name : profile.mInteriors)
                validateText(name, "cells");
        }
    }

    LocationRenderingProfile parseLocationRenderingProfile(std::istream& stream)
    {
        const YAML::Node root = YAML::Load(stream);
        validateKeys(root,
            { "format", "id", "name", "enabled", "priority", "required-content", "match", "shaders" }, "root");

        if (!root["format"] || !root["format"].IsScalar() || root["format"].as<int>() != 2)
            throw std::runtime_error("Unsupported location rendering profile format (expected 2)");
        if (!root["id"] || !root["id"].IsScalar())
            throw std::runtime_error("Location rendering profile requires an id");
        if (!root["name"] || !root["name"].IsScalar())
            throw std::runtime_error("Location rendering profile requires a name");

        LocationRenderingProfile result;
        result.mId = root["id"].as<std::string>();
        result.mName = root["name"].as<std::string>();
        if (root["enabled"])
        {
            if (!root["enabled"].IsScalar())
                throw std::runtime_error("Location rendering profile enabled must be boolean");
            result.mEnabled = root["enabled"].as<bool>();
        }
        if (root["priority"])
        {
            if (!root["priority"].IsScalar())
                throw std::runtime_error("Location rendering profile priority must be an integer");
            result.mPriority = root["priority"].as<int>();
        }
        result.mRequiredContent = readStringList(root, "required-content", false);

        const YAML::Node match = root["match"];
        validateKeys(match, { "type", "cells" }, "match");
        if (!match["type"] || !match["type"].IsScalar() || match["type"].as<std::string>() != "interior")
            throw std::runtime_error("Location rendering profile match.type must be 'interior'");
        result.mInteriors = readStringList(match, "cells", true);

        const YAML::Node shaders = root["shaders"];
        validateKeys(shaders,
            { "clamp-lighting", "force-per-pixel-lighting", "auto-use-object-normal-maps",
                "auto-use-object-specular-maps" },
            "shaders");
        result.mShaders.mClampLighting = readRequiredBool(shaders, "clamp-lighting");
        result.mShaders.mForcePerPixelLighting = readRequiredBool(shaders, "force-per-pixel-lighting");
        result.mShaders.mAutoUseObjectNormalMaps = readRequiredBool(shaders, "auto-use-object-normal-maps");
        result.mShaders.mAutoUseObjectSpecularMaps = readRequiredBool(shaders, "auto-use-object-specular-maps");

        validateProfile(result);
        return result;
    }

    void LocationRenderingProfiles::clear()
    {
        mProfiles.clear();
        mCells.clear();
        mIds.clear();
    }

    void LocationRenderingProfiles::load(const VFS::Manager& vfs, const std::vector<std::string>& activeContent)
    {
        clear();
        for (const VFS::Path::Normalized& path : vfs.getRecursiveDirectoryIterator("shader_profiles/"))
        {
            const std::string& name = path.value();
            if (!name.ends_with(".yaml") && !name.ends_with(".yml") && !name.ends_with(".json"))
                continue;
            try
            {
                auto stream = vfs.get(path);
                const auto profile = parseLocationRenderingProfile(*stream);
                if (add(profile, activeContent))
                    Log(Debug::Info) << "Location rendering profile: loaded '" << profile.mName << "' from " << name
                                     << " (" << profile.mInteriors.size() << " interiors, priority "
                                     << profile.mPriority << ")";
                else
                    Log(Debug::Info) << "Location rendering profile: inactive or disabled " << name;
            }
            catch (const std::exception& error)
            {
                Log(Debug::Warning) << "Location rendering profile: skipping " << name << ": " << error.what();
            }
        }
    }

    bool LocationRenderingProfiles::add(
        const LocationRenderingProfile& profile, const std::vector<std::string>& activeContent)
    {
        validateProfile(profile);
        if (!profile.mEnabled)
            return false;

        std::set<std::string, std::less<>> active;
        for (std::string_view name : activeContent)
        {
            const std::size_t separator = name.find_last_of("/\\");
            if (separator != std::string_view::npos)
                name.remove_prefix(separator + 1);
            active.insert(Misc::StringUtils::lowerCase(name));
        }
        for (const std::string& name : profile.mRequiredContent)
            if (!active.contains(Misc::StringUtils::lowerCase(name)))
                return false;

        const std::string normalizedId = Misc::StringUtils::lowerCase(profile.mId);
        if (mIds.contains(normalizedId))
            throw std::runtime_error("Repeated location rendering profile id: " + profile.mId);

        // Check every priority before publishing any cell. A higher-priority
        // winner must not hide a conflict between two lower-priority profiles.
        std::vector<std::string> cells;
        cells.reserve(profile.mInteriors.size());
        for (const std::string& name : profile.mInteriors)
        {
            cells.push_back(Misc::StringUtils::lowerCase(name));
            const auto found = mCells.find(cells.back());
            if (found == mCells.end())
                continue;
            const auto existing = found->second.find(profile.mPriority);
            if (existing != found->second.end())
                throw std::runtime_error("Location rendering profiles have equal priority at cell '" + name + "': '"
                    + mProfiles.at(existing->second).mId + "' and '" + profile.mId + "'");
        }

        const std::size_t index = mProfiles.size();
        mProfiles.push_back(profile);
        mIds.insert(normalizedId);
        for (std::string& cell : cells)
            mCells[std::move(cell)].emplace(profile.mPriority, index);
        return true;
    }

    const LocationRenderingProfile* LocationRenderingProfiles::find(std::string_view cellName, bool exterior) const
    {
        if (exterior || mCells.empty())
            return nullptr;
        const auto found = mCells.find(Misc::StringUtils::lowerCase(cellName));
        return found == mCells.end() ? nullptr : &mProfiles.at(found->second.rbegin()->second);
    }
}
