#include "apps/openmw/mwlua/magicbindingshelper.hpp"

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/npc.hpp"
#include "apps/openmw/mwworld/customdata.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

#include <components/esm3/loadnpc.hpp>
#include <components/esm3/readerscache.hpp>

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

            MWWorld::Ptr actor(bool observed = true, ESM::RefNum id = {})
            {
                ESM::CellRef ref;
                ref.blank();
                ref.mRefID = mNpc.mId;
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
                        const auto value = candidate ? MagicBindings::getActiveEffect(obj, lookup)
                                                     : baselineEffect(obj, lookup);
                        sum += value->param.getBase();
                    }
                const auto duration = std::chrono::duration<double, std::nano>(
                    std::chrono::steady_clock::now() - start);
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
