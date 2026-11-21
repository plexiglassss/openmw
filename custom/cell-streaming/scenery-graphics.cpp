#include <components/sceneutil/compilepriority.hpp>
#include <components/sceneutil/gameplaycompile.hpp>
#include <components/terrain/scenerycoverage.hpp>
#include <components/terrain/vertexbuffer.hpp>
#include <components/terrain/buffercache.hpp>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <osg/GraphicsContext>
#include <osg/ContextData>
#include <osg/State>
#include <osg/GLObjects>
#include <osg/Program>
#include <osg/BufferObject>
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

    TEST(TerrainVertexSharingGL, StitchVariantsDrawAfterSiblingReleaseAndBufferRecompile)
    {
        osg::ref_ptr<Context> context = new Context;
        auto* state = context->getState();
        Terrain::VertexBuffer vertices;
        for (unsigned y=0; y<5; ++y)
            for (unsigned x=0; x<5; ++x)
            {
                vertices.mPositions->push_back(osg::Vec3f(-1.f+x*.5f,-1.f+y*.5f,0));
                vertices.mNormals->push_back(osg::Vec3f(0,0,1));
                vertices.mColours->push_back(osg::Vec4ub(64,128,192,255));
            }
        vertices.seal();
        Terrain::BufferCache buffers;
        const auto shader = quad(-1,1,{1,1,1,1});
        osg::ref_ptr<osg::Geometry> first = new osg::Geometry;
        osg::ref_ptr<osg::Geometry> second = new osg::Geometry;
        const auto setup = [&](osg::Geometry& g,unsigned flags) {
            vertices.attach(g);
            g.addPrimitiveSet(buffers.getIndexBuffer(5,flags));
            g.setUseDisplayList(false);g.setUseVertexBufferObjects(true);
            g.setTexCoordArrayList(osg::Geometry::ArrayList(2,buffers.getUVBuffer(5)));
            g.setStateSet(shader->getStateSet());
        };
        setup(*first,0);
        const auto draw = [&](osg::Geometry& g) {
            glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            osg::RenderInfo info(state,nullptr);
            g.compileGLObjects(info);
            state->pushStateSet(g.getStateSet());state->apply();g.draw(info);state->popStateSet();
            EXPECT_EQ(pixel(16),(std::array<unsigned char,3>{64,128,192}));
            EXPECT_EQ(glGetError(),static_cast<GLenum>(GL_NO_ERROR));
        };
        draw(*first);
        // Attach a new variant after the original VBO has already been compiled/drawn.
        setup(*second,0x1111);
        ASSERT_EQ(first->getVertexArray(),second->getVertexArray());
        ASSERT_NE(first->getPrimitiveSet(0),second->getPrimitiveSet(0));
        draw(*second);
        first->releaseGLObjects(state);first=nullptr;
        draw(*second);
        vertices.releaseGLObjects(state);buffers.releaseGLObjects(state);
        draw(*second);
        EXPECT_EQ(vertices.mPositions->getVertexBufferObject()->getNumBufferData(),3u);
        second->releaseGLObjects(state);
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

    TEST(GranularCompilation, BufferStepsPreserveBindingsAndDoNotCompleteTheDrawable)
    {
        osg::ref_ptr<Context> context = new Context;
        auto* state = context->getState();
        auto* extensions = state->get<osg::GLExtensions>();
        const auto geometry = quad(-1, 1, {1, 0, 0, 1});
        // Distinct VBOs plus an EBO; duplicate arrays sharing a VBO must not
        // duplicate its upload operation.
        geometry->getColorArray()->setVertexBufferObject(new osg::VertexBufferObject);
        const std::array<unsigned, 6> indices{0, 1, 2, 3, 4, 5};
        geometry->removePrimitiveSet(0);
        geometry->addPrimitiveSet(new osg::DrawElementsUInt(GL_TRIANGLES, indices.begin(), indices.end()));
        const std::array<osg::BufferObject*, 3> uploads{ geometry->getVertexArray()->getBufferObject(),
            geometry->getColorArray()->getBufferObject(), geometry->getPrimitiveSet(0)->getBufferObject() };
        for (auto* buffer : uploads)
        {
            ASSERT_TRUE(buffer);
            EXPECT_TRUE(buffer->getOrCreateGLBufferObject(state->getContextID())->isDirty());
        }
        osg::ref_ptr<SceneUtil::ResourceCompileSet> set = new SceneUtil::ResourceCompileSet(geometry);
        auto& list = set->_compileMap[context];
        list.add(geometry);
        set->captureResources();
        set->splitCompileOperations();
        ASSERT_EQ(list._compileOps.size(), 4u);
        ASSERT_TRUE(extensions->glGenVertexArrays && extensions->glBindVertexArray);
        GLuint vao = 0, bindings[2]{};
        extensions->glGenVertexArrays(1, &vao);
        extensions->glBindVertexArray(vao);
        extensions->glGenBuffers(2, bindings);
        extensions->glBindBuffer(GL_ARRAY_BUFFER_ARB, bindings[0]);
        extensions->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB, bindings[1]);
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> operation = new osgUtil::IncrementalCompileOperation;
        for (unsigned step = 0; step < 3; ++step)
        {
            osgUtil::IncrementalCompileOperation::CompileInfo info(context, operation);
            info.maxNumObjectsToCompile = 1;
            info.allocatedTime = 1;
            EXPECT_FALSE(list.compile(info));
            EXPECT_EQ(list._compileOps.size(), 3u - step);
            unsigned completed = 0;
            for (auto* buffer : uploads)
                completed += !buffer->getOrCreateGLBufferObject(state->getContextID())->isDirty();
            EXPECT_EQ(completed, step + 1);
            GLint actual = 0;
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING_ARB, &actual);
            EXPECT_EQ(static_cast<GLuint>(actual), bindings[0]);
            glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB, &actual);
            EXPECT_EQ(static_cast<GLuint>(actual), bindings[1]);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &actual);
            EXPECT_EQ(static_cast<GLuint>(actual), vao);
            EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
        }
        extensions->glBindVertexArray(0);
        extensions->glBindBuffer(GL_ARRAY_BUFFER_ARB, 0);
        auto* colors = static_cast<osg::Vec4Array*>(geometry->getColorArray());
        (*colors)[0].set(0, 1, 0, 1);
        colors->dirty();
        EXPECT_TRUE(uploads[1]->getOrCreateGLBufferObject(state->getContextID())->isDirty());
        osgUtil::IncrementalCompileOperation::CompileInfo finish(context, operation);
        finish.compileAll = true;
        EXPECT_TRUE(list.compile(finish));
        // The final standard drawable remains functional after staged uploads.
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        osg::RenderInfo render(state, nullptr);
        state->pushStateSet(geometry->getStateSet());state->apply();geometry->draw(render);state->popStateSet();
        EXPECT_EQ(pixel(16), (std::array<unsigned char,3>{0,255,0}));
        EXPECT_FALSE(uploads[1]->getOrCreateGLBufferObject(state->getContextID())->isDirty());
        EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
        geometry->releaseGLObjects(state);
        extensions->glDeleteBuffers(2, bindings);
        extensions->glDeleteVertexArrays(1, &vao);
    }

    TEST(GranularCompilation, ShaderStagesFinishBeforeLinkAndDirtySourcesStillRelink)
    {
        osg::ref_ptr<Context> context = new Context;
        const auto geometry = quad(-1, 1, {0,1,0,1});
        auto* program = static_cast<osg::Program*>(geometry->getStateSet()->getAttribute(osg::StateAttribute::PROGRAM));
        osg::ref_ptr<SceneUtil::ResourceCompileSet> set = new SceneUtil::ResourceCompileSet(geometry);
        auto& list = set->_compileMap[context];
        list.add(program);
        set->splitCompileOperations();
        ASSERT_EQ(list._compileOps.size(), 3u);
        EXPECT_FALSE(program->getShader(0)->getPCS(*context->getState())->isCompiled());
        EXPECT_FALSE(program->getShader(1)->getPCS(*context->getState())->isCompiled());
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> operation = new osgUtil::IncrementalCompileOperation;
        for (unsigned step = 0; step < 2; ++step)
        {
            osgUtil::IncrementalCompileOperation::CompileInfo info(context, operation);
            info.maxNumObjectsToCompile = 1;
            info.allocatedTime = 1;
            EXPECT_FALSE(list.compile(info));
            for (unsigned shader = 0; shader < 2; ++shader)
                EXPECT_EQ(program->getShader(shader)->getPCS(*context->getState())->isCompiled(), shader <= step);
            EXPECT_TRUE(program->getPCP(*context->getState())->needsLink());
        }
        // Source changes between staging and finalization cannot make a stale
        // compiled shader/program appear ready.
        program->getShader(1)->setShaderSource("#version 120\nvoid main(){gl_FragColor=vec4(1,0,0,1);}");
        osgUtil::IncrementalCompileOperation::CompileInfo finish(context, operation);
        finish.compileAll = true;
        EXPECT_TRUE(list.compile(finish));
        EXPECT_FALSE(program->getPCP(*context->getState())->needsLink());
        auto* state = context->getState();
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        osg::RenderInfo render(state, nullptr);
        state->pushStateSet(geometry->getStateSet());state->apply();geometry->draw(render);state->popStateSet();
        EXPECT_EQ(pixel(16), (std::array<unsigned char,3>{255,0,0}));
        EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
        geometry->releaseGLObjects(state);
    }

    TEST(GranularCompilation, CustomCompileOverridesKeepTheirOriginalContract)
    {
        struct CustomGeometry : osg::Geometry
        {
            mutable int mCalls = 0;
            void compileGLObjects(osg::RenderInfo&) const override { ++mCalls; }
        };
        struct CustomProgram : osg::Program
        {
            mutable int mCalls = 0;
            void compileGLObjects(osg::State&) const override { ++mCalls; }
        };
        osg::ref_ptr<Context> context = new Context;
        osg::ref_ptr<CustomGeometry> geometry = new CustomGeometry;
        geometry->setVertexArray(new osg::Vec3Array(3));
        geometry->setUseVertexBufferObjects(true);
        osg::ref_ptr<CustomProgram> program = new CustomProgram;
        program->addShader(new osg::Shader(osg::Shader::VERTEX, "this must not compile"));
        osg::ref_ptr<SceneUtil::ResourceCompileSet> set = new SceneUtil::ResourceCompileSet(geometry);
        auto& list = set->_compileMap[context];
        list.add(geometry);list.add(program);
        set->splitCompileOperations();
        ASSERT_EQ(list._compileOps.size(), 2u);
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> operation = new osgUtil::IncrementalCompileOperation;
        osgUtil::IncrementalCompileOperation::CompileInfo info(context, operation);
        info.compileAll = true;
        EXPECT_TRUE(list.compile(info));
        EXPECT_EQ(geometry->mCalls, 1);
        EXPECT_EQ(program->mCalls, 1);
        EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    }
}
