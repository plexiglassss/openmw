#include "glextensions.hpp"

#include <osg/GraphicsContext>

#include <atomic>
#include <mutex>

namespace SceneUtil
{
    namespace
    {
        std::set<osg::observer_ptr<osg::GLExtensions>> sGLExtensions;
        std::atomic_bool sGpuSkinning{ false };
        bool sCapabilitiesInitialized = false;
        std::mutex sCapabilitiesMutex;

        class GLExtensionsObserver : public osg::Observer
        {
        public:
            static GLExtensionsObserver sInstance;

            ~GLExtensionsObserver() override
            {
                for (auto& ptr : sGLExtensions)
                {
                    osg::ref_ptr<osg::GLExtensions> ref;
                    if (ptr.lock(ref))
                        ref->removeObserver(this);
                }
            }

            void objectDeleted(void* referenced) override
            {
                sGLExtensions.erase(static_cast<osg::GLExtensions*>(referenced));
            }
        };

        // construct after sGLExtensions so this gets destroyed first.
        GLExtensionsObserver GLExtensionsObserver::sInstance{};
    }

    bool glExtensionsReady()
    {
        return !sGLExtensions.empty();
    }

    bool supportsGpuSkinning()
    {
        return sGpuSkinning.load(std::memory_order_acquire);
    }

    osg::GLExtensions& getGLExtensions()
    {
        if (sGLExtensions.empty())
            throw std::runtime_error(
                "GetGLExtensionsOperation was not used when the current context was created or there is no current "
                "context");
        return **sGLExtensions.begin();
    }

    GetGLExtensionsOperation::GetGLExtensionsOperation()
        : GraphicsOperation("GetGLExtensionsOperation", false)
    {
    }

    void GetGLExtensionsOperation::operator()(osg::GraphicsContext* graphicsContext)
    {
        const std::lock_guard lock(sCapabilitiesMutex);
        auto [itr, _] = sGLExtensions.emplace(graphicsContext->getState()->get<osg::GLExtensions>());
        (*itr)->addObserver(&GLExtensionsObserver::sInstance);
        // Query with a current GL context, before background model loaders are started.
        // Leave ample room beyond the 64-matrix palette for lighting, stereo and material uniforms.
        GLint uniforms = 0;
        GLint attributes = 0;
        if ((*itr)->isGlslSupported)
        {
            glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &uniforms);
            glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &attributes);
        }
        const bool supported = (*itr)->glslLanguageVersion >= 1.2f && uniforms >= 4096 && attributes >= 16;
        sGpuSkinning.store(supported && (!sCapabilitiesInitialized || sGpuSkinning.load()), std::memory_order_release);
        sCapabilitiesInitialized = true;
    }
}
