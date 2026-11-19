#include "templateref.hpp"
#include "riggeometry.hpp"

#include <osg/MatrixTransform>

#include <osgUtil/CullVisitor>

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/algorithm.hpp>

#include "skeleton.hpp"
#include "util.hpp"
#include "vertexattributes.hpp"

#include <cmath>
#include <mutex>

namespace SceneUtil
{
    namespace
    {
        bool finiteAffine(const osg::Matrixf& matrix)
        {
            for (unsigned int i = 0; i < 16; ++i)
                if (!std::isfinite(matrix.ptr()[i]))
                    return false;
            return matrix(0, 3) == 0.f && matrix(1, 3) == 0.f && matrix(2, 3) == 0.f && matrix(3, 3) == 1.f;
        }
    }

    // A program can be replaced after culling (transparent depth, custom passes, or shader hot reload).
    // Decide from the actual linked program at draw time, and retain a private CPU fallback per buffer.
    // Picking never touches these arrays, and the normal GPU path performs no vertex deformation here.
    class RigGeometry::GpuDrawCallback : public osg::Drawable::DrawCallback
    {
    public:
        GpuDrawCallback(osg::Geometry* source, std::shared_ptr<InfluenceData> data)
            : mSource(source)
            , mData(data)
            , mDisabled(new osg::Uniform("openmwRigEnabled", false))
        {
        }

        void setPose(const RigGeometry& rig)
        {
            mMatrices = rig.mPoseMatrices;
            mPresent = rig.mPoseBonePresent;
            mTransform = rig.mPoseTransform;
            ++mGeneration;
        }

        void drawImplementation(osg::RenderInfo& info, const osg::Drawable* drawable) const override
        {
            const auto* program = info.getState()->getLastAppliedProgramObject();
            static const unsigned int enabledId = osg::Uniform::getNameID("openmwRigEnabled");
            static const unsigned int transformId = osg::Uniform::getNameID("openmwRigTransform");
            static const unsigned int paletteId = osg::Uniform::getNameID("openmwRigPalette");
            const auto& uniforms = info.getState()->getUniformMap();
            const auto enabled = uniforms.find("openmwRigEnabled");
            const osg::Uniform* effective = enabled != uniforms.end() && !enabled->second.uniformVec.empty()
                ? enabled->second.uniformVec.back().first
                : mDisabled.get();
            bool enabledValue = false;
            effective->get(enabledValue);
            const auto hasUniform = [program](unsigned int id, GLenum type, GLint size) {
                if (!program || !program->isLinked())
                    return false;
                const auto& active = program->getActiveUniforms();
                const auto found = active.find(id);
                return found != active.end() && found->second._type == type && found->second._size >= size;
            };
            if (enabledValue && hasUniform(enabledId, GL_BOOL, 1) && hasUniform(transformId, GL_FLOAT_MAT4, 1)
                && hasUniform(paletteId, GL_FLOAT_MAT4, static_cast<GLint>(mMatrices.size()))
                && program->getAttribLocation("openmwRigWeights") == SkinWeightsAttribute
                && program->getAttribLocation("openmwRigIndices") == SkinIndicesAttribute)
            {
                drawable->drawImplementation(info);
                return;
            }
            // Serialise only the uncommon fallback for viewers with multiple graphics contexts.
            std::lock_guard lock(mMutex);
            // A custom program may contain the helper at different attribute bindings. Do not deform twice.
            // PerContextProgram::apply also updates OSG's uniform cache; restore the inherited value afterwards.
            if (hasUniform(enabledId, GL_BOOL, 1))
                program->apply(*mDisabled);
            getCpuGeometry()->draw(info);
            if (hasUniform(enabledId, GL_BOOL, 1))
                program->apply(*effective);
        }

        void accept(osg::PrimitiveFunctor& functor) const
        {
            std::lock_guard lock(mMutex);
            getCpuGeometry()->accept(functor);
        }

    private:
        osg::Geometry* getCpuGeometry() const
        {
            if (!mCpu)
            {
                mCpu = new RigGeometry;
                mCpu->mData = mData;
                mCpu->setSourceGeometry(mSource);
            }
            if (mCpu->mPoseGeneration != mGeneration)
            {
                mCpu->mPoseMatrices = mMatrices;
                mCpu->mPoseBonePresent = mPresent;
                mCpu->mPoseTransform = mTransform;
                mCpu->mPoseGeneration = mGeneration;
            }
            return mCpu->getCpuGeometry();
        }

        osg::ref_ptr<osg::Geometry> mSource;
        std::shared_ptr<InfluenceData> mData;
        osg::ref_ptr<osg::Uniform> mDisabled;
        std::vector<osg::Matrixf> mMatrices;
        std::vector<bool> mPresent;
        osg::Matrixf mTransform;
        std::uint64_t mGeneration = 0;
        mutable std::mutex mMutex;
        mutable osg::ref_ptr<RigGeometry> mCpu;
    };

    class RigGeometry::GpuGeometry : public osg::Geometry
    {
    public:
        GpuGeometry(const osg::Geometry& source)
            : osg::Geometry(source, osg::CopyOp::SHALLOW_COPY)
        {
        }

        void accept(osg::PrimitiveFunctor& functor) const override
        {
            // CullVisitor may defer primitive-based near/far fitting until after RigGeometry::cull.
            // It holds this child, not the wrapper; give it the frozen posed vertices as well.
            static_cast<const GpuDrawCallback*>(getDrawCallback())->accept(functor);
        }
    };

    RigGeometry::RigGeometry()
    {
        setNumChildrenRequiringUpdateTraversal(1);
        // update done in accept(NodeVisitor&)
    }

    RigGeometry::RigGeometry(const RigGeometry& copy, const osg::CopyOp& copyop)
        : Drawable(copy, copyop)
        , mData(copy.mData)
    {
        setSourceGeometry(copy.mSourceGeometry);
        setGpuSkinning(copy.getGpuSkinning());
        setNumChildrenRequiringUpdateTraversal(1);
    }

    void RigGeometry::setSourceGeometry(osg::ref_ptr<osg::Geometry> sourceGeometry)
    {
        setGpuSkinning(false);
        mPoseGeneration = 0;
        mCpuGeneration[0] = mCpuGeneration[1] = 0;
        mGpuPose = false;
        for (unsigned int i = 0; i < 2; ++i)
            mGeometry[i] = nullptr;

        mSourceGeometry = sourceGeometry;

        mSourceTangents = dynamic_cast<const osg::Vec4Array*>(sourceGeometry->getTexCoordArray(7));
        const auto* fallback
            = dynamic_cast<const osg::Vec4Array*>(sourceGeometry->getVertexAttribArray(FallbackTangentAttribute));
        bool marked = false;
        if (fallback)
            fallback->getUserValue(FallbackTangentUserValue, marked);
        mSourceFallbackTangents = marked ? fallback : nullptr;
    }

    osg::ref_ptr<osg::Geometry> RigGeometry::createCpuGeometry() const
    {
        const osg::Geometry& from = *mSourceGeometry;

        // DO NOT COPY AND PASTE THIS CODE. Cloning osg::Geometry without also cloning its contained Arrays is
        // generally unsafe. In this specific case the operation is safe under the following two assumptions:
        // - When Arrays are removed or replaced in the cloned geometry, the original Arrays in their place must
        // outlive the cloned geometry regardless. (ensured by mSourceGeometry)
        // - Arrays that we add or replace in the cloned geometry must be explicitely forbidden from reusing
        // BufferObjects of the original geometry. (ensured by vbo below)
        osg::ref_ptr<osg::Geometry> result = new osg::Geometry(from, osg::CopyOp::SHALLOW_COPY);
        result->setUserDataContainer(from.getUserDataContainer()
                ? static_cast<osg::UserDataContainer*>(from.getUserDataContainer()->clone(osg::CopyOp::SHALLOW_COPY))
                : new osg::DefaultUserDataContainer);
        SceneUtil::addTemplateRef(*result, mSourceGeometry.get());

        osg::Geometry& to = *result;
        to.setSupportsDisplayList(false);
        to.setUseVertexBufferObjects(true);
        to.setCullingActive(false); // make sure to disable culling since that's handled by this class
        auto* box = new CopyBoundingBoxCallback;
        box->boundingBox = _boundingBox;
        auto* sphere = new CopyBoundingSphereCallback;
        sphere->boundingSphere = _boundingSphere;
        to.setComputeBoundingBoxCallback(box);
        to.setComputeBoundingSphereCallback(sphere);

        // vertices and normals are modified every frame, so we need to deep copy them.
        // assign a dedicated VBO to make sure that modifications don't interfere with source geometry's VBO.
        osg::ref_ptr<osg::VertexBufferObject> vbo(new osg::VertexBufferObject);
        vbo->setUsage(GL_DYNAMIC_DRAW_ARB);

        osg::ref_ptr<osg::Array> vertexArray
            = static_cast<osg::Array*>(from.getVertexArray()->clone(osg::CopyOp::DEEP_COPY_ALL));
        if (vertexArray)
        {
            vertexArray->setVertexBufferObject(vbo);
            to.setVertexArray(vertexArray);
        }

        if (const osg::Array* normals = from.getNormalArray())
        {
            osg::ref_ptr<osg::Array> normalArray = static_cast<osg::Array*>(normals->clone(osg::CopyOp::DEEP_COPY_ALL));
            if (normalArray)
            {
                normalArray->setVertexBufferObject(vbo);
                to.setNormalArray(normalArray, osg::Array::BIND_PER_VERTEX);
            }
        }

        if (const osg::Vec4Array* tangents = dynamic_cast<const osg::Vec4Array*>(from.getTexCoordArray(7)))
        {
            osg::ref_ptr<osg::Array> tangentArray
                = static_cast<osg::Array*>(tangents->clone(osg::CopyOp::DEEP_COPY_ALL));
            tangentArray->setVertexBufferObject(vbo);
            to.setTexCoordArray(7, tangentArray, osg::Array::BIND_PER_VERTEX);
        }

        bool fallbackTangent = false;
        const auto* fallbackTangents
            = dynamic_cast<const osg::Vec4Array*>(from.getVertexAttribArray(FallbackTangentAttribute));
        if (fallbackTangents)
            fallbackTangents->getUserValue(FallbackTangentUserValue, fallbackTangent);
        if (!fallbackTangent)
            fallbackTangents = nullptr;
        if (fallbackTangents)
        {
            osg::ref_ptr<osg::Array> tangentArray
                = static_cast<osg::Array*>(fallbackTangents->clone(osg::CopyOp::DEEP_COPY_ALL));
            tangentArray->setVertexBufferObject(vbo);
            to.setVertexAttribArray(FallbackTangentAttribute, tangentArray, osg::Array::BIND_PER_VERTEX);
        }
        return result;
    }

    osg::ref_ptr<osg::Geometry> RigGeometry::getSourceGeometry() const
    {
        return mSourceGeometry;
    }

    bool RigGeometry::setGpuSkinning(bool enabled)
    {
        if (!enabled)
        {
            for (unsigned int i = 0; i < 2; ++i)
            {
                mGpuGeometry[i] = nullptr;
                mPaletteUniform[i] = nullptr;
                mTransformUniform[i] = nullptr;
            }
            mGpuPose = false;
            return false;
        }
        if (getGpuSkinning())
            return true;
        if (!mSourceGeometry || !mData || mData->mBones.empty() || mData->mBones.size() > SkinPaletteSize)
            return false;
        const auto& source = *mSourceGeometry;
        const auto* positions = dynamic_cast<const osg::Vec3Array*>(source.getVertexArray());
        const auto* normals = dynamic_cast<const osg::Vec3Array*>(source.getNormalArray());
        if (!positions || positions->empty() || positions->size() > 65536
            || (source.getNormalArray()
                && (!normals || normals->size() != positions->size()
                    || normals->getBinding() != osg::Array::BIND_PER_VERTEX))
            || source.getSecondaryColorArray() || source.getFogCoordArray()
            || source.getVertexAttribArray(SkinWeightsAttribute) || source.getVertexAttribArray(SkinIndicesAttribute)
            || source.getDrawCallback() || !finiteAffine(mData->mTransform))
            return false;
        for (const auto& bone : mData->mBones)
            if (!finiteAffine(bone.mInvBindMatrix))
                return false;

        osg::ref_ptr<osg::Vec4Array> weights = new osg::Vec4Array(positions->size());
        osg::ref_ptr<osg::Vec4Array> indices = new osg::Vec4Array(positions->size());
        // An empty influence list leaves the vertex entirely unchanged, including the root transform.
        std::fill(indices->begin(), indices->end(), osg::Vec4f(-1.f, 0.f, 0.f, 0.f));
        for (const auto& [influences, vertices] : mData->mInfluences)
        {
            if (influences.size() > 4)
                return false;
            osg::Vec4f vertexWeights;
            osg::Vec4f vertexIndices;
            for (unsigned int i = 0; i < influences.size(); ++i)
            {
                const auto& [index, weight] = influences[i];
                if (index >= mData->mBones.size() || !std::isfinite(weight))
                    return false;
                vertexIndices[i] = static_cast<float>(index);
                vertexWeights[i] = weight;
            }
            for (unsigned short vertex : vertices)
            {
                if (vertex >= positions->size())
                    return false;
                (*weights)[vertex] = vertexWeights;
                (*indices)[vertex] = vertexIndices;
            }
        }

        // These arrays never change and never borrow the source's VBO. Uniforms alone are double buffered.
        osg::ref_ptr<osg::VertexBufferObject> attributesVbo = new osg::VertexBufferObject;
        attributesVbo->setUsage(GL_STATIC_DRAW_ARB);
        weights->setVertexBufferObject(attributesVbo);
        indices->setVertexBufferObject(attributesVbo);
        for (unsigned int i = 0; i < 2; ++i)
        {
            osg::ref_ptr<osg::Geometry> geometry = new GpuGeometry(source);
            // Hold all original arrays for the lifetime of this shallow clone (including queued draw frames).
            geometry->setUserDataContainer(source.getUserDataContainer()
                    ? static_cast<osg::UserDataContainer*>(
                          source.getUserDataContainer()->clone(osg::CopyOp::SHALLOW_COPY))
                    : new osg::DefaultUserDataContainer);
            SceneUtil::addTemplateRef(*geometry, mSourceGeometry.get());
            geometry->setSupportsDisplayList(false);
            geometry->setUseVertexBufferObjects(true);
            geometry->setCullingActive(false);
            auto* box = new CopyBoundingBoxCallback;
            box->boundingBox = _boundingBox;
            auto* sphere = new CopyBoundingSphereCallback;
            sphere->boundingSphere = _boundingSphere;
            geometry->setComputeBoundingBoxCallback(box);
            geometry->setComputeBoundingSphereCallback(sphere);
            geometry->setVertexAttribArray(SkinWeightsAttribute, weights, osg::Array::BIND_PER_VERTEX);
            geometry->setVertexAttribArray(SkinIndicesAttribute, indices, osg::Array::BIND_PER_VERTEX);
            // Never mutate a source/shared StateSet, even when ShaderVisitor is allowed to do so.
            osg::ref_ptr<osg::StateSet> state = source.getStateSet()
                ? new osg::StateSet(*source.getStateSet(), osg::CopyOp::SHALLOW_COPY)
                : new osg::StateSet;
            state->addUniform(new osg::Uniform("openmwRigEnabled", true));
            mPaletteUniform[i] = new osg::Uniform(osg::Uniform::FLOAT_MAT4, "openmwRigPalette", SkinPaletteSize);
            mTransformUniform[i] = new osg::Uniform("openmwRigTransform", osg::Matrixf());
            state->addUniform(mPaletteUniform[i]);
            state->addUniform(mTransformUniform[i]);
            geometry->setStateSet(state);
            geometry->setDrawCallback(new GpuDrawCallback(mSourceGeometry, mData));
            mGpuGeometry[i] = std::move(geometry);
        }
        // Shader recreation can occur between culls. Publish the new buffers on the next cull, never half a pose.
        mNeedsPoseUpdate = true;
        mGpuPose = false;
        return true;
    }

    bool RigGeometry::initFromParentSkeleton(osg::NodeVisitor* nv)
    {
        const osg::NodePath& path = nv->getNodePath();
        for (osg::NodePath::const_reverse_iterator it = path.rbegin() + 1; it != path.rend(); ++it)
        {
            osg::Node* node = *it;
            if (node->asTransform())
                continue;
            if (Skeleton* skel = dynamic_cast<Skeleton*>(node))
            {
                mSkeleton = skel;
                break;
            }
        }

        if (!mSkeleton)
        {
            Log(Debug::Error) << "Error: A RigGeometry did not find its parent skeleton";
            return false;
        }

        if (!mData)
        {
            Log(Debug::Error) << "Error: No influence data set on RigGeometry";
            return false;
        }

        mNodes.clear();
        for (const BoneInfo& info : mData->mBones)
        {
            mNodes.push_back(mSkeleton->getBone(info.mName));
            if (!mNodes.back())
                Log(Debug::Error) << "Error: RigGeometry did not find bone " << info.mName;
        }

        return true;
    }

    void RigGeometry::cull(osg::NodeVisitor* nv)
    {
        if (!mSkeleton)
        {
            Log(Debug::Error)
                << "Error: RigGeometry rendering with no skeleton, should have been initialized by UpdateVisitor";
            // try to recover anyway, though rendering is likely to be incorrect.
            if (!initFromParentSkeleton(nv))
                return;
        }

        unsigned int traversalNumber = nv->getTraversalNumber();
        if (!mNeedsPoseUpdate && mPoseGeneration != 0
            && (mLastFrameNumber == traversalNumber || !mSkeleton->getActive()))
        {
            osg::Geometry& geom = *getGeometry();
            nv->pushOntoNodePath(&geom);
            nv->apply(geom);
            nv->popFromNodePath();
            return;
        }
        mLastFrameNumber = traversalNumber;
        // Inactive skeletons may submit the same buffer for many frames. Alternate from the last
        // published buffer, not frame parity, so reactivation never edits the buffer still being drawn.
        mCurrentBuffer ^= 1;

        mSkeleton->updateBoneMatrices(traversalNumber);

        // Reuse scratch storage and retain the exact pose for lazy CPU intersection queries.
        mPoseMatrices.resize(mNodes.size());
        mPoseBonePresent.resize(mNodes.size());
        bool affine = true;
        for (std::size_t i = 0; i < mNodes.size(); ++i)
        {
            mPoseBonePresent[i] = mNodes[i] != nullptr;
            if (mNodes[i])
            {
                mPoseMatrices[i] = mData->mBones[i].mInvBindMatrix * mNodes[i]->mMatrixInSkeletonSpace;
                affine = affine && finiteAffine(mPoseMatrices[i]);
            }
            else
                mPoseMatrices[i] = osg::Matrixf(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }
        if (mSkinToSkelMatrix)
            mPoseTransform = *mSkinToSkelMatrix * mData->mTransform;
        else
            mPoseTransform = mData->mTransform;
        ++mPoseGeneration;
        mNeedsPoseUpdate = false;
        mGpuPose = getGpuSkinning() && affine && finiteAffine(mPoseTransform);
        if (mGpuPose)
        {
            const unsigned int buffer = mCurrentBuffer;
            for (unsigned int i = 0; i < mPoseMatrices.size(); ++i)
                mPaletteUniform[buffer]->setElement(i, mPoseMatrices[i]);
            mTransformUniform[buffer]->set(mPoseTransform);
            static_cast<GpuDrawCallback*>(mGpuGeometry[buffer]->getDrawCallback())->setPose(*this);
        }
        else
            getCpuGeometry();

        osg::Geometry& geom = *getGeometry();
        nv->pushOntoNodePath(&geom);
        nv->apply(geom);
        nv->popFromNodePath();
    }

    void RigGeometry::skinCpu(osg::Geometry& geom) const
    {
        // skinning
        const osg::Vec3Array* positionSrc = static_cast<osg::Vec3Array*>(mSourceGeometry->getVertexArray());
        const osg::Vec3Array* normalSrc = static_cast<osg::Vec3Array*>(mSourceGeometry->getNormalArray());
        const osg::Vec4Array* tangentSrc = mSourceTangents;

        osg::Vec3Array* positionDst = static_cast<osg::Vec3Array*>(geom.getVertexArray());
        osg::Vec3Array* normalDst = static_cast<osg::Vec3Array*>(geom.getNormalArray());
        osg::Vec4Array* tangentDst = static_cast<osg::Vec4Array*>(geom.getTexCoordArray(7));
        auto* fallbackTangentDst = mSourceFallbackTangents
            ? static_cast<osg::Vec4Array*>(geom.getVertexAttribArray(FallbackTangentAttribute))
            : nullptr;

        for (const auto& [influences, vertices] : mData->mInfluences)
        {
            osg::Matrixf resultMat(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

            for (const auto& [index, weight] : influences)
            {
                if (!mPoseBonePresent[index])
                    continue;
                const float* boneMatPtr = mPoseMatrices[index].ptr();
                float* resultMatPtr = resultMat.ptr();
                for (int i = 0; i < 16; ++i, ++resultMatPtr, ++boneMatPtr)
                    if (i % 4 != 3)
                        *resultMatPtr += *boneMatPtr * weight;
            }

            resultMat *= mPoseTransform;

            for (unsigned short vertex : vertices)
            {
                (*positionDst)[vertex] = resultMat.preMult((*positionSrc)[vertex]);
                if (normalDst)
                    (*normalDst)[vertex] = osg::Matrixf::transform3x3((*normalSrc)[vertex], resultMat);

                if (tangentDst)
                {
                    const osg::Vec4f& srcTangent = (*tangentSrc)[vertex];
                    osg::Vec3f transformedTangent = osg::Matrixf::transform3x3(
                        osg::Vec3f(srcTangent.x(), srcTangent.y(), srcTangent.z()), resultMat);
                    (*tangentDst)[vertex] = osg::Vec4f(transformedTangent, srcTangent.w());
                }
                if (fallbackTangentDst)
                {
                    const osg::Vec4f& srcTangent = (*mSourceFallbackTangents)[vertex];
                    osg::Vec3f transformedTangent = osg::Matrixf::transform3x3(
                        osg::Vec3f(srcTangent.x(), srcTangent.y(), srcTangent.z()), resultMat);
                    (*fallbackTangentDst)[vertex] = osg::Vec4f(transformedTangent, srcTangent.w());
                }
            }
        }

        positionDst->dirty();
        if (normalDst)
            normalDst->dirty();
        if (tangentDst)
            tangentDst->dirty();
        if (fallbackTangentDst)
            fallbackTangentDst->dirty();

        geom.osg::Drawable::dirtyGLObjects();
    }

    void RigGeometry::updateBounds(osg::NodeVisitor* nv)
    {
        if (!mSkeleton)
        {
            if (!initFromParentSkeleton(nv))
                return;
        }

        if (!mSkeleton->getActive() && !mBoundsFirstFrame)
            return;
        mBoundsFirstFrame = false;

        mSkeleton->updateBoneMatrices(nv->getTraversalNumber());

        updateSkinToSkelMatrix(nv->getNodePath());

        osg::BoundingBox box;
        osg::Matrixf transform;
        if (mSkinToSkelMatrix)
            transform = (*mSkinToSkelMatrix) * mData->mTransform;
        else
            transform = mData->mTransform;

        size_t index = 0;
        for (const BoneInfo& info : mData->mBones)
        {
            const Bone* bone = mNodes[index++];
            if (bone == nullptr)
                continue;

            osg::BoundingSpheref bs = info.mBoundSphere;
            transformBoundingSphere(bone->mMatrixInSkeletonSpace * transform, bs);
            box.expandBy(bs);
        }

        if (box != _boundingBox)
        {
            _boundingBox = box;
            _boundingSphere = osg::BoundingSphere(_boundingBox);
            _boundingSphereComputed = true;
            for (unsigned int i = 0; i < getNumParents(); ++i)
                getParent(i)->dirtyBound();

            for (unsigned int i = 0; i < 2; ++i)
            {
                if (mGeometry[i])
                {
                    osg::Geometry& geom = *mGeometry[i];
                    static_cast<CopyBoundingBoxCallback*>(geom.getComputeBoundingBoxCallback())->boundingBox
                        = _boundingBox;
                    static_cast<CopyBoundingSphereCallback*>(geom.getComputeBoundingSphereCallback())->boundingSphere
                        = _boundingSphere;
                    geom.dirtyBound();
                }
                if (mGpuGeometry[i])
                {
                    osg::Geometry& gpu = *mGpuGeometry[i];
                    static_cast<CopyBoundingBoxCallback*>(gpu.getComputeBoundingBoxCallback())->boundingBox
                        = _boundingBox;
                    static_cast<CopyBoundingSphereCallback*>(gpu.getComputeBoundingSphereCallback())->boundingSphere
                        = _boundingSphere;
                    gpu.dirtyBound();
                }
            }
        }
    }

    void RigGeometry::updateSkinToSkelMatrix(const osg::NodePath& nodePath)
    {
        if (mSkinToSkelMatrix)
            mSkinToSkelMatrix->makeIdentity();
        auto skeletonRoot = std::find(nodePath.begin(), nodePath.end(), mSkeleton);
        if (skeletonRoot == nodePath.end())
            return;
        skeletonRoot++;
        auto skinRoot = nodePath.end();
        if (!mData->mRootBone.empty())
            skinRoot = std::find_if(skeletonRoot, nodePath.end(),
                [&](const osg::Node* node) { return Misc::StringUtils::ciEqual(node->getName(), mData->mRootBone); });
        if (skinRoot == nodePath.end())
        {
            // Failed to find skin root, cancel out everything up till the trishape.
            // Our parent node is the trishape's transform
            skinRoot = nodePath.end() - 2;
            if ((*skinRoot)->getName() != getName()) // but maybe it can get optimized out
                skinRoot++;
        }
        else
            skinRoot++;
        for (auto it = skeletonRoot; it != skinRoot; ++it)
        {
            const osg::Node* node = *it;
            if (const osg::Transform* trans = node->asTransform())
            {
                const osg::MatrixTransform* matrixTrans = trans->asMatrixTransform();
                if (matrixTrans && matrixTrans->getMatrix().isIdentity())
                    continue;
                if (!mSkinToSkelMatrix)
                    mSkinToSkelMatrix = new osg::RefMatrix;
                trans->computeWorldToLocalMatrix(*mSkinToSkelMatrix, nullptr);
            }
        }
    }

    void RigGeometry::setBoneInfo(std::vector<BoneInfo>&& bones)
    {
        setGpuSkinning(false);
        if (!mData)
            mData = std::make_shared<InfluenceData>();

        mData->mBones = std::move(bones);
    }

    void RigGeometry::setInfluences(const std::vector<BoneWeights>& influences)
    {
        setGpuSkinning(false);
        if (!mData)
            mData = std::make_shared<InfluenceData>();

        std::map<BoneWeights, VertexList> influencesToVertices;
        for (size_t i = 0; i < influences.size(); i++)
            influencesToVertices[influences[i]].emplace_back(static_cast<VertexList::value_type>(i));
        influencesToVertices.erase(BoneWeights());

        mData->mInfluences.reserve(influencesToVertices.size());
        mData->mInfluences.assign(influencesToVertices.begin(), influencesToVertices.end());
    }

    void RigGeometry::setTransform(osg::Matrixf&& transform)
    {
        if (!mData)
            mData = std::make_shared<InfluenceData>();
        mData->mTransform = transform;
    }

    void RigGeometry::setRootBone(std::string_view name)
    {
        if (!mData)
            mData = std::make_shared<InfluenceData>();
        mData->mRootBone = name;
    }

    void RigGeometry::accept(osg::NodeVisitor& nv)
    {
        if (!nv.validNodeMask(*this))
            return;

        nv.pushOntoNodePath(this);

        if (nv.getVisitorType() == osg::NodeVisitor::CULL_VISITOR)
        {
            // The cull visitor won't be applied to the node itself,
            // but we want to use its state to render the child geometry.
            osg::StateSet* stateset = getStateSet();
            osgUtil::CullVisitor* cv = static_cast<osgUtil::CullVisitor*>(&nv);
            if (stateset)
                cv->pushStateSet(stateset);

            cull(&nv);
            if (stateset)
                cv->popStateSet();
        }
        else if (nv.getVisitorType() == osg::NodeVisitor::UPDATE_VISITOR)
            updateBounds(&nv);
        else
            nv.apply(*this);

        nv.popFromNodePath();
    }

    void RigGeometry::accept(osg::PrimitiveFunctor& func) const
    {
        getCpuGeometry()->accept(func);
    }

    osg::Geometry* RigGeometry::getCpuGeometry() const
    {
        const unsigned int buffer = mCurrentBuffer;
        if (!mGeometry[buffer])
            mGeometry[buffer] = createCpuGeometry();
        if (mCpuGeneration[buffer] != mPoseGeneration)
        {
            skinCpu(*mGeometry[buffer]);
            mCpuGeneration[buffer] = mPoseGeneration;
        }
        return mGeometry[buffer];
    }

    osg::Geometry* RigGeometry::getGeometry() const
    {
        return mGpuPose ? mGpuGeometry[mCurrentBuffer].get() : getCpuGeometry();
    }

}
