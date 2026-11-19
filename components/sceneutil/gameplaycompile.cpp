#include "gameplaycompile.hpp"

#include <components/diagnostics/timing.hpp>

#include <osg/FrameStamp>
#include <osg/GLObjects>
#include <osg/Timer>
#include <OpenThreads/ScopedLock>

#include <algorithm>

namespace SceneUtil
{
    GameplayCompileOperation::GameplayCompileOperation(osg::Stats* stats, double target)
        : mStats(stats)
        , mGameplayBudget(std::min(0.001, 0.06 / std::max(1.0, target)))
    {
        setTargetFrameRate(std::max(1.0, target));
        setMinimumTimeAvailableForGLCompileAndDeletePerFrame(mGameplayBudget);
        // OSG counts already-compiled dependencies as operations too. Two per
        // frame can indefinitely delay the completion of a shared-resource set.
        setMaximumNumOfObjectsToCompilePerFrame(256);
    }

    void GameplayCompileOperation::operator()(osg::GraphicsContext* context)
    {
        osg::Timer clock;
        const auto* stamp = context->getState()->getFrameStamp();
        if (mLoading.load(std::memory_order_relaxed))
            osgUtil::IncrementalCompileOperation::operator()(context);
        else
        {
            CompileInfo info(context, this);
            info.compileAll = false;
            info.maxNumObjectsToCompile = getMaximumNumOfObjectsToCompilePerFrame();
            info.allocatedTime = mGameplayBudget;
            CompileSets pending;
            {
                Diagnostics::Scope timing("graphics", "preparation", "queue snapshot");
                const OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_toCompileMutex);
                pending = _toCompile;
            }
            // Flush and compile share one elapsed-time allowance. Checking the
            // deadline between operations cannot preempt an individual driver call.
            double flushTime = std::max(0.0, std::min(mGameplayBudget * 0.1,
                mGameplayBudget - info.timer.elapsedTime()));
            if (flushTime > 0)
            {
                Diagnostics::Scope timing("graphics", "preparation", "deletion");
                osg::flushDeletedGLObjects(context->getState()->getContextID(),
                    stamp ? stamp->getReferenceTime() : 0.0, flushTime);
            }
            Diagnostics::Scope timing("graphics", "preparation", "queue execution");
            compileSets(pending, info);
        }
        if (stamp && mStats && mStats->collectStats("resource"))
            mStats->setAttribute(stamp->getFrameNumber(), "GL incremental compile operation time", clock.time_s());
    }
}
