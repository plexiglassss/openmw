#include "attach.hpp"

#include <stdexcept>

#include <osg/FrontFace>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/NodeVisitor>
#include <osg/PositionAttitudeTransform>

#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/riggeometryosgaextension.hpp>
#include <components/sceneutil/skeleton.hpp>

#include "templateref.hpp"
#include "visitor.hpp"

namespace SceneUtil
{

    namespace
    {

        void mergeUserData(const osg::UserDataContainer* source, osg::Object* target)
        {
            if (!source)
                return;

            if (!target->getUserDataContainer())
                target->setUserDataContainer(osg::clone(source, osg::CopyOp::SHALLOW_COPY));
            else
            {
                for (unsigned int i = 0; i < source->getNumUserObjects(); ++i)
                    target->getUserDataContainer()->addUserObject(
                        osg::clone(source->getUserObject(i), osg::CopyOp::SHALLOW_COPY));
            }
        }

        osg::ref_ptr<osg::StateSet> makeFrontFaceStateSet()
        {
            osg::ref_ptr<osg::FrontFace> frontFace = new osg::FrontFace;
            frontFace->setMode(osg::FrontFace::CLOCKWISE);

            osg::ref_ptr<osg::StateSet> frontFaceStateSet = new osg::StateSet;
            frontFaceStateSet->setAttributeAndModes(frontFace, osg::StateAttribute::ON);
            return frontFaceStateSet;
        }
    }

    osg::ref_ptr<osg::Node> attach(osg::ref_ptr<const osg::Node> toAttach, osg::Node* master, std::string_view filter,
        osg::Group* attachNode, Resource::SceneManager* sceneManager, const osg::Quat* attitude)
    {
        if (dynamic_cast<const SceneUtil::Skeleton*>(toAttach.get()))
        {
            osg::ref_ptr<osg::Group> handle = new osg::Group;

            const auto plan = sceneManager->getRigAttachmentPlan(*toAttach, filter);
            for (const auto& node : plan->mNodes)
                handle->addChild(sceneManager->getInstance(node));
            // add a ref to the original template to hint to the cache that it is still being used and should be kept in
            // cache.
            SceneUtil::addTemplateRef(*handle, toAttach.get());

            if (handle->getNumChildren() == 1)
            {
                osg::ref_ptr<osg::Node> newHandle = handle->getChild(0);
                handle->removeChild(newHandle);
                master->asGroup()->addChild(newHandle);
                mergeUserData(toAttach->getUserDataContainer(), newHandle);
                return newHandle;
            }
            else
            {
                master->asGroup()->addChild(handle);
                mergeUserData(toAttach->getUserDataContainer(), handle);
                return handle;
            }
        }
        else
        {
            osg::ref_ptr<osg::Node> clonedToAttach = sceneManager->getInstance(toAttach);

            FindByNameVisitor findBoneOffset("BoneOffset");
            clonedToAttach->accept(findBoneOffset);

            osg::ref_ptr<osg::PositionAttitudeTransform> trans;

            if (findBoneOffset.mFoundNode)
            {
                osg::MatrixTransform* boneOffset = dynamic_cast<osg::MatrixTransform*>(findBoneOffset.mFoundNode);
                if (!boneOffset)
                    throw std::runtime_error("BoneOffset must be a MatrixTransform");

                trans = new osg::PositionAttitudeTransform;
                trans->setPosition(boneOffset->getMatrix().getTrans());

                // Now that we used it, get rid of the redundant node.
                if (boneOffset->getNumChildren() == 0 && boneOffset->getNumParents() == 1)
                    boneOffset->getParent(0)->removeChild(boneOffset);
            }

            if (attachNode->getName().find("Left") != std::string::npos)
            {
                if (!trans)
                    trans = new osg::PositionAttitudeTransform;
                trans->setScale(osg::Vec3f(-1.f, 1.f, 1.f));

                // Need to invert culling because of the negative scale
                // Note: for absolute correctness we would need to check the current front face for every mesh then
                // invert it However MW isn't doing this either, so don't. Assuming all meshes are using backface
                // culling is more efficient.
                static const osg::ref_ptr<osg::StateSet> frontFaceStateSet = makeFrontFaceStateSet();

                trans->setStateSet(frontFaceStateSet);
            }

            if (attitude)
            {
                if (!trans)
                    trans = new osg::PositionAttitudeTransform;
                trans->setAttitude(*attitude);
            }

            if (trans)
            {
                attachNode->addChild(trans);
                trans->addChild(clonedToAttach);
                return trans;
            }
            else
            {
                attachNode->addChild(clonedToAttach);
                return clonedToAttach;
            }
        }
    }

}
