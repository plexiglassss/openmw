#include "locationrenderingprofiles.hpp"

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/sceneutil/lightcommon.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <istream>
#include <set>
#include <sstream>
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
            if (profile.mInteriors.empty() == !profile.mContainsNegativeLight)
                throw std::runtime_error(
                    "Location rendering profile requires either interior cells or a negative-light condition");
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

    static LocationRenderingProfile parseProfile(const YAML::Node& root)
    {
        validateKeys(root, { "id", "name", "enabled", "priority", "required-content", "match", "shaders" }, "profile");

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
        validateKeys(match, { "type", "cells", "condition" }, "match");
        if (!match["type"] || !match["type"].IsScalar() || match["type"].as<std::string>() != "interior")
            throw std::runtime_error("Location rendering profile match.type must be 'interior'");
        if (static_cast<bool>(match["cells"]) == static_cast<bool>(match["condition"]))
            throw std::runtime_error("Location rendering match requires exactly one of cells or condition");
        if (match["condition"])
        {
            if (!match["condition"].IsScalar() || match["condition"].as<std::string>() != "contains-negative-light")
                throw std::runtime_error("Unknown location rendering condition (expected contains-negative-light)");
            result.mContainsNegativeLight = true;
        }
        else
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

    LocationRenderingProfile parseLocationRenderingProfile(std::istream& stream)
    {
        return parseProfile(YAML::Load(stream));
    }

    bool isQualifyingNegativeLight(const SceneUtil::LightCommon& light)
    {
        // Radius zero can still emit: the renderer applies a minimum radius.
        // Alpha and pulse/flicker intensity do not determine authored darkness.
        return light.mNegative && (light.mColor.r() != 0.f || light.mColor.g() != 0.f || light.mColor.b() != 0.f);
    }

    void LocationRenderingProfiles::clear()
    {
        mProfiles.clear();
        mCells.clear();
        mNegativeLights.clear();
        mIds.clear();
    }

    void LocationRenderingProfiles::load(
        const std::filesystem::path& file, const std::vector<std::string>& activeContent)
    {
        clear();
        try
        {
            if (!std::filesystem::exists(file))
            {
                Log(Debug::Info) << "Location rendering profiles: no shared file at " << file
                                 << "; using global settings";
                return;
            }
            std::ifstream stream(file);
            if (!stream)
                throw std::runtime_error("Could not open shared location rendering file");
            const auto documents = YAML::LoadAll(stream);
            if (documents.size() != 1)
                throw std::runtime_error("Shared location rendering file must contain exactly one YAML document");
            const YAML::Node& root = documents.front();
            validateKeys(root, { "format", "profiles" }, "document");
            if (!root["format"] || !root["format"].IsScalar() || root["format"].as<int>() != 3)
                throw std::runtime_error("Unsupported shared location rendering format (expected 3)");
            const auto entries = root["profiles"];
            if (!entries || !entries.IsSequence())
                throw std::runtime_error("Shared location rendering file requires a profiles list");

            std::vector<LocationRenderingProfile> parsed;
            std::map<std::string, std::size_t, std::less<>> ids;
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                try
                {
                    auto profile = parseProfile(entries[i]);
                    ++ids[Misc::StringUtils::lowerCase(profile.mId)];
                    parsed.push_back(std::move(profile));
                }
                catch (const std::exception& error)
                {
                    Log(Debug::Warning) << "Location rendering profiles: skipping entry " << i + 1 << " at line "
                                        << entries[i].Mark().line + 1 << " in " << file << ": " << error.what();
                }
            }
            for (const auto& profile : parsed)
            {
                // Reject every duplicate, rather than allowing document order to
                // decide which occurrence supplies the runtime settings.
                if (ids.at(Misc::StringUtils::lowerCase(profile.mId)) != 1)
                {
                    Log(Debug::Warning) << "Location rendering profiles: duplicate id '" << profile.mId << "' in "
                                        << file << "; all occurrences skipped";
                    continue;
                }
                if (add(profile, activeContent))
                    Log(Debug::Info) << "Location rendering profile: loaded '" << profile.mName << "' from " << file
                                     << " (priority " << profile.mPriority << ")";
                else
                    Log(Debug::Info) << "Location rendering profile: inactive or disabled '" << profile.mId << "'";
            }
            reportConflicts();
        }
        catch (const std::exception& error)
        {
            clear();
            Log(Debug::Warning) << "Location rendering profiles: could not load " << file << ": " << error.what()
                                << "; using global settings";
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

        const std::size_t index = mProfiles.size();
        mProfiles.push_back(profile);
        mIds.insert(normalizedId);
        if (profile.mContainsNegativeLight)
            mNegativeLights[profile.mPriority].push_back(index);
        else
            for (const std::string& cell : profile.mInteriors)
                mCells[Misc::StringUtils::lowerCase(cell)][profile.mPriority].push_back(index);
        return true;
    }

    void LocationRenderingProfiles::reportConflicts() const
    {
        const auto report = [&](std::string_view cell, int priority, const std::vector<std::size_t>& entries) {
            if (entries.empty())
                return;
            const auto& first = mProfiles.at(entries.front());
            if (std::all_of(entries.begin(), entries.end(),
                    [&](std::size_t index) { return mProfiles.at(index).mShaders == first.mShaders; }))
                return;
            std::ostringstream names;
            for (std::size_t index : entries)
                names << " '" << mProfiles.at(index).mId << "'";
            Log(Debug::Warning) << "Location rendering profiles: contradictory priority " << priority << " at " << cell
                                << ':' << names.str()
                                << "; a tied winning match uses global settings (set distinct priorities)";
        };
        for (const auto& [priority, entries] : mNegativeLights)
            report("negative-light interiors", priority, entries);
        for (const auto& [cell, priorities] : mCells)
            for (const auto& [priority, entries] : priorities)
            {
                report(cell, priority, entries);
                if (const auto condition = mNegativeLights.find(priority); condition != mNegativeLights.end())
                {
                    auto combined = entries;
                    combined.insert(combined.end(), condition->second.begin(), condition->second.end());
                    report(cell + " when negative lighting is present", priority, combined);
                }
            }
    }

    const LocationRenderingProfile* LocationRenderingProfiles::find(
        std::string_view cellName, bool exterior, bool containsNegativeLight) const
    {
        if (exterior)
            return nullptr;
        const auto found = mCells.find(Misc::StringUtils::lowerCase(cellName));
        const Priorities* literal = found == mCells.end() ? nullptr : &found->second;
        const Priorities* conditional = containsNegativeLight && !mNegativeLights.empty() ? &mNegativeLights : nullptr;
        if (!literal && !conditional)
            return nullptr;

        const int priority = !literal ? conditional->rbegin()->first
            : !conditional            ? literal->rbegin()->first
                                      : std::max(literal->rbegin()->first, conditional->rbegin()->first);
        const LocationRenderingProfile* selected = nullptr;
        bool conflict = false;
        for (const Priorities* candidates : { literal, conditional })
        {
            if (!candidates)
                continue;
            const auto entries = candidates->find(priority);
            if (entries == candidates->end())
                continue;
            for (std::size_t index : entries->second)
            {
                const auto& profile = mProfiles.at(index);
                if (selected && selected->mShaders != profile.mShaders)
                    conflict = true;
                if (!selected || profile.mId < selected->mId)
                    selected = &profile;
            }
        }
        return conflict ? nullptr : selected;
    }
}
