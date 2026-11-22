#ifndef OPENMW_TESTS_PREPARED_CELL_GL_H
#define OPENMW_TESTS_PREPARED_CELL_GL_H
// Headless Mesa context, based on the existing scenery-graphics regression fixture.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <osg/ContextData>
#include <osg/GLExtensions>
#include <osg/GLObjects>
#include <osg/GraphicsContext>
#include <osg/State>
#include <stdexcept>
namespace
{
    class PreparedEglContext : public osg::GraphicsContext
    {
        EGLDisplay mDisplay;
        EGLSurface mSurface;
        EGLContext mContext;
    public:
        PreparedEglContext()
        {
            const auto platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
                eglGetProcAddress("eglGetPlatformDisplayEXT"));
            if (!platform)
                throw std::runtime_error("Surfaceless EGL unavailable");
            mDisplay = platform(EGL_PLATFORM_SURFACELESS_MESA, nullptr, nullptr);
            if (!eglInitialize(mDisplay, nullptr, nullptr) || !eglBindAPI(EGL_OPENGL_API))
                throw std::runtime_error("EGL initialization failed");
            const EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
            EGLConfig config;
            EGLint count;
            if (!eglChooseConfig(mDisplay, attrs, &config, 1, &count) || count != 1)
                throw std::runtime_error("EGL configuration failed");
            const EGLint dimensions[] = { EGL_WIDTH, 32, EGL_HEIGHT, 32, EGL_NONE };
            mSurface = eglCreatePbufferSurface(mDisplay, config, dimensions);
            mContext = eglCreateContext(mDisplay, config, EGL_NO_CONTEXT, nullptr);
            if (!makeCurrentImplementation())
                throw std::runtime_error("EGL context failed");
            setState(new osg::State);
            getState()->setGraphicsContext(this);
            getState()->setContextID(0);
            osg::getOrCreateContextData(0);
            getState()->initializeExtensionProcs();
            glViewport(0, 0, 32, 32);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
        }
        ~PreparedEglContext() override
        {
            osg::flushAllDeletedGLObjects(0);
            osg::GLExtensions::Set(0, nullptr);
            setState(nullptr);
            eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(mDisplay, mContext);
            eglDestroySurface(mDisplay, mSurface);
            eglTerminate(mDisplay);
        }
        bool valid() const override { return true; }
        bool realizeImplementation() override { return true; }
        bool isRealizedImplementation() const override { return true; }
        void closeImplementation() override {}
        bool makeCurrentImplementation() override { return eglMakeCurrent(mDisplay, mSurface, mSurface, mContext); }
        bool makeContextCurrentImplementation(osg::GraphicsContext*) override { return makeCurrentImplementation(); }
        bool releaseContextImplementation() override { return true; }
        void bindPBufferToTextureImplementation(GLenum) override {}
        void swapBuffersImplementation() override {}
    };

}
#endif
