#include <components/sceneutil/compilepriority.hpp>

#include <osg/Group>
#include <osg/Texture2D>
#include <osg/Program>
#include <osg/GraphicsContext>
#include <osg/State>
#include <gtest/gtest.h>

namespace
{
    // No native context or GL calls: these tests exercise dependency collection
    // and callback ownership, not driver compilation.
    class TestContext : public osg::GraphicsContext
    {
    public:
        TestContext() { setState(new osg::State); }
        ~TestContext() override { setState(nullptr); } // no native context or GL object manager was allocated
        bool valid() const override { return true; }
        bool realizeImplementation() override { return false; }
        bool isRealizedImplementation() const override { return false; }
        void closeImplementation() override {}
        bool makeCurrentImplementation() override { return false; }
        bool makeContextCurrentImplementation(osg::GraphicsContext*) override { return false; }
        bool releaseContextImplementation() override { return false; }
        void bindPBufferToTextureImplementation(GLenum) override {}
        void swapBuffersImplementation() override {}
    };

    TEST(CompileReadiness, QueuedMarkersDoNotImplyCompletion)
    {
        using Operation = osgUtil::IncrementalCompileOperation;
        osg::ref_ptr<Operation> operation = new Operation;
        osg::ref_ptr<TestContext> context = new TestContext;
        osg::ref_ptr<osg::Group> node = new osg::Group;
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        node->getOrCreateStateSet()->setTextureAttribute(0, texture);
        osgUtil::StateToCompile queued(osgUtil::GLObjectsVisitor::COMPILE_STATE_ATTRIBUTES, operation->getMarkerObject());
        node->accept(queued);
        ASSERT_EQ(texture->getUserData(), operation->getMarkerObject());
        auto prepared = SceneUtil::prepareCompileReadiness(node, { context });
        EXPECT_FALSE(prepared.mReady->load());
        EXPECT_TRUE(prepared.mSet->resources().contains(texture));
        EXPECT_FALSE(prepared.mSet->_compileMap.at(context)._compileOps.empty());
        EXPECT_EQ(texture->getUserData(), operation->getMarkerObject());
        operation->add(prepared.mSet, false);
        EXPECT_FALSE(prepared.mReady->load());
        operation->remove(prepared.mSet);
    }

    TEST(CompileReadiness, RejectsMissingContextsAndUninitializedState)
    {
        osg::ref_ptr<osg::Group> node = new osg::Group;
        EXPECT_THROW(SceneUtil::prepareCompileReadiness(node, {}), std::invalid_argument);
        EXPECT_THROW(SceneUtil::prepareCompileReadiness(node, { nullptr }), std::invalid_argument);
        osg::ref_ptr<TestContext> context = new TestContext;
        context->setState(nullptr);
        EXPECT_THROW(SceneUtil::prepareCompileReadiness(node, { context }), std::invalid_argument);
    }

    TEST(CompileReadiness, CapturesEveryContextAndLateCallbackIsIsolated)
    {
        osg::ref_ptr<TestContext> first = new TestContext, second = new TestContext;
        osg::ref_ptr<osg::Group> node = new osg::Group;
        node->getOrCreateStateSet()->setAttribute(new osg::Program);
        auto old = SceneUtil::prepareCompileReadiness(node, { first, second });
        auto current = SceneUtil::prepareCompileReadiness(node, { second });
        EXPECT_EQ(old.mSet->_compileMap.size(), 2u);
        EXPECT_EQ(current.mSet->_compileMap.size(), 1u);
        auto callback = old.mSet->_compileCompletedCallback;
        old.mSet = nullptr;
        EXPECT_TRUE(callback->compileCompleted(nullptr));
        EXPECT_TRUE(callback->compileCompleted(nullptr));
        EXPECT_TRUE(old.mReady->load());
        EXPECT_FALSE(current.mReady->load());
        EXPECT_GT(first->referenceCount(), 1);
    }

    TEST(CompilePriority, PromotesSharedTextureAndProgramOwnersWithoutDuplicatingWork)
    {
        using Operation = osgUtil::IncrementalCompileOperation;
        osg::ref_ptr<Operation> operation = new Operation;
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        osg::ref_ptr<osg::Program> program = new osg::Program;
        osg::ref_ptr<osg::Group> farNode = new osg::Group;
        osg::ref_ptr<osg::Group> original = new osg::Group;
        osg::ref_ptr<osg::Group> nearNode = new osg::Group;
        nearNode->getOrCreateStateSet()->setTextureAttribute(0, texture);
        nearNode->getOrCreateStateSet()->setAttribute(program);
        osg::ref_ptr<SceneUtil::ResourceCompileSet> far = new SceneUtil::ResourceCompileSet(farNode);
        osg::ref_ptr<SceneUtil::ResourceCompileSet> textureOwner = new SceneUtil::ResourceCompileSet(original);
        osg::ref_ptr<SceneUtil::ResourceCompileSet> programOwner = new SceneUtil::ResourceCompileSet(original);
        textureOwner->_compileMap[nullptr].add(texture);
        programOwner->_compileMap[nullptr].add(program);
        textureOwner->captureResources();
        programOwner->captureResources();
        operation->add(far, false);
        operation->add(textureOwner, false);
        operation->add(programOwner, false);
        // OSG is free to consume these lists after enqueue. Priority uses only
        // the immutable metadata, never the concurrently changing CompileLists.
        textureOwner->_compileMap.clear();
        programOwner->_compileMap.clear();
        SceneUtil::CompilePriorityVisitor needed;
        nearNode->accept(needed);
        EXPECT_EQ(needed.promote(*operation), 2u);
        auto it = operation->getToCompile().begin();
        EXPECT_EQ((it++)->get(), textureOwner.get());
        EXPECT_EQ((it++)->get(), programOwner.get());
        EXPECT_EQ((it++)->get(), far.get());
        EXPECT_EQ(it, operation->getToCompile().end());
        EXPECT_EQ(needed.promote(*operation), 2u);
        EXPECT_EQ(operation->getToCompile().size(), 3u);
    }

    TEST(CompilePriority, RecognizesCachedSubgraphsAndExplicitPassState)
    {
        using Operation = osgUtil::IncrementalCompileOperation;
        osg::ref_ptr<Operation> operation = new Operation;
        osg::ref_ptr<osg::Group> root = new osg::Group;
        osg::ref_ptr<osg::Group> child = new osg::Group;
        root->addChild(child);
        child->setNodeMask(0);
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        osg::ref_ptr<osg::StateSet> pass = new osg::StateSet;
        pass->setTextureAttribute(0, texture);
        osg::ref_ptr<Operation::CompileSet> cached = new Operation::CompileSet(child);
        osg::ref_ptr<SceneUtil::ResourceCompileSet> owner = new SceneUtil::ResourceCompileSet(root);
        owner->_compileMap[nullptr].add(texture);
        owner->captureResources();
        operation->add(cached, false);
        operation->add(owner, false);
        SceneUtil::CompilePriorityVisitor needed;
        root->accept(needed);
        needed.collect(pass);
        EXPECT_EQ(needed.promote(*operation), 2u);
        operation->remove(cached);
        operation->remove(owner);
        EXPECT_EQ(needed.promote(*operation), 0u);
    }
}
