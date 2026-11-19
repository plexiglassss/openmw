#ifndef OPENMW_COMPONENTS_SCENEUTIL_GAMEPLAYCOMPILE_H
#define OPENMW_COMPONENTS_SCENEUTIL_GAMEPLAYCOMPILE_H

#include <osg/Stats>
#include <osgUtil/IncrementalCompileOperation>

#include <atomic>

namespace SceneUtil
{
    class GameplayCompileOperation final : public osgUtil::IncrementalCompileOperation
    {
    public:
        GameplayCompileOperation(osg::Stats* stats, double target);
        void setLoading(bool loading) { mLoading.store(loading, std::memory_order_relaxed); }
        void operator()(osg::GraphicsContext* context) override;

    private:
        osg::ref_ptr<osg::Stats> mStats;
        std::atomic_bool mLoading{ false };
        double mGameplayBudget;
    };
}

#endif
