#include "corebindings.hpp"

#include <chrono>
#include <cmath>
#include <algorithm>
#include <istream>
#include <components/resource/resourcesystem.hpp>
#include <components/vfs/manager.hpp>
#include <components/misc/combinedperformance.hpp>
#include <components/lua/pureworker.hpp>
#include "jobbindings.hpp"
#include <stdexcept>

#include <components/debug/debuglog.hpp>
#include <components/lua/l10n.hpp>
#include <components/lua/luastate.hpp>
#include <components/lua/serialization.hpp>
#include <components/lua/util.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/settings/values.hpp>
#include <components/version/version.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/datetimemanager.hpp"
#include "../mwworld/esmstore.hpp"

#include "context.hpp"
#include "coremwscriptbindings.hpp"
#include "dialoguebindings.hpp"
#include "factionbindings.hpp"
#include "landbindings.hpp"
#include "luaevents.hpp"
#include "magicbindings.hpp"
#include "regionbindings.hpp"
#include "soundbindings.hpp"
#include "stats.hpp"
#include "weatherbindings.hpp"

namespace MWLua
{
    static sol::table initContentFilesBindings(sol::state_view& lua)
    {
        const std::vector<std::string>& contentList = MWBase::Environment::get().getWorld()->getContentFiles();
        sol::table list(lua, sol::create);
        for (size_t i = 0; i < contentList.size(); ++i)
            list[LuaUtil::toLuaIndex(i)] = Misc::StringUtils::lowerCase(contentList[i]);
        sol::table res(lua, sol::create);
        res["list"] = LuaUtil::makeReadOnly(list);
        res["indexOf"] = [&contentList](std::string_view contentFile) -> sol::optional<int64_t> {
            for (size_t i = 0; i < contentList.size(); ++i)
                if (Misc::StringUtils::ciEqual(contentList[i], contentFile))
                    return LuaUtil::toLuaIndex(static_cast<int64_t>(i));
            return sol::nullopt;
        };
        res["has"] = [&contentList](std::string_view contentFile) -> bool {
            for (size_t i = 0; i < contentList.size(); ++i)
                if (Misc::StringUtils::ciEqual(contentList[i], contentFile))
                    return true;
            return false;
        };
        return LuaUtil::makeReadOnly(res);
    }

    void addCoreTimeBindings(sol::table& api, const Context& context)
    {
        MWWorld::DateTimeManager* timeManager = MWBase::Environment::get().getWorld()->getTimeManager();

        api["getSimulationTime"] = [timeManager]() { return timeManager->getSimulationTime(); };
        api["getSimulationTimeScale"] = [timeManager]() { return timeManager->getSimulationTimeScale(); };
        api["getGameTime"] = [timeManager]() { return timeManager->getGameTime(); };
        api["getGameTimeScale"] = [timeManager]() { return timeManager->getGameTimeScale(); };
        api["isWorldPaused"] = [timeManager]() { return timeManager->isPaused(); };
        api["getRealTime"] = []() {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        };

        if (context.mType != Context::Global)
            api["getRealFrameDuration"] = []() { return MWBase::Environment::get().getFrameDuration(); };
    }

    void addIsolatedWorkerBindings(sol::table& api, const Context& context)
    {
        auto lua = context.sol();
        auto* jobs = context.mNativeQueryService;
        if (jobs && Misc::useIsolatedLuaWorkers() && Settings::general().mNativeJobsEnabled
            && (context.mType == Context::Global || context.mType == Context::Local))
        {
            using Numbers = std::vector<double>;
            using Ticket = JobTicket<Numbers>;
            if (context.initializeOnce("OpenMW_IsolatedWorkerTicket"))
            {
                auto type = lua.new_usertype<Ticket>("IsolatedWorkerTicket", sol::no_constructor);
                bindTicket<Numbers>(type, *jobs, [](sol::state_view view, Numbers values) {
                    return sol::make_object(view, sol::as_table(std::move(values)));
                });
            }
            const auto define = [jobs](std::string source, sol::this_state state) {
                if (source.empty() || source.size() > LuaUtil::maxWorkerSource || source.front() == '\x1b')
                    throw std::invalid_argument("defineWorker expects a Lua text chunk up to 32768 bytes");
                auto submit = [jobs, text = std::make_shared<const std::string>(std::move(source))](const sol::object& object)
                    -> sol::optional<Ticket> {
                    if (!jobs->mAsync.hasRoom())
                        return sol::nullopt;
                    if (object.get_type() != sol::type::table)
                        throw std::invalid_argument("worker input must be a number array");
                    const auto input = object.as<sol::table>();
                    const auto count = rawArraySize(input);
                    if (count > LuaUtil::maxWorkerValues)
                        throw std::invalid_argument("worker input exceeds item limit");
                    Numbers values(count);
                    std::size_t keys = 0;
                    input.for_each([&](sol::object key, sol::object value) {
                        if (key.get_type() != sol::type::number || value.get_type() != sol::type::number)
                            throw std::invalid_argument("worker inputs must be dense number arrays");
                        const double i = key.as<double>();
                        if (!(i >= 1 && i <= count && i == std::floor(i)))
                            throw std::invalid_argument("worker input has an invalid array index");
                        values[static_cast<std::size_t>(i) - 1] = value.as<double>();
                        ++keys;
                    });
                    if (keys != count)
                        throw std::invalid_argument("worker input has missing array entries");
                    auto job = jobs->mAsync.submit<Numbers>([text, data = std::move(values)](const auto& cancelled) {
                        return LuaUtil::runPureWorker(*text, data, cancelled);
                    });
                    if (!job)
                        return sol::nullopt;
                    ++jobs->mLuaSubmitted;
                    return Ticket{ std::move(job) };
                };
                return sol::make_object(sol::state_view(state), sol::as_function(std::move(submit)));
            };
            api["defineWorker"] = define;
            api["loadWorker"] = [define](std::string module, sol::this_state state) {
                if (module.size() > 256 || !module.starts_with("scripts.") || module.ends_with('.')
                    || module.find("..") != std::string::npos
                    || module.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos)
                    throw std::invalid_argument("loadWorker expects a script module name under scripts");
                std::replace(module.begin(), module.end(), '.', '/');
                auto file = MWBase::Environment::get().getResourceSystem()->getVFS()->get(VFS::Path::Normalized(module + ".lua"));
                std::string source(LuaUtil::maxWorkerSource + 1, ' ');
                file->read(source.data(), static_cast<std::streamsize>(source.size()));
                if (file->bad())
                    throw std::runtime_error("could not read worker module: " + module);
                source.resize(static_cast<std::size_t>(file->gcount()));
                return define(std::move(source), state);
            };
        }
    }

    sol::table initCorePackage(const Context& context)
    {
        auto lua = context.sol();
        sol::object cached = context.getTypePackage("openmw_core");
        if (cached != sol::nil)
            return cached;

        sol::table api(lua, sol::create);
        addIsolatedWorkerBindings(api, context);
        api["API_REVISION"] = Version::getLuaApiRevision(); // specified in CMakeLists.txt
        api["contentFiles"] = initContentFilesBindings(lua);
        api["getFormId"] = [](std::string_view contentFile, unsigned int index) -> std::string {
            const std::vector<std::string>& contentList = MWBase::Environment::get().getWorld()->getContentFiles();
            for (size_t i = 0; i < contentList.size(); ++i)
                if (Misc::StringUtils::ciEqual(contentList[i], contentFile))
                    return ESM::RefId(ESM::FormId{ index, int(i) }).serializeText();
            throw std::runtime_error("Content file not found: " + std::string(contentFile));
        };
        if (context.mType != Context::Load)
        {
            api["quit"] = [lua = context.mLua]() {
                Log(Debug::Warning) << "Quit requested by a Lua script.\n" << lua->debugTraceback();
                MWBase::Environment::get().getStateManager()->requestQuit();
            };
            addCoreTimeBindings(api, context);

            api["magic"]
                = context.cachePackage("openmw_core_magic", [context]() { return initCoreMagicBindings(context); });

            api["stats"]
                = context.cachePackage("openmw_core_stats", [context]() { return initCoreStatsBindings(context); });

            api["mwscripts"] = context.cachePackage(
                "openmw_core_mwscripts", [context]() { return initCoreMwScriptBindings(context); });

            api["land"]
                = context.cachePackage("openmw_core_land", [context]() { return initCoreLandBindings(context); });

            api["weather"]
                = context.cachePackage("openmw_core_weather", [context]() { return initCoreWeatherBindings(context); });

            api["factions"] = context.cachePackage(
                "openmw_core_factions", [context]() { return initCoreFactionBindings(context); });
            api["regions"]
                = context.cachePackage("openmw_core_regions", [context]() { return initCoreRegionBindings(context); });
            api["dialogue"] = context.cachePackage(
                "openmw_core_dialogue", [context]() { return initCoreDialogueBindings(context); });

            const MWWorld::Store<ESM::GameSetting>* gmstStore
                = &MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();
            api["getGMST"] = [lua, gmstStore](const std::string& setting) -> sol::object {
                const ESM::GameSetting* gmst = gmstStore->search(setting);
                if (gmst == nullptr)
                    return sol::nil;
                const ESM::Variant& value = gmst->mValue;
                switch (value.getType())
                {
                    case ESM::VT_Float:
                        return sol::make_object<float>(lua, value.getFloat());
                    case ESM::VT_Short:
                    case ESM::VT_Long:
                    case ESM::VT_Int:
                        return sol::make_object<int>(lua, value.getInteger());
                    case ESM::VT_String:
                        return sol::make_object<std::string>(lua, value.getString());
                    case ESM::VT_Unknown:
                    case ESM::VT_None:
                        break;
                }
                return sol::nil;
            };

            if (context.mType != Context::Menu)
            {
                api["sendGlobalEvent"] = [context](std::string eventName, const sol::object& eventData) {
                    context.mLuaEvents->addGlobalEvent(
                        { std::move(eventName), LuaUtil::serialize(eventData, context.mSerializer) });
                };
                api["sound"]
                    = context.cachePackage("openmw_core_sound", [context]() { return initCoreSoundBindings(context); });
            }
            else
            {
                api["sendGlobalEvent"] = [context](std::string eventName, const sol::object& eventData) {
                    if (MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_NoGame)
                    {
                        throw std::logic_error("Can't send global events when no game is loaded");
                    }
                    context.mLuaEvents->addGlobalEvent(
                        { std::move(eventName), LuaUtil::serialize(eventData, context.mSerializer) });
                };
            }
        }
        api["l10n"] = context.cachePackage("openmw_core_l10n",
            [lua]() { return LuaUtil::initL10nLoader(lua, MWBase::Environment::get().getL10nManager()); });

        api["getGameDifficulty"] = []() { return Settings::game().mDifficulty.get(); };

        sol::table readOnlyApi = LuaUtil::makeReadOnly(api);
        return context.setTypePackage(readOnlyApi, "openmw_core");
    }
}
