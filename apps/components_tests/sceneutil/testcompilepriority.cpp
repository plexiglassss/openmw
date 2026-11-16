#include <components/sceneutil/compilepriority.hpp>
#include <components/sceneutil/asyncpreparation.hpp>

#include <osg/Group>
#include <osg/Texture2D>
#include <osg/Program>
#include <osg/GraphicsContext>
#include <osg/State>
#include <gtest/gtest.h>

#include <chrono>
#include <latch>
#include <new>
#include <thread>

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

    TEST(CompileReadinessCache, DoesNotKeepSceneGraphsAlive)
    {
        struct Node final : osg::Group
        {
            bool& mDestroyed;
            explicit Node(bool& destroyed) : mDestroyed(destroyed) {}
            ~Node() override { mDestroyed = true; }
        };
        SceneUtil::CompileReadinessCache cache;
        bool destroyed = false;
        osg::ref_ptr<osg::Node> node = new Node(destroyed);
        cache.remember(node);
        EXPECT_TRUE(cache.contains(node));
        EXPECT_EQ(node->referenceCount(), 1);
        node = nullptr;
        EXPECT_TRUE(destroyed);
        cache.clear();
        EXPECT_EQ(cache.size(), 0u);
    }

    TEST(CompileReadinessCache, RecycledAddressCannotInheritReadiness)
    {
        struct Node final : osg::Group
        {
            // Deterministic allocator address reuse; ref_ptr still performs the
            // real OSG destruction/observer notification before the next node.
            static void operator delete(void*) {}
        };
        alignas(Node) unsigned char storage[sizeof(Node)];
        SceneUtil::CompileReadinessCache cache;
        osg::ref_ptr<osg::Node> first = ::new (static_cast<void*>(storage)) Node;
        const auto address = first.get();
        cache.remember(first);
        first = nullptr;
        osg::ref_ptr<osg::Node> replacement = ::new (static_cast<void*>(storage)) Node;
        ASSERT_EQ(replacement.get(), address);
        EXPECT_FALSE(cache.contains(replacement));
        cache.remember(replacement);
        EXPECT_TRUE(cache.contains(replacement));
        replacement = nullptr;
    }

    TEST(CompileReadinessCache, EvictsLeastRecentlyUsedReceiptWithinLimit)
    {
        SceneUtil::CompileReadinessCache cache(2);
        osg::ref_ptr<osg::Node> a = new osg::Group, b = new osg::Group, c = new osg::Group;
        cache.remember(a);
        cache.remember(b);
        EXPECT_TRUE(cache.contains(a));
        cache.remember(c);
        EXPECT_EQ(cache.size(), 2u);
        EXPECT_TRUE(cache.contains(a));
        EXPECT_FALSE(cache.contains(b));
        EXPECT_TRUE(cache.contains(c));
        cache.remember(a);
        EXPECT_EQ(cache.size(), 2u);
        EXPECT_EQ(a->referenceCount(), 1);
        EXPECT_EQ(b->referenceCount(), 1);
        EXPECT_EQ(c->referenceCount(), 1);
    }

    TEST(CompileReadinessCache, ClearAndDisabledCacheNeverCertifyNodes)
    {
        SceneUtil::CompileReadinessCache cache, disabled(0);
        osg::ref_ptr<osg::Node> node = new osg::Group;
        cache.remember(nullptr);
        EXPECT_FALSE(cache.contains(nullptr));
        EXPECT_EQ(cache.size(), 0u);
        cache.remember(node);
        cache.clear();
        EXPECT_FALSE(cache.contains(node));
        disabled.remember(node);
        EXPECT_FALSE(disabled.contains(node));
        EXPECT_EQ(disabled.size(), 0u);
    }

    TEST(CompileReadinessCache, CompletedReceiptSurvivesHeavyRequestRetirement)
    {
        using Requests = SceneUtil::AsyncPreparation<int, osg::ref_ptr<osg::Node>>;
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Requests requests(queue);
        SceneUtil::CompileReadinessCache cache;
        osg::ref_ptr<osg::Node> node = new osg::Group;
        auto gate = std::make_shared<std::atomic_bool>(false);
        std::atomic_int calls{ 0 };
        std::latch produced(1);
        auto factory = [&](const std::atomic_bool&) {
            ++calls;
            produced.count_down();
            return Requests::Prepared{ node, gate };
        };
        auto request = [&] {
            auto result = requests.request(1, 0, factory);
            if (result.mReady)
                cache.remember(result.mValue);
            return result.mReady;
        };
        EXPECT_FALSE(request());
        produced.wait();
        EXPECT_FALSE(request());
        EXPECT_FALSE(cache.contains(node));
        gate->store(true, std::memory_order_release);
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!request() && std::chrono::steady_clock::now() < end)
            std::this_thread::yield();
        ASSERT_TRUE(cache.contains(node));
        requests.update(0.3);
        EXPECT_EQ(requests.size(), 0u);
        EXPECT_TRUE(cache.contains(node));
        EXPECT_EQ(calls, 1);
    }

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
