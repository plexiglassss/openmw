#include <components/sceneutil/rigattachment.hpp>

#include <osg/Geometry>
#include <gtest/gtest.h>

namespace
{
    osg::ref_ptr<osg::Geometry> mesh(const char* name, bool rig = true)
    {
        osg::ref_ptr<osg::Geometry> value = new osg::Geometry;
        value->setName(name);
        value->setNodeMask(rig ? 1 : 2);
        return value;
    }
    const auto isRig = [](const osg::Drawable& node) { return node.getNodeMask() == 1; };

    TEST(ActorAttachmentPlan, PreservesCasePrefixFilteringAndDistinctSubgraphs)
    {
        osg::ref_ptr<osg::Group> root = new osg::Group;
        auto chest = mesh("Tri CHEST");
        auto hand = mesh("Hand");
        root->addChild(chest); root->addChild(hand); root->addChild(mesh("Chest", false));
        auto plan = SceneUtil::planRigAttachment(*root, "chest", isRig);
        ASSERT_EQ(plan.mNodes.size(), 1);
        EXPECT_EQ(plan.mNodes[0], chest);
        EXPECT_EQ(plan.mTemplate, root);
        EXPECT_TRUE(SceneUtil::planRigAttachment(*root, "missing", isRig).mNodes.empty());
    }

    TEST(ActorAttachmentPlan, DeduplicatesMatchingAncestorsInLegacyPointerOrder)
    {
        osg::ref_ptr<osg::Group> root = new osg::Group;
        osg::ref_ptr<osg::MatrixTransform> a = new osg::MatrixTransform, b = new osg::MatrixTransform;
        a->setName("Chest A"); b->setName("Tri chest B");
        a->addChild(mesh("Chest 1")); a->addChild(mesh("Tri chest 2"));
        b->addChild(mesh("Chest 3")); root->addChild(b); root->addChild(a);
        auto plan = SceneUtil::planRigAttachment(*root, "CHEST", isRig);
        std::set<osg::ref_ptr<const osg::Node>> expected{a, b};
        EXPECT_EQ(plan.mNodes, (std::vector<osg::ref_ptr<const osg::Node>>(expected.begin(), expected.end())));
    }

    TEST(ActorAttachmentPlan, KeepsSourceAliveWithoutOwningActors)
    {
        osg::ref_ptr<osg::Group> root = new osg::Group;
        auto child = mesh("Chest"); root->addChild(child);
        auto plan = SceneUtil::planRigAttachment(*root, "Chest", isRig);
        root = nullptr;
        EXPECT_NE(plan.mTemplate, nullptr);
        EXPECT_EQ(plan.mNodes[0], child);
        EXPECT_EQ(plan.mTemplate->getNumParents(), 0);
    }
}
