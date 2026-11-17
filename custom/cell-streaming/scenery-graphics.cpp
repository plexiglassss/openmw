#include <components/sceneutil/compilepriority.hpp>
#include <components/sceneutil/gameplaycompile.hpp>
#include <components/terrain/scenerycoverage.hpp>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <osg/GraphicsContext>
#include <osg/ContextData>
#include <osg/State>
#include <osg/GLObjects>
#include <osg/Program>
#include <osg/FrameStamp>
#include <gtest/gtest.h>
#include <array>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <vector>

namespace
{
    class Context : public osg::GraphicsContext
    {
        EGLDisplay mDisplay;
        EGLSurface mSurface;
        EGLContext mContext;
    public:
        Context()
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
        ~Context() override
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

    osg::ref_ptr<osg::Geometry> quad(float left, float right, const osg::Vec4& color)
    {
        osg::ref_ptr<osg::Geometry> result = new osg::Geometry;
        const std::array<osg::Vec3f, 6> vertices{ osg::Vec3f(left,-1,0), {right,-1,0}, {right,1,0},
            {left,-1,0}, {right,1,0}, {left,1,0} };
        result->setVertexArray(new osg::Vec3Array(vertices.begin(), vertices.end()));
        result->setColorArray(new osg::Vec4Array(1, &color), osg::Array::BIND_OVERALL);
        result->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 6));
        result->setUseDisplayList(false);
        result->setUseVertexBufferObjects(true);
        osg::ref_ptr<osg::Program> program = new osg::Program;
        program->addShader(new osg::Shader(osg::Shader::VERTEX,
            "#version 120\nvoid main(){gl_Position=gl_Vertex;gl_FrontColor=gl_Color;}"));
        program->addShader(new osg::Shader(osg::Shader::FRAGMENT,
            "#version 120\nvoid main(){gl_FragColor=gl_Color;}"));
        result->getOrCreateStateSet()->setAttributeAndModes(program, osg::StateAttribute::ON);
        return result;
    }

    std::array<unsigned char, 3> pixel(int x)
    {
        std::array<unsigned char, 3> value;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, 16, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, value.data());
        return value;
    }

    TEST(GameplayCompilation, SharedCompiledDependenciesDoNotStarveTheFinalReceipt)
    {
        osg::ref_ptr<Context> context = new Context;
        osg::ref_ptr<SceneUtil::GameplayCompileOperation> operation
            = new SceneUtil::GameplayCompileOperation(nullptr, 120);
        operation->addGraphicsContext(context);
        const auto shared = quad(-1, 1, { 0, 0, 1, 1 });
        auto warm = SceneUtil::prepareCompileReadiness(shared, { context });
        operation->add(warm.mSet, false);
        for (unsigned frame = 0; frame < 128 && !warm.mReady->load(); ++frame)
            (*operation)(context);
        ASSERT_TRUE(warm.mReady->load());
        std::vector<SceneUtil::CompileReadiness> receipts;
        for (unsigned i = 0; i < 1024; ++i)
        {
            receipts.push_back(SceneUtil::prepareCompileReadiness(shared, { context }));
            operation->add(receipts.back().mSet, false);
        }
        const auto replacement = quad(-1, 1, { 1, 0, 0, 1 });
        auto last = SceneUtil::prepareCompileReadiness(replacement, { context });
        operation->add(last.mSet, false);
        ASSERT_FALSE(last.mReady->load());
        // Use the actual gameplay scheduler, with no compileAll/loading override.
        // At two operations per frame the 1,024 preceding sets cannot finish here.
        unsigned frames = 0;
        while (frames++ < 128 && !last.mReady->load())
            (*operation)(context);
        ASSERT_TRUE(last.mReady->load());
        for (const auto& receipt : receipts)
            EXPECT_TRUE(receipt.mReady->load());
        EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
        shared->releaseGLObjects(context->getState());
        replacement->releaseGLObjects(context->getState());
        operation->removeGraphicsContext(context);
    }

    class CountedCompile final : public osgUtil::IncrementalCompileOperation::CompileOp
    {
        int& mCalls;
        bool mSlow;
    public:
        CountedCompile(int& calls, bool slow = false) : mCalls(calls), mSlow(slow) {}
        double estimatedTimeForCompile(osgUtil::IncrementalCompileOperation::CompileInfo&) const override { return 0; }
        bool compile(osgUtil::IncrementalCompileOperation::CompileInfo&) override
        {
            ++mCalls;
            if (mSlow)
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            return true;
        }
    };

    TEST(GameplayCompilation, StopsAfterAnIndivisibleOverrunAndResumesWithFiniteWork)
    {
        osg::ref_ptr<Context> context = new Context;
        osg::ref_ptr<SceneUtil::GameplayCompileOperation> operation
            = new SceneUtil::GameplayCompileOperation(nullptr, 120);
        operation->addGraphicsContext(context);
        osg::ref_ptr<osgUtil::IncrementalCompileOperation::CompileSet> set
            = new osgUtil::IncrementalCompileOperation::CompileSet;
        set->_numberCompileListsToCompile.exchange(1);
        int calls = 0;
        auto& list = set->_compileMap[context];
        list.add(new CountedCompile(calls, true));
        for (int i = 0; i < 1000; ++i)
            list.add(new CountedCompile(calls));
        operation->add(set, false);
        operation->setCompileAllTillFrameNumber(100); // cannot bypass the gameplay deadline
        for (unsigned frame = 0; frame < 32 && calls == 0; ++frame)
            (*operation)(context);
        ASSERT_EQ(calls, 1);
        EXPECT_FALSE(set->compiled());
        (*operation)(context);
        EXPECT_GT(calls, 1);
        EXPECT_LE(calls, 257);
        operation->setLoading(true);
        operation->setMaximumNumOfObjectsToCompilePerFrame(1000);
        (*operation)(context);
        EXPECT_TRUE(set->compiled());
        EXPECT_EQ(calls, 1001);
        operation->setMaximumNumOfObjectsToCompilePerFrame(256);
        operation->setLoading(false);
        osg::ref_ptr<osgUtil::IncrementalCompileOperation::CompileSet> resumed
            = new osgUtil::IncrementalCompileOperation::CompileSet;
        resumed->_numberCompileListsToCompile.exchange(1);
        resumed->_compileMap[context].add(new CountedCompile(calls, true));
        resumed->_compileMap[context].add(new CountedCompile(calls));
        operation->add(resumed, false);
        for (unsigned frame = 0; frame < 32 && calls == 1001; ++frame)
            (*operation)(context);
        EXPECT_EQ(calls, 1002);
        EXPECT_FALSE(resumed->compiled());
        operation->remove(resumed);
        operation->removeGraphicsContext(context);
    }

    TEST(SceneryGraphics, CompletedWholeSectionReplacesCoverAfterActualCompilation)
    {
        osg::ref_ptr<Context> context = new Context;
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> operation = new osgUtil::IncrementalCompileOperation;
        operation->addGraphicsContext(context);
        const auto bridge = quad(-1, 1, {0,0,1,1});
        const auto left = quad(-1, 0, {1,0,0,1});
        const auto right = quad(0, 1, {0,1,0,1});
        auto first = SceneUtil::prepareCompileReadiness(left, {context});
        auto second = SceneUtil::prepareCompileReadiness(right, {context});
        struct Request {float mSize;osg::Vec2f mCenter;bool mActiveGrid=false;};
        const std::array requests{Request{.5f,{.25f,.25f}},Request{.5f,{.75f,.25f}}};
        const auto group = Terrain::sceneryCoverage(std::span<const Request>(requests),osg::Vec4i{}).front();
        const auto ready = [&] { return Terrain::sceneryReplacementReady(group,
            [&](std::size_t i) {return (i == 0 ? first.mReady : second.mReady)->load();}); };
        const auto draw = [&] {
            glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            osg::RenderInfo info(context->getState(), nullptr);
            const auto render = [&](osg::Geometry* geometry) {
                auto* state = context->getState();
                state->pushStateSet(geometry->getStateSet());state->apply();geometry->draw(info);state->popStateSet();
            };
            if (ready()) {render(left);render(right);} else render(bridge);
            glFinish();
        };
        const auto pump = [&](unsigned frame) {
            operation->setCurrentFrameNumber(frame);operation->compileAllForNextFrame();
            (*operation)(context);
            osg::ref_ptr<osg::FrameStamp> stamp = new osg::FrameStamp;stamp->setFrameNumber(frame);
            operation->mergeCompiledSubgraphs(stamp);
        };
        operation->add(first.mSet, false);
        EXPECT_FALSE(ready());draw();
        EXPECT_EQ(pixel(8), (std::array<unsigned char,3>{0,0,255}));
        pump(1);
        ASSERT_TRUE(first.mReady->load());ASSERT_FALSE(second.mReady->load());
        EXPECT_FALSE(ready());draw();
        EXPECT_EQ(pixel(8), (std::array<unsigned char,3>{0,0,255}));
        EXPECT_EQ(pixel(24), (std::array<unsigned char,3>{0,0,255}));
        operation->add(second.mSet, false);pump(2);
        ASSERT_TRUE(ready());draw();
        EXPECT_EQ(pixel(8), (std::array<unsigned char,3>{255,0,0}));
        EXPECT_EQ(pixel(24), (std::array<unsigned char,3>{0,255,0}));
        auto cancelled = SceneUtil::prepareCompileReadiness(left, {context});
        operation->add(cancelled.mSet, false);operation->remove(cancelled.mSet);pump(3);
        EXPECT_FALSE(cancelled.mReady->load());
        EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
        bridge->releaseGLObjects(context->getState());left->releaseGLObjects(context->getState());
        right->releaseGLObjects(context->getState());operation->removeGraphicsContext(context);
    }
}
