#ifndef OPENMW_MWWORLD_PRELOADDEPENDENCIES_H
#define OPENMW_MWWORLD_PRELOADDEPENDENCIES_H

#include "esmstore.hpp"

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadweap.hpp>

#include <algorithm>
#include <deque>
#include <set>
#include <span>
#include <vector>

namespace MWWorld
{
    inline std::vector<VFS::Path::Normalized> snapshotPreloadModels(std::span<const VFS::Path::NormalizedView> models)
    {
        std::vector<VFS::Path::Normalized> result;
        result.reserve(models.size());
        for (const auto model : models)
            result.emplace_back(model);
        return result;
    }

    struct ActorPreloadModels
    {
        VFS::Path::NormalizedView mCommon, mMale, mFemale, mBeast, mArgonianSwim;
    };

    inline void appendActorAnimationPreloadModels(const ESM::NPC& npc, const ESM::Race& race,
        const ActorPreloadModels& paths, std::vector<VFS::Path::NormalizedView>& models)
    {
        const bool beast = race.mData.mFlags & ESM::Race::Beast;
        models.push_back(paths.mCommon);
        models.push_back(beast ? paths.mBeast : (npc.isMale() ? paths.mMale : paths.mFemale));
        if (!npc.mModel.empty())
            models.push_back(npc.mModel.getNormalized());
        if (beast && npc.mRace.contains("argonian"))
            models.push_back(paths.mArgonianSwim);
    }

    inline void appendActorAnimationPreloadModels(
        const ESM::Creature& creature, const ActorPreloadModels& paths, std::vector<VFS::Path::NormalizedView>& models)
    {
        if (creature.mFlags & ESM::Creature::Bipedal)
            models.push_back(paths.mCommon);
        if (!creature.mModel.empty())
            models.push_back(creature.mModel.getNormalized());
    }

    struct PreloadDependencyOptions
    {
        int mLevel = 1;
        bool mDelevelCreatures = false;
        bool mDelevelItems = false;
        std::size_t mMaxCandidates = 128;
        std::size_t mMaxRecords = 512;
        std::size_t mMaxEntries = 8192;
    };

    // Enumerate possible resources, never choose a spawn or consume randomness. A
    // bounded, breadth-first walk handles cycles and cross-kind nested lists. A
    // truncated prediction simply leaves the ordinary demand-loading path in place.
    inline std::vector<ESM::RefId> collectPreloadCandidates(
        const ESMStore& store, std::span<const ESM::RefId> roots, const PreloadDependencyOptions& options)
    {
        std::deque<ESM::RefId> pending;
        for (const auto& root : roots.first(std::min(roots.size(), options.mMaxRecords)))
            pending.push_back(root);
        std::set<ESM::RefId> visited;
        std::vector<ESM::RefId> result;
        std::size_t entriesLeft = options.mMaxEntries;
        while (!pending.empty() && visited.size() < options.mMaxRecords && result.size() < options.mMaxCandidates)
        {
            const auto id = pending.front();
            pending.pop_front();
            if (!visited.insert(id).second)
                continue;
            const ESM::LevelledListBase* list = store.get<ESM::CreatureLevList>().search(id);
            const bool creature = list != nullptr;
            if (!list)
                list = store.get<ESM::ItemLevList>().search(id);
            if (!list)
            {
                if (store.find(id))
                    result.push_back(id);
                continue;
            }
            if (list->mChanceNone >= 100 || list->mList.size() > entriesLeft)
                continue;
            entriesLeft -= list->mList.size();
            const bool delevel = creature ? options.mDelevelCreatures : options.mDelevelItems;
            const bool allLevels
                = list->mFlags & (creature ? int(ESM::CreatureLevList::AllLevels) : int(ESM::ItemLevList::AllLevels));
            int highest = 0;
            for (const auto& entry : list->mList)
            {
                const int level = delevel ? 1 : entry.mLevel;
                if (level <= options.mLevel)
                    highest = std::max(highest, level);
            }
            for (const auto& entry : list->mList)
            {
                const int level = delevel ? 1 : entry.mLevel;
                if (level <= options.mLevel && (allLevels || level == highest))
                    pending.push_back(entry.mId);
            }
        }
        return result;
    }

    inline void appendEquipmentPreloadModels(const ESMStore& store, const ESM::InventoryList& inventory, bool female,
        const PreloadDependencyOptions& options, std::vector<VFS::Path::NormalizedView>& models)
    {
        std::vector<ESM::RefId> roots;
        for (const auto& item : std::span(inventory.mList).first(std::min(inventory.mList.size(), options.mMaxRecords)))
        {
            // Negative counts are restocking entries, and can still supply equipment.
            if (item.mCount != 0)
                roots.push_back(item.mItem);
        }
        const auto appendParts = [&](const auto& parts) {
            for (const auto& part : parts)
            {
                const ESM::BodyPart* body = nullptr;
                if (female && !part.mFemale.empty())
                    body = store.get<ESM::BodyPart>().search(part.mFemale);
                if (!body && !part.mMale.empty())
                    body = store.get<ESM::BodyPart>().search(part.mMale);
                if (body && !body->mModel.empty())
                    models.push_back(body->mModel.getNormalized());
            }
        };
        for (const auto& id : collectPreloadCandidates(store, roots, options))
        {
            if (const auto* armor = store.get<ESM::Armor>().search(id))
                appendParts(armor->mParts.mParts);
            else if (const auto* clothing = store.get<ESM::Clothing>().search(id))
                appendParts(clothing->mParts.mParts);
            else if (const auto* weapon = store.get<ESM::Weapon>().search(id))
            {
                if (!weapon->mModel.empty())
                    models.push_back(weapon->mModel.getNormalized());
            }
            else if (const auto* light = store.get<ESM::Light>().search(id))
            {
                if (!light->mModel.empty())
                    models.push_back(light->mModel.getNormalized());
            }
        }
    }
}

#endif
