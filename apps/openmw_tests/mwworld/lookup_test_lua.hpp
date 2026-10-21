#ifndef OPENMW_TESTS_MWWORLD_LOOKUP_LUA_HPP
#define OPENMW_TESTS_MWWORLD_LOOKUP_LUA_HPP

#include "apps/openmw/mwbase/luamanager.hpp"
#include <stdexcept>

namespace MWWorld
{
    // Cell serialization tests have no Lua scripts. Any other use is an error.
    class LookupTestLuaManager final : public MWBase::LuaManager
    {
    public:
        void contentFilesLoaded() override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: contentFilesLoaded");
        }
        void newGameStarted() override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: newGameStarted");
        }
        void gameLoaded() override { throw std::logic_error("Unexpected Lua call in cell lookup test: gameLoaded"); }
        void gameEnded() override { throw std::logic_error("Unexpected Lua call in cell lookup test: gameEnded"); }
        void noGame() override { throw std::logic_error("Unexpected Lua call in cell lookup test: noGame"); }
        void objectAddedToScene(const MWWorld::Ptr& ptr) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: objectAddedToScene");
        }
        void objectRemovedFromScene(const MWWorld::Ptr& ptr) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: objectRemovedFromScene");
        }
        void objectTeleported(const MWWorld::Ptr& ptr) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: objectTeleported");
        }
        void itemConsumed(const MWWorld::Ptr& consumable, const MWWorld::Ptr& actor) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: itemConsumed");
        }
        void objectDropped(const MWWorld::Ptr& object, const MWWorld::Ptr& actor, const osg::Vec3f& position,
            const osg::Quat& rotation) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: objectDropped");
        }
        void objectPlaced(const MWWorld::Ptr& object, const MWWorld::Ptr& actor, const osg::Vec3f& position,
            const osg::Quat& rotation) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: objectPlaced");
        }
        void objectActivated(const MWWorld::Ptr& object, const MWWorld::Ptr& actor) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: objectActivated");
        }
        void useItem(const MWWorld::Ptr& object, const MWWorld::Ptr& actor, bool force) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: useItem");
        }
        void animationTextKey(const MWWorld::Ptr& actor, const std::string& key) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: animationTextKey");
        }
        void playAnimation(const MWWorld::Ptr& object, const std::string& groupname,
            const MWRender::AnimPriority& priority, int blendMask, bool autodisable, float speedmult,
            std::string_view start, std::string_view stop, float startpoint, uint32_t loops, bool loopfallback) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: playAnimation");
        }
        void animationEnded(const MWWorld::Ptr& actor, std::string_view groupname, float time, float completion,
            std::string_view startKey, std::string_view stopKey) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: animationEnded");
        }
        void jailTimeServed(const MWWorld::Ptr& actor, int days) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: jailTimeServed");
        }
        void skillLevelUp(const MWWorld::Ptr& actor, ESM::RefId skillId, std::string_view source) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: skillLevelUp");
        }
        void skillUse(const MWWorld::Ptr& actor, ESM::RefId skillId, int useType, float scale) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: skillUse");
        }
        void onHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const MWWorld::Ptr& weapon,
            const MWWorld::Ptr& ammo, int attackType, float attackStrength, float attackWindUp, float damage,
            bool isHealth, const osg::Vec3f& hitPos, bool successful, MWMechanics::DamageSourceType) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: onHit");
        }
        void exteriorCreated(MWWorld::CellStore& cell) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: exteriorCreated");
        }
        void actorDied(const MWWorld::Ptr& actor) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: actorDied");
        }
        void onDialogueResponse(
            const MWWorld::Ptr& actor, const ESM::DialInfo& info, const ESM::Dialogue& record) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: onDialogueResponse");
        }
        void questUpdated(const ESM::RefId& questId, int stage) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: questUpdated");
        }
        void uiModeChanged(const MWWorld::Ptr& arg) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: uiModeChanged");
        }
        void viewportResized(int width, int height) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: viewportResized");
        }
        void savePermanentStorage(const std::filesystem::path& userConfigPath) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: savePermanentStorage");
        }
        void applyMagicEffects(ESM::RefId id, const MWWorld::Ptr& caster, ESM::RefNum item, const MWWorld::Ptr& target,
            const std::vector<int>& effects, bool ignoreReflect, bool ignoreSpellAbsorption, bool stackable,
            bool isReflect) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: applyMagicEffects");
        }
        void magicProjectileHit(ESM::RefId spellId, const MWWorld::Ptr& caster, ESM::RefNum item,
            const MWWorld::Ptr& victim, const osg::Vec3f& position, const osg::Vec3f& normal) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: magicProjectileHit");
        }
        void inputEvent(const InputEvent& event) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: inputEvent");
        }
        ActorControls* getActorControls(const MWWorld::Ptr&) const override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: getActorControls");
        }
        void clear() override { throw std::logic_error("Unexpected Lua call in cell lookup test: clear"); }
        void setupPlayer(const MWWorld::Ptr&) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: setupPlayer");
        }
        void write(ESM::ESMWriter& writer, Loading::Listener& progress) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: write");
        }
        void saveLocalScripts(const MWWorld::Ptr& ptr, ESM::LuaScripts& data) override {}
        void applyDelayedActions() override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: applyDelayedActions");
        }
        void readRecord(ESM::ESMReader& reader, uint32_t type) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: readRecord");
        }
        void loadLocalScripts(const MWWorld::Ptr& ptr, const ESM::LuaScripts& data) override {}
        void setContentFileMapping(const std::map<int, int>&) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: setContentFileMapping");
        }
        void reloadAllScripts() override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: reloadAllScripts");
        }
        void handleConsoleCommand(
            const std::string& consoleMode, const std::string& command, const MWWorld::Ptr& selectedPtr) override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: handleConsoleCommand");
        }
        std::string formatResourceUsageStats() const override
        {
            throw std::logic_error("Unexpected Lua call in cell lookup test: formatResourceUsageStats");
        }
    };
}

#endif
