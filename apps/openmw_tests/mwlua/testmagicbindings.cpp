#include "apps/openmw/mwlua/context.hpp"
#include "apps/openmw/mwlua/magicbindings.hpp"
#include "apps/openmw/mwlua/magicbindingshelper.hpp"
#include "apps/openmw/mwlua/magicqueries.hpp"
#include "apps/openmw/mwlua/types/types.hpp"

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/npc.hpp"
#include "apps/openmw/mwworld/customdata.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

#include <components/esm3/loadnpc.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/misc/combinedperformance.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace MWLua
{
    namespace
    {
        // These are the old getStore/getEffect bodies, applied to the same actual
        // ObjectVariant/SafePtr and native MagicEffects used by the candidate.
        bool baselineIsActor(const ObjectVariant& actor)
        {
            return !actor.ptr().isEmpty() && actor.ptr().getClass().isActor();
        }

        MWMechanics::CreatureStats* baselineStats(const ObjectVariant& actor)
        {
            if (!baselineIsActor(actor))
                return nullptr;
            const MWWorld::Ptr& ptr = actor.ptr();
            return &ptr.getClass().getCreatureStats(ptr);
        }

        template <class GetEffectKey>
        sol::optional<ActiveEffect> baselineEffect(const ObjectVariant& actor, GetEffectKey&& getEffectKey)
        {
            if (!baselineIsActor(actor))
                return sol::nullopt;
            MWMechanics::EffectKey key = getEffectKey();
            if (auto* stats = baselineStats(actor))
                if (auto effect = stats->getMagicEffects().get(key))
                    return ActiveEffect{ key, effect.value() };
            return ActiveEffect{ key, MWMechanics::EffectParam() };
        }

        struct TestActorData : MWWorld::CustomData
        {
            MWMechanics::CreatureStats mStats;

            std::unique_ptr<MWWorld::CustomData> clone() const override
            {
                throw std::logic_error("The headless magic fixture does not clone actor data");
            }
        };

        struct Observations
        {
            int mActorChecks = 0;
            int mStatsReads = 0;
            bool mThrowStats = false;
            std::vector<std::string> mOrder;
        };

        // Controlled headless service boundary: real NPC initialization requires
        // a World and game records. The fixture supplies native CreatureStats and
        // observes validation/initialization ordering instead. The benchmark's
        // unobserved specialization has no counters or event recording.
        template <bool Observe>
        class TestActorClass final : public MWWorld::Class
        {
        public:
            bool mIsActor = true;
            Observations* mObservations = nullptr;

            TestActorClass()
                : MWWorld::Class(ESM::NPC::sRecordId)
            {
            }

            std::string_view getName(const MWWorld::ConstPtr&) const override { return "magic fixture"; }

            bool isActor() const override
            {
                if constexpr (Observe)
                    ++mObservations->mActorChecks;
                return mIsActor;
            }

            MWMechanics::CreatureStats& getCreatureStats(const MWWorld::Ptr& ptr) const override
            {
                if constexpr (Observe)
                {
                    ++mObservations->mStatsReads;
                    mObservations->mOrder.emplace_back("stats");
                    if (mObservations->mThrowStats)
                        throw std::runtime_error("fixture stats failure");
                }
                return static_cast<TestActorData*>(ptr.getRefData().getCustomData())->mStats;
            }
        };

        class MWLuaMagicBindingsTest : public testing::Test
        {
        protected:
            MWBase::Environment mEnvironment;
            MWWorld::ESMStore mStore;
            ESM::ReadersCache mReaders;
            MWWorld::WorldModel mWorld{ mStore, mReaders };
            VFS::Manager mVfs;
            ToUTF8::Utf8Encoder mEncoder{ ToUTF8::WINDOWS_1252 };
            std::unique_ptr<Resource::ResourceSystem> mResources;
            LuaUtil::LuaState mLua{ nullptr, nullptr };
            ESM::NPC mNpc;
            Observations mObservations;
            TestActorClass<true> mObservedClass;
            TestActorClass<false> mPlainClass;
            std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::NPC>>> mRefs;
            std::vector<std::unique_ptr<SelfObject>> mSelfObjects;
            std::uint32_t mNextRef = 1;

            void SetUp() override
            {
                MWClass::Npc::registerSelf();
                mEnvironment.setWorldModel(mWorld);
                mEnvironment.setESMStore(mStore);
                mNpc.blank();
                mNpc.mId = ESM::RefId::stringRefId("magic_fixture");
                mObservedClass.mObservations = &mObservations;
            }

            MWWorld::Ptr actor(bool observed = true, ESM::RefNum id = {}, ESM::RefId recordId = {})
            {
                ESM::CellRef ref;
                ref.blank();
                ref.mRefID = recordId.empty() ? mNpc.mId : recordId;
                ref.mRefNum = id.isSet() ? id : ESM::RefNum{ mNextRef++, 0 };
                auto live = std::make_unique<MWWorld::LiveCellRef<ESM::NPC>>(ref, &mNpc);
                live->mClass = observed ? static_cast<MWWorld::Class*>(&mObservedClass)
                                        : static_cast<MWWorld::Class*>(&mPlainClass);
                live->mData.setCustomData(std::make_unique<TestActorData>());
                MWWorld::Ptr ptr(live.get());
                mWorld.registerPtr(ptr);
                mRefs.push_back(std::move(live));
                return ptr;
            }

            ObjectVariant object(const MWWorld::Ptr& ptr, int context = 0)
            {
                sol::state_view lua = mLua.unsafeState();
                if (context == 1)
                    return ObjectVariant(sol::make_object(lua, GObject(ptr)));
                if (context == 2)
                {
                    auto self = std::make_unique<SelfObject>(LObject(ptr));
                    auto result = ObjectVariant(sol::make_object(lua, self.get()));
                    mSelfObjects.push_back(std::move(self));
                    return result;
                }
                return ObjectVariant(sol::make_object(lua, LObject(ptr)));
            }

            static MWMechanics::CreatureStats& stats(const MWWorld::Ptr& ptr)
            {
                return static_cast<TestActorData*>(ptr.getRefData().getCustomData())->mStats;
            }

            static MWMechanics::EffectKey key(std::string_view id = "fixture_effect", std::string_view arg = "")
            {
                return MWMechanics::EffectKey(
                    ESM::RefId::stringRefId(id), arg.empty() ? ESM::RefId{} : ESM::RefId::stringRefId(arg));
            }

            static ESM::ActiveEffect applied(std::string_view id, bool isApplied = true)
            {
                ESM::ActiveEffect result{};
                result.mEffectId = ESM::RefId::stringRefId(id);
                result.mMagnitude = 10.f;
                result.mDuration = 30.f;
                result.mTimeLeft = 20.f;
                result.mFlags = isApplied ? ESM::ActiveEffect::Flag_Applied : ESM::ActiveEffect::Flag_None;
                return result;
            }

            static ESM::ActiveSpells::ActiveSpellParams spell(
                ESM::RefNum caster, std::string_view id, std::vector<ESM::ActiveEffect> effects, bool temporary = true)
            {
                ESM::ActiveSpells::ActiveSpellParams result{};
                result.mActiveSpellId = ESM::RefId::stringRefId(id);
                result.mSourceSpellId = result.mActiveSpellId;
                result.mCaster = caster;
                result.mDisplayName = "Native query fixture spell with an owned display-name string";
                result.mFlags = temporary ? ESM::ActiveSpells::Flag_Temporary : ESM::ActiveSpells::Flag_SpellStore;
                result.mEffects = std::move(effects);
                return result;
            }

            static void setSpells(const MWWorld::Ptr& ptr, const ESM::ActiveSpells& state)
            {
                auto& spells = stats(ptr).getActiveSpells();
                spells = MWMechanics::ActiveSpells();
                spells.readState(state);
            }

            static void equal(const sol::optional<ActiveEffect>& lhs, const sol::optional<ActiveEffect>& rhs)
            {
                ASSERT_EQ(lhs.has_value(), rhs.has_value());
                if (lhs)
                {
                    EXPECT_EQ(lhs->key, rhs->key);
                    EXPECT_EQ(lhs->param.getBase(), rhs->param.getBase());
                    EXPECT_FLOAT_EQ(lhs->param.getModifier(), rhs->param.getModifier());
                }
            }

            template <class Function>
            static std::string exception(Function&& function)
            {
                try
                {
                    function();
                }
                catch (const std::runtime_error& e)
                {
                    return e.what();
                }
                return {};
            }
        };

        TEST_F(MWLuaMagicBindingsTest, readsExistingMissingAndArgumentEffectsWithoutCachingValues)
        {
            const auto ptr = actor();
            const auto obj = object(ptr);
            auto& effects = stats(ptr).getMagicEffects();
            effects.add(key(), MWMechanics::EffectParam(2.5f));
            effects.modifyBase(key(), 7);
            effects.add(key("fixture_effect", "fixture_skill"), MWMechanics::EffectParam(12.f));

            for (const auto& effectKey : { key(), key("missing"), key("fixture_effect", "fixture_skill") })
            {
                const auto lookup = [&] { return effectKey; };
                equal(baselineEffect(obj, lookup), MagicBindings::getActiveEffect(obj, lookup));
            }
            const auto lookup = [&] { return key(); };
            const auto before = MagicBindings::getActiveEffect(obj, lookup);
            effects.modifyBase(key(), 13);
            const auto after = MagicBindings::getActiveEffect(obj, lookup);
            ASSERT_TRUE(before);
            ASSERT_TRUE(after);
            EXPECT_EQ(before->param.getBase(), 7);
            EXPECT_EQ(after->param.getBase(), 20);
            equal(after, baselineEffect(obj, lookup));

            const auto missing = MagicBindings::getActiveEffect(obj, [&] { return key("missing"); });
            ASSERT_TRUE(missing);
            EXPECT_EQ(missing->param.getMagnitude(), 0.f);
        }

        TEST_F(MWLuaMagicBindingsTest, preservesLocalGlobalAndSelfObjectContexts)
        {
            const auto ptr = actor();
            for (int context = 0; context < 3; ++context)
            {
                const auto obj = object(ptr, context);
                EXPECT_EQ(obj.isLObject(), context == 0);
                EXPECT_EQ(obj.isGObject(), context == 1);
                EXPECT_EQ(obj.isSelfObject(), context == 2);
                equal(baselineEffect(obj, [&] { return key(); }),
                    MagicBindings::getActiveEffect(obj, [&] { return key(); }));
                EXPECT_EQ(MagicBindings::getActorStats(obj), &stats(ptr));
            }
        }

        TEST_F(MWLuaMagicBindingsTest, validatesActorOnceAndPreservesKeyBeforeStatsOrdering)
        {
            const auto obj = object(actor());
            const auto lookup = [&] {
                mObservations.mOrder.emplace_back("key");
                return key();
            };
            const auto before = baselineEffect(obj, lookup);
            EXPECT_EQ(mObservations.mActorChecks, 2);
            EXPECT_EQ(mObservations.mStatsReads, 1);
            EXPECT_EQ(mObservations.mOrder, (std::vector<std::string>{ "key", "stats" }));
            mObservations = {};
            const auto after = MagicBindings::getActiveEffect(obj, lookup);
            equal(before, after);
            EXPECT_EQ(mObservations.mActorChecks, 1);
            EXPECT_EQ(mObservations.mStatsReads, 1);
            EXPECT_EQ(mObservations.mOrder, (std::vector<std::string>{ "key", "stats" }));
        }

        TEST_F(MWLuaMagicBindingsTest, nonActorDoesNotResolveKeyOrStats)
        {
            const auto obj = object(actor());
            mObservedClass.mIsActor = false;
            const auto lookup = [&]() -> MWMechanics::EffectKey {
                ADD_FAILURE() << "Non-actor must return before effect key lookup";
                throw std::runtime_error("unexpected lookup");
            };
            EXPECT_FALSE(baselineEffect(obj, lookup));
            EXPECT_FALSE(MagicBindings::getActiveEffect(obj, lookup));
            EXPECT_EQ(baselineStats(obj), nullptr);
            EXPECT_EQ(MagicBindings::getActorStats(obj), nullptr);
            EXPECT_EQ(mObservations.mStatsReads, 0);
        }

        TEST_F(MWLuaMagicBindingsTest, unavailableActorRetainsExceptionBeforeKeyResolution)
        {
            const auto ptr = actor();
            const auto obj = object(ptr);
            const std::string expected = "Object is not available: " + ptr.getCellRef().getRefNum().toString();
            mWorld.deregisterLiveCellRef(*ptr.mRef);
            int keyReads = 0;
            const auto lookup = [&] {
                ++keyReads;
                return key();
            };
            EXPECT_EQ(exception([&] { baselineEffect(obj, lookup); }), expected);
            EXPECT_EQ(exception([&] { MagicBindings::getActiveEffect(obj, lookup); }), expected);
            EXPECT_EQ(exception([&] { baselineStats(obj); }), expected);
            EXPECT_EQ(exception([&] { MagicBindings::getActorStats(obj); }), expected);
            EXPECT_EQ(keyReads, 0);
            EXPECT_EQ(mObservations.mStatsReads, 0);
        }

        TEST_F(MWLuaMagicBindingsTest, keyFailurePrecedesPotentialStatsInitializationFailure)
        {
            const auto obj = object(actor());
            mObservations.mThrowStats = true;
            const auto lookup = [&]() -> MWMechanics::EffectKey {
                mObservations.mOrder.emplace_back("key");
                throw std::runtime_error("fixture key failure");
            };
            EXPECT_EQ(exception([&] { baselineEffect(obj, lookup); }), "fixture key failure");
            EXPECT_EQ(exception([&] { MagicBindings::getActiveEffect(obj, lookup); }), "fixture key failure");
            EXPECT_EQ(mObservations.mStatsReads, 0);
            EXPECT_EQ(mObservations.mOrder, (std::vector<std::string>{ "key", "key" }));
        }

        TEST_F(MWLuaMagicBindingsTest, statsFailureRemainsAfterSuccessfulKeyLookup)
        {
            const auto obj = object(actor());
            mObservations.mThrowStats = true;
            const auto lookup = [&] {
                mObservations.mOrder.emplace_back("key");
                return key();
            };
            EXPECT_EQ(exception([&] { baselineEffect(obj, lookup); }), "fixture stats failure");
            EXPECT_EQ(exception([&] { MagicBindings::getActiveEffect(obj, lookup); }), "fixture stats failure");
            EXPECT_EQ(mObservations.mOrder, (std::vector<std::string>{ "key", "stats", "key", "stats" }));
        }

        TEST_F(MWLuaMagicBindingsTest, registryReplacementIsObservedByTheNextQuery)
        {
            const auto first = actor();
            const auto obj = object(first);
            stats(first).getMagicEffects().modifyBase(key(), 11);
            const auto lookup = [&] { return key(); };
            ASSERT_EQ(MagicBindings::getActiveEffect(obj, lookup)->param.getBase(), 11);

            const auto replacement = actor(true, first.getCellRef().getRefNum());
            stats(replacement).getMagicEffects().modifyBase(key(), 29);
            EXPECT_EQ(MagicBindings::getActorStats(obj), &stats(replacement));
            ASSERT_EQ(MagicBindings::getActiveEffect(obj, lookup)->param.getBase(), 29);
            equal(baselineEffect(obj, lookup), MagicBindings::getActiveEffect(obj, lookup));

            mWorld.deregisterLiveCellRef(*replacement.mRef);
            EXPECT_FALSE(exception([&] { MagicBindings::getActorStats(obj); }).empty());
            mWorld.registerPtr(first);
            ASSERT_EQ(MagicBindings::getActiveEffect(obj, lookup)->param.getBase(), 11);
        }

        TEST_F(MWLuaMagicBindingsTest, statsHelperReturnsTheSameNativeCollections)
        {
            const auto ptr = actor();
            const auto obj = object(ptr);
            auto* expected = baselineStats(obj);
            auto* actual = MagicBindings::getActorStats(obj);
            ASSERT_EQ(actual, expected);
            EXPECT_EQ(&actual->getSpells(), &stats(ptr).getSpells());
            EXPECT_EQ(&actual->getActiveSpells(), &stats(ptr).getActiveSpells());
            EXPECT_EQ(&actual->getMagicEffects(), &stats(ptr).getMagicEffects());
        }

        TEST_F(MWLuaMagicBindingsTest, effectSourceQueryPreservesOrderDuplicatesAndImmutableSnapshots)
        {
            const auto caster = actor(true, {}, ESM::RefId::stringRefId("Player"));
            const auto other = actor();
            const auto victim = actor();
            ESM::ActiveSpells state;
            state.mSpells = {
                spell(getId(caster), "first",
                    { applied("charm"), applied("unapplied", false), applied("frenzyhumanoid") }),
                spell(getId(other), "other", { applied("other_effect") }),
                spell(getId(caster), "permanent", { applied("permanent_effect") }, false),
                spell(getId(caster), "last", { applied("charm") }),
            };
            setSpells(victim, state);
            MagicBindings::EffectSourceFilter filter;
            filter.mTemporary = true;
            filter.mCasterType = ESM::REC_INTERNAL_PLAYER;
            EffectSourceQuery query{ object(victim), MagicBindings::EffectSourceQueryData(filter), {}, {} };
            auto [ids, casters, changed] = query.read(sol::this_state{ mLua.unsafeState() });
            EXPECT_TRUE(changed);
            EXPECT_EQ(ids.get<std::string>(1), "charm");
            EXPECT_EQ(ids.get<std::string>(2), "frenzyhumanoid");
            EXPECT_EQ(ids.get<std::string>(3), "charm");
            EXPECT_EQ(casters.get<LObject>(1).id(), getId(caster));
            EXPECT_EQ(query.mData.values().size(), 3);
            auto [sameIds, sameCasters, sameChanged] = query.read(sol::this_state{ mLua.unsafeState() });
            EXPECT_FALSE(sameChanged);
            EXPECT_TRUE(ids == sameIds);
            EXPECT_TRUE(casters == sameCasters);

            // A source query deliberately does not expose ticking durations or
            // magnitudes. They cannot make an ID/caster snapshot stale.
            state.mSpells[0].mEffects[0].mTimeLeft = 3.f;
            state.mSpells[0].mEffects[0].mMagnitude = 17.f;
            setSpells(victim, state);
            EXPECT_FALSE(std::get<2>(query.read(sol::this_state{ mLua.unsafeState() })));

            state.mSpells.erase(state.mSpells.begin());
            setSpells(victim, state);
            auto [newIds, newCasters, newChanged] = query.read(sol::this_state{ mLua.unsafeState() });
            EXPECT_TRUE(newChanged);
            EXPECT_EQ(query.mData.values().size(), 1);
            EXPECT_EQ(newIds.get<std::string>(1), "charm");
            EXPECT_EQ(ids.get<std::string>(2), "frenzyhumanoid");
            EXPECT_TRUE(ids != newIds);
            sol::state_view lua(mLua.unsafeState());
            lua["oldEffectIds"] = ids;
            const auto result = lua.safe_script(
                "assert(not pcall(function() oldEffectIds[1] = 'changed' end))", sol::script_pass_on_error);
            EXPECT_TRUE(result.valid());
        }

        TEST_F(MWLuaMagicBindingsTest, effectSourceQueryObservesCasterAvailabilityAndAccessContext)
        {
            const auto caster = actor();
            const auto victim = actor();
            ESM::ActiveSpells state;
            state.mSpells = { spell(getId(caster), "source", { applied("charm") }) };
            setSpells(victim, state);
            for (int context = 0; context < 3; ++context)
            {
                EffectSourceQuery query{ object(victim, context), MagicBindings::EffectSourceQueryData({}), {}, {} };
                const auto result = query.read(sol::this_state{ mLua.unsafeState() });
                const auto& casters = std::get<1>(result);
                EXPECT_EQ(casters.get<sol::object>(1).is<GObject>(), context == 1);
                EXPECT_EQ(casters.get<sol::object>(1).is<LObject>(), context != 1);
            }
            EffectSourceQuery query{ object(victim), MagicBindings::EffectSourceQueryData({}), {}, {} };
            auto [ids, casters, changed] = query.read(sol::this_state{ mLua.unsafeState() });
            ASSERT_TRUE(changed);
            mWorld.deregisterLiveCellRef(*caster.mRef);
            const auto result = query.read(sol::this_state{ mLua.unsafeState() });
            EXPECT_TRUE(std::get<2>(result));
            EXPECT_EQ(std::get<0>(result).get<std::string>(1), "charm");
            EXPECT_TRUE(std::get<1>(result).get<sol::object>(1) == sol::nil);
            EXPECT_TRUE(casters.get<LObject>(1).ptrOrEmpty().isEmpty());
            mWorld.registerPtr(caster);
            EXPECT_TRUE(std::get<2>(query.read(sol::this_state{ mLua.unsafeState() })));
        }

        TEST_F(MWLuaMagicBindingsTest, effectSourceQuerySelectionDoesNotReorderOrDeduplicateResults)
        {
            const auto caster = actor();
            const auto victim = actor();
            ESM::ActiveSpells state;
            state.mSpells = {
                spell(getId(caster), "temporary", { applied("z"), applied("a"), applied("z") }),
                spell(getId(caster), "permanent", { applied("a") }, false),
            };
            setSpells(victim, state);
            MagicBindings::EffectSourceFilter filter;
            filter.mCaster = getId(caster);
            filter.mTemporary = true;
            filter.mEffectIds = std::vector{ ESM::RefId::stringRefId("z"), ESM::RefId::stringRefId("z") };
            EffectSourceQuery query{ object(victim), MagicBindings::EffectSourceQueryData(filter), {}, {} };
            const auto result = query.read(sol::this_state{ mLua.unsafeState() });
            ASSERT_EQ(query.mData.values().size(), 2);
            EXPECT_EQ(std::get<0>(result).get<std::string>(1), "z");
            EXPECT_EQ(std::get<0>(result).get<std::string>(2), "z");
            filter.mEffectIds = std::vector<ESM::RefId>{};
            EffectSourceQuery empty{ object(victim), MagicBindings::EffectSourceQueryData(filter), {}, {} };
            EXPECT_TRUE(std::get<2>(empty.read(sol::this_state{ mLua.unsafeState() })));
            EXPECT_TRUE(empty.mData.values().empty());
            EXPECT_FALSE(std::get<2>(empty.read(sol::this_state{ mLua.unsafeState() })));
            filter.mEffectIds.reset();
            filter.mTemporary = false;
            EffectSourceQuery permanent{ object(victim), MagicBindings::EffectSourceQueryData(filter), {}, {} };
            const auto permanentResult = permanent.read(sol::this_state{ mLua.unsafeState() });
            ASSERT_EQ(permanent.mData.values().size(), 1);
            EXPECT_EQ(std::get<0>(permanentResult).get<std::string>(1), "a");
        }

        TEST_F(MWLuaMagicBindingsTest, magnitudeQueryResolvesActorOnceAndObservesSameFrameMutations)
        {
            const auto ptr = actor();
            auto& effects = stats(ptr).getMagicEffects();
            effects.modifyBase(key("bound"), 7);
            effects.modifyBase(key("attribute", "luck"), 2);
            EffectMagnitudeQuery query{ object(ptr),
                MagicBindings::EffectMagnitudeQueryData(
                    { key("bound"), key("missing"), key("attribute", "luck"), key("bound") }),
                {} };
            mObservations = {};
            auto [values, changed] = query.read(sol::this_state{ mLua.unsafeState() });
            EXPECT_TRUE(changed);
            EXPECT_EQ(mObservations.mActorChecks, 1);
            EXPECT_EQ(mObservations.mStatsReads, 1);
            EXPECT_FLOAT_EQ(values.get<float>(1), 7.f);
            EXPECT_FLOAT_EQ(values.get<float>(2), 0.f);
            EXPECT_FLOAT_EQ(values.get<float>(3), 2.f);
            EXPECT_FLOAT_EQ(values.get<float>(4), 7.f);
            const auto unchanged = query.read(sol::this_state{ mLua.unsafeState() });
            EXPECT_FALSE(std::get<1>(unchanged));
            EXPECT_TRUE(values == std::get<0>(unchanged));
            effects.modifyBase(key("bound"), 5);
            auto [updated, updateChanged] = query.read(sol::this_state{ mLua.unsafeState() });
            EXPECT_TRUE(updateChanged);
            EXPECT_FLOAT_EQ(updated.get<float>(1), 12.f);
            EXPECT_FLOAT_EQ(values.get<float>(1), 7.f);

            const auto replacement = actor(true, getId(ptr));
            stats(replacement).getMagicEffects().modifyBase(key("bound"), 31);
            EXPECT_FLOAT_EQ(std::get<0>(query.read(sol::this_state{ mLua.unsafeState() })).get<float>(1), 31.f);
            mWorld.deregisterLiveCellRef(*replacement.mRef);
            EXPECT_FALSE(exception([&] { query.read(sol::this_state{ mLua.unsafeState() }); }).empty());
        }

        TEST_F(MWLuaMagicBindingsTest, nativeEffectPresenceMatchesLegacyIterationRatherThanTotalMagnitude)
        {
            const auto ptr = actor();
            const auto obj = object(ptr);
            auto& effects = stats(ptr).getMagicEffects();
            EXPECT_FALSE(MagicBindings::hasActiveEffects(obj));
            effects.modifyBase(key(), 0);
            EXPECT_FALSE(MagicBindings::hasActiveEffects(obj));
            effects.modifyBase(key(), 5);
            effects.add(key(), MWMechanics::EffectParam(-5.f));
            EXPECT_EQ(effects.getOrDefault(key()).getMagnitude(), 0.f);
            EXPECT_TRUE(MagicBindings::hasActiveEffects(obj));
            EffectMagnitudeQuery empty{ obj, MagicBindings::EffectMagnitudeQueryData({}), {} };
            EXPECT_TRUE(std::get<1>(empty.read(sol::this_state{ mLua.unsafeState() })));
            EXPECT_TRUE(empty.mData.values().empty());
            EXPECT_FALSE(std::get<1>(empty.read(sol::this_state{ mLua.unsafeState() })));
        }

        TEST_F(MWLuaMagicBindingsTest, nativeQueryFactoriesMatchLuaAdapterCallsAndMasterMode)
        {
            RecordProperty("combined_performance_mode", Misc::useCombinedPerformance() ? "enabled" : "disabled");
            const auto caster = actor(true, {}, ESM::RefId::stringRefId("Player"));
            const auto other = actor();
            const auto victim = actor();
            ESM::ActiveSpells state;
            state.mSpells = {
                spell(getId(caster), "first",
                    { applied("charm"), applied("unapplied", false), applied("frenzyhumanoid") }),
                spell(getId(other), "other", { applied("charm") }),
                spell(getId(caster), "permanent", { applied("charm") }, false),
            };
            setSpells(victim, state);
            stats(victim).getMagicEffects().modifyBase(key("bound"), 7);
            stats(victim).getMagicEffects().modifyBase(key("attribute", "luck"), 4);
            for (const auto id : { "charm", "frenzyhumanoid", "bound", "attribute", "missing" })
            {
                ESM::MagicEffect effect;
                effect.blank();
                effect.mId = ESM::RefId::stringRefId(id);
                if (effect.mId == "attribute")
                    effect.mData.mFlags = ESM::MagicEffect::TargetAttribute;
                mStore.insertStatic(effect);
            }

            Context context{ Context::Local };
            context.mLua = &mLua;
            sol::state_view lua = mLua.unsafeState();
            // Core magic registration also binds record icon properties. They
            // capture the resource VFS even though this test never loads an
            // image. Keep these real services alive until after Lua teardown.
            mResources = std::make_unique<Resource::ResourceSystem>(&mVfs, 1.0, &mEncoder.getStatelessEncoder());
            mEnvironment.setResourceSystem(*mResources);
            // These are the production registration functions, including
            // ActorStore userdata, not a mock of the new methods. The only
            // reduced service boundary is the type-package inventory: populate
            // its Player token after actor registration, as initTypesPackage
            // does, without constructing unrelated UI/journal services.
            lua["magic"] = initCoreMagicBindings(context);
            sol::table actorPackage(lua, sol::create);
            addActorMagicBindings(actorPackage, context);
            sol::table playerPackage = LuaUtil::makeReadOnly(sol::table(lua, sol::create));
            sol::table types(lua, sol::create);
            types["Actor"] = LuaUtil::makeReadOnly(actorPackage);
            types["Player"] = playerPackage;
            getPackageToTypeTable(lua)[playerPackage] = ESM::REC_INTERNAL_PLAYER;
            lua["types"] = LuaUtil::makeReadOnly(types);
            const auto self = object(victim, 2);
            lua["actorObject"] = self.asSelfObject();
            lua["casterObject"] = LObject(caster);
            lua["combinedEnabled"] = Misc::useCombinedPerformance();
            const auto result = lua.safe_script(R"(
                local spells = types.Actor.activeSpells(actorObject)
                local effects = types.Actor.activeEffects(actorObject)
                local supported, createSource = pcall(function() return spells.createEffectSourceQuery end)
                local hasMagnitude, createMagnitude = pcall(function() return effects.createMagnitudeQuery end)
                local hasPresence, presence = pcall(function() return effects.hasEffects end)
                assert(effects:getEffect('bound').magnitude == 7)
                local spellCount = 0
                for _, spell in pairs(spells) do
                    spellCount = spellCount + 1
                    assert(spell.effects[1].id == 'charm')
                end
                assert(spellCount == 3)
                if not combinedEnabled then
                    assert(not (supported and type(createSource) == 'function'))
                    assert(not (hasMagnitude and type(createMagnitude) == 'function'))
                    assert(not (hasPresence and type(presence) == 'function'))
                    return
                end
                assert(supported and type(createSource) == 'function')
                assert(hasMagnitude and type(createMagnitude) == 'function')
                assert(hasPresence and type(presence) == 'function')
                assert(effects:hasEffects())
                local source = createSource(spells, {temporary=true, casterType=types.Player})
                local ids, casters, changed = source:read()
                assert(changed)
                local count = 0
                for i, id in ipairs(ids) do
                    count = count + 1
                    assert(id == ({'charm', 'frenzyhumanoid'})[i])
                    assert(casters[i] ~= nil)
                end
                assert(count == 2)
                sourceCasters = casters
                local sameIds, sameCasters, sameChanged = source:read()
                assert(not sameChanged and sameIds == ids and sameCasters == casters)
                assert(not pcall(function() ids[1] = 'altered' end))
                local filtered = spells:createEffectSourceQuery({
                    temporary=true, caster=casterObject, effectIds={'frenzyhumanoid'}
                })
                local selected = filtered:read()
                assert(selected[1] == 'frenzyhumanoid' and selected[2] == nil)
                local magnitude = effects:createMagnitudeQuery({
                    'bound', 'missing', {id='attribute', argument='luck'}, 'bound'
                })
                assert(#magnitude == 4)
                local values, valuesChanged = magnitude:read()
                assert(valuesChanged and values[1] == 7 and values[2] == 0
                    and values[3] == 4 and values[4] == 7)
                local sameValues, changedAgain = magnitude:read()
                assert(not changedAgain and sameValues == values)
                assert(not pcall(function() values[1] = 999 end))
                assert(not pcall(function() spells:createEffectSourceQuery({casterType={}}) end))
                assert(not pcall(function() spells:createEffectSourceQuery({effectIds={'not_a_record'}}) end))
                assert(not pcall(function() effects:createMagnitudeQuery({'not_a_record'}) end))
            )",
                sol::script_pass_on_error);
            if (!result.valid())
            {
                const auto error = result.get<sol::error>();
                FAIL() << error.what();
            }
            if (Misc::useCombinedPerformance())
            {
                const sol::table casters = lua["sourceCasters"];
                EXPECT_EQ(casters.get<LObject>(1).id(), getId(caster));
                EXPECT_EQ(casters.get<LObject>(2).id(), getId(caster));
            }
        }

        TEST_F(MWLuaMagicBindingsTest, nativeEffectSourceQueryBenchmark)
        {
            // SaneMagic-shaped native/Lua materialization workload. The legacy
            // model copies spell params and builds the applied-effect tables,
            // but omits Lua dispatch/property access: it is not a whole-engine
            // or FPS benchmark, and no timing threshold decides correctness.
            constexpr int actorCount = 72;
            constexpr int rounds = 5;
            constexpr int cycles = 64;
            const auto player = actor(false, {}, ESM::RefId::stringRefId("Player"));
            const auto other = actor(false);
            std::vector<MWWorld::Ptr> victims;
            std::vector<EffectSourceQuery> queries;
            MagicBindings::EffectSourceFilter filter;
            filter.mTemporary = true;
            filter.mCasterType = ESM::REC_INTERNAL_PLAYER;
            for (int i = 0; i < actorCount; ++i)
            {
                const auto victim = actor(false);
                ESM::ActiveSpells state;
                for (int j = 0; j < 6; ++j)
                    state.mSpells.push_back(spell(getId(j % 2 == 0 ? player : other), "spell_" + std::to_string(j),
                        { applied("charm"), applied("frenzyhumanoid"), applied("unapplied", false), applied("other") },
                        j % 3 != 0));
                setSpells(victim, state);
                victims.push_back(victim);
                queries.push_back({ object(victim), MagicBindings::EffectSourceQueryData(filter), {}, {} });
            }
            sol::state_view lua(mLua.unsafeState());
            const auto run = [&](bool candidate, int iterations) {
                std::uint64_t count = 0;
                const auto start = std::chrono::steady_clock::now();
                for (int cycle = 0; cycle < iterations; ++cycle)
                    for (int i = 0; i < actorCount; ++i)
                    {
                        if (candidate)
                        {
                            const auto snapshot = queries[i].read(sol::this_state{ mLua.unsafeState() });
                            count += queries[i].mData.values().size();
                        }
                        else
                        {
                            for (const auto& current : stats(victims[i]).getActiveSpells())
                            {
                                const auto params = current;
                                if (!params.hasFlag(ESM::ActiveSpells::Flag_Temporary)
                                    || params.getCaster() != getId(player))
                                    continue;
                                sol::table effects(lua, sol::create);
                                std::size_t index = 0;
                                for (const auto& effect : params.getEffects())
                                    if (effect.mFlags & ESM::ActiveEffect::Flag_Applied)
                                        effects[++index] = effect;
                                count += index;
                            }
                        }
                    }
                const double ns
                    = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
                lua_gc(mLua.unsafeState(), LUA_GCCOLLECT, 0);
                return std::pair{ ns / (actorCount * iterations), count };
            };
            EXPECT_EQ(run(false, 2).second, run(true, 2).second);
            std::vector<double> before, after;
            for (int round = 0; round < rounds; ++round)
                for (int step = 0; step < 2; ++step)
                {
                    const bool candidate = (round + step) % 2 != 0;
                    const auto [ns, count] = run(candidate, cycles);
                    EXPECT_EQ(count, static_cast<std::uint64_t>(6 * actorCount * cycles));
                    (candidate ? after : before).push_back(ns);
                }
            const auto record = [&](const char* name, std::vector<double> values) {
                std::ostringstream samples;
                for (std::size_t i = 0; i < values.size(); ++i)
                    samples << (i ? "," : "") << values[i];
                RecordProperty(std::string(name) + "_samples_ns_per_actor", samples.str());
                std::sort(values.begin(), values.end());
                RecordProperty(std::string(name) + "_median_ns_per_actor", std::to_string(values[values.size() / 2]));
            };
            record("legacy_materialization_model", before);
            record("compiled_query", after);
            RecordProperty(
                "scope", "72 actors x six spells x four effects; unchanged result; no Lua callback dispatch or FPS");
        }

        TEST_F(MWLuaMagicBindingsTest, nativeHelperBenchmark)
        {
            // Native helper microbenchmark, not an engine/FPS benchmark. The
            // Class stats service and effect-ID lookup are controlled boundaries;
            // there is no Lua call/return dispatch, profiler, or game simulation.
            // Both implementations use real ObjectVariant/SafePtr/WorldModel and
            // MagicEffects. Build Release/LTO for useful CSE comparisons. Timings
            // are recorded only; correctness never depends on a timing threshold.
            constexpr int actorCount = 160;
            constexpr int rounds = 7;
            constexpr int cycles = 512;
            std::vector<ObjectVariant> objects;
            objects.reserve(actorCount);
            const auto effectKey = key("measured_effect");
            for (int i = 0; i < actorCount; ++i)
            {
                const auto ptr = actor(false);
                for (int j = 0; j < 16; ++j)
                    stats(ptr).getMagicEffects().modifyBase(key("effect_" + std::to_string(j)), j);
                stats(ptr).getMagicEffects().modifyBase(effectKey, i + 1);
                objects.push_back(object(ptr, i % 2));
            }
            const auto lookup = [&] { return effectKey; };
            const auto run = [&](bool candidate, int count) {
                std::uint64_t sum = 0;
                const auto start = std::chrono::steady_clock::now();
                for (int cycle = 0; cycle < count; ++cycle)
                    for (const auto& obj : objects)
                    {
                        const auto value
                            = candidate ? MagicBindings::getActiveEffect(obj, lookup) : baselineEffect(obj, lookup);
                        sum += value->param.getBase();
                    }
                const auto duration
                    = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start);
                return std::pair{ duration.count() / (count * actorCount), sum };
            };
            const auto expected = static_cast<std::uint64_t>(actorCount * (actorCount + 1) / 2) * cycles;
            EXPECT_EQ(run(false, 4).second, run(true, 4).second);
            std::vector<double> baselineNs;
            std::vector<double> candidateNs;
            for (int round = 0; round < rounds; ++round)
                for (int step = 0; step < 2; ++step)
                {
                    const bool candidate = ((round + step) % 2) != 0;
                    const auto [elapsed, sum] = run(candidate, cycles);
                    EXPECT_EQ(sum, expected);
                    (candidate ? candidateNs : baselineNs).push_back(elapsed);
                }
            const auto record = [&](const char* name, std::vector<double> values) {
                std::ostringstream samples;
                for (std::size_t i = 0; i < values.size(); ++i)
                    samples << (i == 0 ? "" : ",") << values[i];
                RecordProperty(std::string(name) + "_samples_ns_per_query", samples.str());
                std::sort(values.begin(), values.end());
                RecordProperty(std::string(name) + "_median_ns_per_query", std::to_string(values[values.size() / 2]));
            };
            record("baseline", baselineNs);
            record("candidate", candidateNs);
            RecordProperty("scope", "native helper; controlled stats and key services; no Lua dispatch or game FPS");
            RecordProperty("actors", actorCount);
            RecordProperty("queries_per_round", actorCount * cycles);
        }
    }
}
