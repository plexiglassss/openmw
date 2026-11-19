#include "compositemaprenderer.hpp"

#include <osg/FrameBufferObject>
#include <osg/RenderInfo>
#include <osg/Texture2D>

#include <algorithm>
#include <components/diagnostics/timing.hpp>

namespace Terrain
{

    CompositeMapRenderer::CompositeMapRenderer()
        : mTargetFrameRate(120)
        , mMinimumTimeAvailable(0.0025)
    {
        setSupportsDisplayList(false);
        setCullingActive(false);

        mFBO = new osg::FrameBufferObject;
    }

    CompositeMapRenderer::~CompositeMapRenderer() = default;

    void CompositeMapRenderer::drawImplementation(osg::RenderInfo& renderInfo) const
    {
        double dt = mTimer.time_s();
        dt = std::min(dt, 0.2);
        mTimer.setStartTick();
        double targetFrameTime = 1.0 / static_cast<double>(mTargetFrameRate);
        double conservativeTimeRatio(0.75);
        double availableTime = std::max((targetFrameTime - dt) * conservativeTimeRatio, mMinimumTimeAvailable);

        std::unique_lock lock(mMutex);

        if (mImmediateCompileSet.empty() && mCompileSet.empty())
            return;

        using Clock = std::chrono::steady_clock;
        const auto deadline = Clock::now()
            + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(availableTime));
        std::vector<osg::ref_ptr<CompositeMap>> retry;
        while (!mImmediateCompileSet.empty() || (!mCompileSet.empty() && Clock::now() < deadline))
        {
            const bool immediate = !mImmediateCompileSet.empty();
            auto& pending = immediate ? mImmediateCompileSet : mCompileSet;
            osg::ref_ptr<CompositeMap> node = *pending.begin();
            pending.erase(pending.begin());
            const size_t before = node->mCompiled;
            lock.unlock();
            {
                Diagnostics::Scope timing("terrain", "composite", immediate ? "visibleLayers" : "backgroundLayers");
                compile(*node, renderInfo, deadline);
            }
            lock.lock();
            if (node->mCompiled < node->mDrawables.size())
            {
                if (node->mCompiled == before)
                    retry.push_back(node); // One attempt per frame while the framebuffer is unavailable.
                else
                    (node->mImmediate.load(std::memory_order_relaxed) ? mImmediateCompileSet : mCompileSet).insert(node);
            }
        }
        for (const auto& node : retry)
            (node->mImmediate.load(std::memory_order_relaxed) ? mImmediateCompileSet : mCompileSet).insert(node);
        mTimer.setStartTick();
    }

    void CompositeMapRenderer::compile(CompositeMap& compositeMap, osg::RenderInfo& renderInfo,
        std::chrono::steady_clock::time_point deadline) const
    {
        // if there are no more external references we can assume the texture is no longer required
        if (compositeMap.mTexture->referenceCount() <= 1)
        {
            compositeMap.mCompiled = compositeMap.mDrawables.size();
            return;
        }

        osg::State& state = *renderInfo.getState();
        osg::GLExtensions* ext = state.get<osg::GLExtensions>();

        if (!mFBO)
            return;

        if (!ext->isFrameBufferObjectSupported)
            return;

        osg::FrameBufferAttachment attach(compositeMap.mTexture);
        mFBO->setAttachment(osg::Camera::COLOR_BUFFER, attach);
        mFBO->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);

        GLenum status = ext->glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT);

        if (status != GL_FRAMEBUFFER_COMPLETE_EXT)
        {
            GLuint fboId = state.getGraphicsContext() ? state.getGraphicsContext()->getDefaultFboId() : 0;
            ext->glBindFramebuffer(GL_FRAMEBUFFER_EXT, fboId);
            OSG_ALWAYS << "Error attaching FBO" << std::endl;
            return;
        }

        // inform State that Texture attribute has changed due to compiling of FBO texture
        // should OSG be doing this on its own?
        state.haveAppliedTextureAttribute(state.getActiveTextureUnit(), osg::StateAttribute::TEXTURE);

        compositeMap.drawPendingLayers([&](osg::Drawable& drawable) {
            osg::StateSet* stateset = drawable.getStateSet();

            if (stateset)
                renderInfo.getState()->pushStateSet(stateset);

            renderInfo.getState()->apply();

            glViewport(0, 0, compositeMap.mTexture->getTextureWidth(), compositeMap.mTexture->getTextureHeight());
            drawable.drawImplementation(renderInfo);

            if (stateset)
                renderInfo.getState()->popStateSet();

        }, [&] { return std::chrono::steady_clock::now() >= deadline; });

        state.haveAppliedAttribute(osg::StateAttribute::VIEWPORT);

        GLuint fboId = state.getGraphicsContext() ? state.getGraphicsContext()->getDefaultFboId() : 0;
        ext->glBindFramebuffer(GL_FRAMEBUFFER_EXT, fboId);
    }

    void CompositeMapRenderer::setMinimumTimeAvailableForCompile(double time)
    {
        mMinimumTimeAvailable = time;
    }

    void CompositeMapRenderer::setTargetFrameRate(float framerate)
    {
        mTargetFrameRate = framerate;
    }

    void CompositeMapRenderer::addCompositeMap(CompositeMap* compositeMap, bool immediate)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (immediate)
        {
            compositeMap->mImmediate.store(true, std::memory_order_relaxed);
            mImmediateCompileSet.insert(compositeMap);
        }
        else
            mCompileSet.insert(compositeMap);
    }

    void CompositeMapRenderer::setImmediate(CompositeMap* compositeMap)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        // The draw thread may have temporarily removed a partially prepared map from the queue.
        compositeMap->mImmediate.store(true, std::memory_order_relaxed);
        CompileSet::iterator found = mCompileSet.find(compositeMap);
        if (found == mCompileSet.end())
            return;
        else
        {
            mImmediateCompileSet.insert(compositeMap);
            mCompileSet.erase(found);
        }
    }

    size_t CompositeMapRenderer::getCompileSetSize() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mCompileSet.size();
    }

    CompositeMap::CompositeMap()
        : mCompiled(0)
    {
    }

    CompositeMap::~CompositeMap() {}

}
