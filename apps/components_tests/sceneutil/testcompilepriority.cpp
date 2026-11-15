#include <components/sceneutil/compilepriority.hpp>

#include <osg/Group>
#include <osg/Texture2D>
#include <osg/Program>
#include <gtest/gtest.h>

namespace
{
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
