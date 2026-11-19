#include <gtest/gtest.h>

#include <components/esm3/loadnpc.hpp>
#include <components/lua/configuration.hpp>

namespace
{
    TEST(LuaArrivalConfigurationTest, RepeatedTypesSharePlansWhileReferenceOverridesStayPrivate)
    {
        LuaUtil::ScriptsConfiguration conf;
        ESM::LuaScriptsCfg cfg;
        ESM::LuaScriptCfg script;
        script.mScriptPath = VFS::Path::Normalized("arrival/check.lua");
        script.mTypes = { ESM::REC_NPC_ };
        script.mInitializationData = "ordinary";
        const ESM::RefId first = ESM::RefId::stringRefId("first");
        const ESM::RefId second = ESM::RefId::stringRefId("second");
        const ESM::RefId special = ESM::RefId::stringRefId("special");
        script.mRecords.push_back({ true, special, "custom" });
        script.mRefs.push_back({ false, 4, 3, "" });
        cfg.mScripts.push_back(script);
        conf.init(cfg, false);
        const auto base = conf.sharedLocalConf(ESM::REC_NPC_, first, {});
        EXPECT_EQ(base, conf.sharedLocalConf(ESM::REC_NPC_, second, {}));
        const auto changed = conf.sharedLocalConf(ESM::REC_NPC_, special, {});
        EXPECT_EQ(changed, conf.sharedLocalConf(ESM::REC_NPC_, special, {}));
        EXPECT_EQ(changed->at(0), "custom");
        EXPECT_TRUE(conf.sharedLocalConf(ESM::REC_NPC_, special, ESM::RefNum{4, 3})->empty());
        EXPECT_EQ(base->at(0), "ordinary");
        cfg.mScripts[0].mInitializationData = "new-data";
        cfg.mScripts[0].mRefs.clear();
        conf.init(std::move(cfg), true);
        EXPECT_EQ(conf.sharedLocalConf(ESM::REC_NPC_, first, {})->at(0), "new-data");
        EXPECT_EQ(conf.sharedLocalConf(ESM::REC_NPC_, special, ESM::RefNum{4, 3})->at(0), "custom");
    }
}
