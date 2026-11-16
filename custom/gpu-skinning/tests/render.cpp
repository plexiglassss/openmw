// Standalone Mesa/OSG integration regression. The shared fixture also runs in components-tests on Windows.
#include "../../../apps/components_tests/sceneutil/testgpuskinning.cpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <fstream>
#include <iterator>
#include <osg/GLObjects>
#include <osg/RenderInfo>

namespace
{
    class HeadlessGL
    {
    public:
        HeadlessGL()
        {
            auto platform
                = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
            if (!platform)
                throw std::runtime_error("Surfaceless EGL unavailable");
            display = platform(EGL_PLATFORM_SURFACELESS_MESA, nullptr, nullptr);
            if (!eglInitialize(display, nullptr, nullptr) || !eglBindAPI(EGL_OPENGL_API))
                throw std::runtime_error("EGL initialization failed");
            const EGLint attributes[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
            EGLint count = 0;
            EGLConfig config = nullptr;
            if (!eglChooseConfig(display, attributes, &config, 1, &count) || count != 1)
                throw std::runtime_error("EGL config failed");
            const EGLint dimensions[] = { EGL_WIDTH, 32, EGL_HEIGHT, 32, EGL_NONE };
            surface = eglCreatePbufferSurface(display, config, dimensions);
            context = eglCreateContext(display, config, EGL_NO_CONTEXT, nullptr);
            if (!eglMakeCurrent(display, surface, surface, context))
                throw std::runtime_error("EGL context failed");
            glViewport(0, 0, 32, 32);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
        }
        ~HeadlessGL()
        {
            // Match GraphicsContext teardown: drain retired VAOs before destroying the context.
            // Otherwise OSG 3.6's static managers retain GLExtensions beyond its extension-set lifetime.
            osg::flushAllDeletedGLObjects(0);
            osg::GLExtensions::Set(0, nullptr);
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display, context);
            eglDestroySurface(display, surface);
            eglTerminate(display);
        }
        EGLDisplay display;
        EGLSurface surface;
        EGLContext context;
    };

    osg::ref_ptr<osg::Program> program(bool skinning, unsigned int weightsSlot = 4, unsigned int indicesSlot = 5)
    {
        std::string source = "#version 120\n";
        if (skinning)
        {
            std::ifstream file("files/shaders/lib/core/skinning.glsl");
            if (!file)
                throw std::runtime_error("Run from repository root: skinning.glsl is missing");
            std::string helper{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
            const auto token = helper.find("@gpuSkinning");
            for (auto next = token; next != std::string::npos; next = helper.find("@gpuSkinning"))
                helper.replace(next, std::string("@gpuSkinning").size(), "1");
            source += helper;
        }
        source += skinning ? "\nvoid main(){ gl_Position = openmwRigMatrix() * gl_Vertex; }"
                           : "\nvoid main(){ gl_Position = gl_Vertex; }";
        osg::ref_ptr<osg::Program> result = new osg::Program;
        result->addShader(new osg::Shader(osg::Shader::VERTEX, source));
        result->addShader(
            new osg::Shader(osg::Shader::FRAGMENT, "#version 120\nvoid main(){ gl_FragColor = vec4(1.0); }"));
        result->addBindAttribLocation("openmwRigWeights", weightsSlot);
        result->addBindAttribLocation("openmwRigIndices", indicesSlot);
        return result;
    }

    std::array<unsigned char, 3> pixel(int x, int y)
    {
        std::array<unsigned char, 3> result{};
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, result.data());
        return result;
    }

    TEST(GpuSkinningGL, ActualDrawFallbackRestoresUniformsAndVertexArraysAcrossPasses)
    {
        HeadlessGL context;
        RigFixture fixture;
        fixture.source->setUseVertexArrayObject(true);
        ASSERT_TRUE(fixture.rig->setGpuSkinning(true));
        fixture.bone->setMatrix(osg::Matrix::translate(1, 0, 0));
        auto geometry = fixture.frame(1);
        geometry->setUseVertexArrayObject(true);
        osg::ref_ptr<osg::State> state = new osg::State;
        state->setContextID(0);
        state->initializeExtensionProcs();
        osg::RenderInfo info(state, nullptr);
        const auto supported = program(true);
        const auto custom = program(false);
        const auto remapped = program(true, 10, 11);
        for (int pass = 0; pass < 8; ++pass)
        {
            // Supported -> no helper -> supported -> same helper with incompatible bindings -> supported.
            const auto selected = pass == 1 ? custom : pass == 3 ? remapped : supported;
            osg::ref_ptr<osg::StateSet> passState = new osg::StateSet;
            passState->setAttributeAndModes(selected, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
            const bool forcedOff = pass == 5;
            passState->addUniform(new osg::Uniform("openmwRigEnabled", false),
                forcedOff ? osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE : osg::StateAttribute::ON);
            state->pushStateSet(passState);
            state->pushStateSet(geometry->getStateSet());
            state->apply();
            ASSERT_TRUE(selected->getPCP(*state)->isLinked());
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            geometry->draw(info);
            EXPECT_EQ(pixel(24, 15), (std::array<unsigned char, 3>{ 255, 255, 255 })) << "pass " << pass;
            EXPECT_EQ(pixel(8, 15), (std::array<unsigned char, 3>{ 0, 0, 0 })) << "pass " << pass;
            if (selected != custom)
            {
                GLint enabled = -1;
                const auto* pcp = selected->getPCP(*state);
                state->get<osg::GLExtensions>()->glGetUniformiv(
                    pcp->getHandle(), pcp->getUniformLocation("openmwRigEnabled"), &enabled);
                EXPECT_EQ(enabled, forcedOff ? 0 : 1) << "uniform restoration pass " << pass;
            }
            EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
            state->popStateSet();
            state->popStateSet();
        }
        geometry->releaseGLObjects(state);
        fixture.source->releaseGLObjects(state);
    }
}
