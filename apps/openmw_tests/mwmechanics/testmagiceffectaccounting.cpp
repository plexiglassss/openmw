#include <gtest/gtest.h>

#include <vector>

#include <components/esm3/activespells.hpp>
#include <components/esm3/loadmgef.hpp>

#include "../../openmw/mwmechanics/magiceffectaccounting.hpp"

namespace
{
    using namespace MWMechanics::MagicEffectAccounting;

    TEST(MagicEffectAccountingTest, TracksSuccessfullyAppliedMagnitude)
    {
        EXPECT_TRUE(shouldTrackMagnitude(ESM::ActiveEffect::Flag_None,
            ESM::ActiveEffect::Flag_Remove | ESM::ActiveEffect::Flag_Applied));
    }

    TEST(MagicEffectAccountingTest, RejectsNeverAppliedMagnitude)
    {
        EXPECT_FALSE(shouldTrackMagnitude(ESM::ActiveEffect::Flag_None, ESM::ActiveEffect::Flag_Remove));
        EXPECT_FALSE(shouldTrackMagnitude(
            ESM::ActiveEffect::Flag_None, ESM::ActiveEffect::Flag_Remove | ESM::ActiveEffect::Flag_Invalid));
    }

    TEST(MagicEffectAccountingTest, KeepsAccountingForAnEffectThatWasPreviouslyApplied)
    {
        EXPECT_TRUE(shouldTrackMagnitude(ESM::ActiveEffect::Flag_Applied, ESM::ActiveEffect::Flag_Remove));
        EXPECT_TRUE(shouldTrackMagnitude(
            ESM::ActiveEffect::Flag_Applied | ESM::ActiveEffect::Flag_Remove, ESM::ActiveEffect::Flag_Remove));
    }

    TEST(MagicEffectAccountingTest, RebuildIgnoresRejectedDrainMagnitudeFromOldSave)
    {
        std::vector<ESM::ActiveEffect> effects(4);
        effects[0].mEffectId = ESM::MagicEffect::DrainHealth;
        effects[0].mMagnitude = 12.f;
        effects[0].mFlags = ESM::ActiveEffect::Flag_Applied | ESM::ActiveEffect::Flag_Remove;
        effects[1].mEffectId = ESM::MagicEffect::DrainHealth;
        effects[1].mMagnitude = 3587.f;
        effects[1].mFlags = ESM::ActiveEffect::Flag_Remove;
        effects[2].mEffectId = ESM::MagicEffect::DrainHealth;
        effects[2].mMagnitude = 42.f;
        effects[2].mFlags = ESM::ActiveEffect::Flag_Remove | ESM::ActiveEffect::Flag_Invalid;
        effects[3].mEffectId = ESM::MagicEffect::FortifyHealth;
        effects[3].mMagnitude = 99.f;
        effects[3].mFlags = ESM::ActiveEffect::Flag_Applied;

        EXPECT_FLOAT_EQ(getAppliedMagnitude(effects, ESM::MagicEffect::DrainHealth), 12.f);
        EXPECT_FLOAT_EQ(getAppliedMagnitude(effects, ESM::MagicEffect::DrainMagicka), 0.f);
    }

    TEST(MagicEffectAccountingTest, IdentifiesEveryGodModeSuppressedDrainAndAbsorbEffect)
    {
        EXPECT_TRUE(isGodModeSuppressedEffect(ESM::MagicEffect::DrainHealth));
        EXPECT_TRUE(isGodModeSuppressedEffect(ESM::MagicEffect::DrainMagicka));
        EXPECT_TRUE(isGodModeSuppressedEffect(ESM::MagicEffect::DrainFatigue));
        EXPECT_TRUE(isGodModeSuppressedEffect(ESM::MagicEffect::DrainAttribute));
        EXPECT_TRUE(isGodModeSuppressedEffect(ESM::MagicEffect::DrainSkill));
        EXPECT_TRUE(isGodModeSuppressedEffect(ESM::MagicEffect::AbsorbHealth));
        EXPECT_TRUE(isGodModeSuppressedEffect(ESM::MagicEffect::AbsorbMagicka));
        EXPECT_TRUE(isGodModeSuppressedEffect(ESM::MagicEffect::AbsorbFatigue));
        EXPECT_TRUE(isGodModeSuppressedEffect(ESM::MagicEffect::AbsorbAttribute));
        EXPECT_TRUE(isGodModeSuppressedEffect(ESM::MagicEffect::AbsorbSkill));
        EXPECT_FALSE(isGodModeSuppressedEffect(ESM::MagicEffect::DamageHealth));
        EXPECT_FALSE(isGodModeSuppressedEffect(ESM::MagicEffect::FortifyHealth));
    }
}
