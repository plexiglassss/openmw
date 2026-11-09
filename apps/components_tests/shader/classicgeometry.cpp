#include <components/sceneutil/optimizer.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/riggeometryosgaextension.hpp>
#include <components/sceneutil/skeleton.hpp>
#include <components/sceneutil/vertexattributes.hpp>

#include <gtest/gtest.h>

#include <osg/Geometry>
#include <osg/Group>
#include <osg/Math>
#include <osg/MatrixTransform>
#include <osg/ref_ptr>
#include <osgUtil/CullVisitor>
#include <osgUtil/UpdateVisitor>

#include <vector>

namespace
{
    osg::ref_ptr<osg::Geometry> triangle(bool markClassic = true)
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        vertices->push_back(osg::Vec3f(0.f, 0.f, 0.f));
        vertices->push_back(osg::Vec3f(1.f, 0.f, 0.f));
        vertices->push_back(osg::Vec3f(0.f, 1.f, 0.f));
        geometry->setVertexArray(vertices);
        osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array(3);
        osg::ref_ptr<osg::Vec4Array> tangents = new osg::Vec4Array(3);
        osg::ref_ptr<osg::Vec4Array> classic = new osg::Vec4Array(3);
        for (unsigned int i = 0; i < 3; ++i)
        {
            (*normals)[i] = osg::Vec3f(0.f, 0.f, 1.f);
            (*tangents)[i] = osg::Vec4f(1.f, 0.f, 0.f, 1.f);
            (*classic)[i] = osg::Vec4f(0.f, 1.f, 0.f, -1.f);
        }
        if (markClassic)
            classic->setUserValue(SceneUtil::ClassicTangentUserValue, true);
        geometry->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
        geometry->setTexCoordArray(7, tangents, osg::Array::BIND_PER_VERTEX);
        geometry->setVertexAttribArray(SceneUtil::ClassicTangentAttribute, classic, osg::Array::BIND_PER_VERTEX);
        geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));
        geometry->setDataVariance(osg::Object::STATIC);
        return geometry;
    }

    osg::Vec4Array* classic(osg::Geometry& geometry)
    {
        return dynamic_cast<osg::Vec4Array*>(geometry.getVertexAttribArray(SceneUtil::ClassicTangentAttribute));
    }

    void expectTangents(const osg::Vec4Array* actual, const osg::Vec4f& expected)
    {
        ASSERT_NE(actual, nullptr);
        ASSERT_EQ(actual->size(), 3u);
        for (const osg::Vec4f& tangent : *actual)
        {
            EXPECT_NEAR(tangent.x(), expected.x(), 0.00001f);
            EXPECT_NEAR(tangent.y(), expected.y(), 0.00001f);
            EXPECT_NEAR(tangent.z(), expected.z(), 0.00001f);
            EXPECT_FLOAT_EQ(tangent.w(), expected.w());
        }
    }

    void expectSourceUnchanged(osg::Geometry& source)
    {
        expectTangents(dynamic_cast<osg::Vec4Array*>(source.getTexCoordArray(7)), osg::Vec4f(1.f, 0.f, 0.f, 1.f));
        expectTangents(classic(source), osg::Vec4f(0.f, 1.f, 0.f, -1.f));
        const auto* vertices = dynamic_cast<const osg::Vec3Array*>(source.getVertexArray());
        ASSERT_NE(vertices, nullptr);
        EXPECT_EQ((*vertices)[1], osg::Vec3f(1.f, 0.f, 0.f));
    }

    // Use the real CullVisitor type required by RigGeometry, but capture its
    // emitted draw geometry without invoking GL or renderer camera state.
    struct CaptureGeometry final : osgUtil::CullVisitor
    {
        std::vector<osg::ref_ptr<osg::Geometry>> mCaptured;

        void apply(osg::Node& node) override { traverse(node); }
        void apply(osg::Group& node) override { traverse(node); }
        void apply(osg::MatrixTransform& node) override { traverse(node); }
        void apply(osg::Geometry& geometry) override { mCaptured.emplace_back(&geometry); }
    };

    struct NifFixture
    {
        osg::ref_ptr<osg::Geometry> mSource;
        osg::ref_ptr<SceneUtil::RigGeometry> mRig;
        osg::ref_ptr<SceneUtil::Skeleton> mSkeleton = new SceneUtil::Skeleton;
        osg::ref_ptr<osg::MatrixTransform> mBone = new osg::MatrixTransform;

        explicit NifFixture(osg::ref_ptr<SceneUtil::RigGeometry> existing = nullptr)
        {
            if (existing)
            {
                mRig = existing;
                mSource = mRig->getSourceGeometry();
            }
            else
            {
                mSource = triangle();
                mRig = new SceneUtil::RigGeometry;
                mRig->setSourceGeometry(mSource);
                mRig->setBoneInfo(
                    { { "root", osg::BoundingSpheref(osg::Vec3f(), 2.f), osg::Matrixf::identity() } });
                std::vector<SceneUtil::RigGeometry::BoneWeights> influences(3);
                for (auto& weights : influences)
                    weights.emplace_back(0u, 1.f);
                mRig->setInfluences(influences);
                mRig->setTransform(osg::Matrixf::identity());
            }
            mRig->setName("RigFixture");
            mBone->setName("root");
            mSkeleton->addChild(mBone);
            osg::ref_ptr<osg::Group> shape = new osg::Group;
            shape->setName("RigFixture");
            shape->addChild(mRig);
            mSkeleton->addChild(shape);
        }

        osg::Geometry* render(unsigned int frame, double degrees)
        {
            mBone->setMatrix(osg::Matrix::rotate(osg::DegreesToRadians(degrees), osg::Vec3d(0., 0., 1.)));
            osgUtil::UpdateVisitor update;
            update.setTraversalNumber(frame);
            mSkeleton->accept(update);
            CaptureGeometry cull;
            cull.setTraversalNumber(frame);
            mSkeleton->accept(cull);
            EXPECT_EQ(cull.mCaptured.size(), 1u);
            return cull.mCaptured.size() == 1 ? cull.mCaptured.front().get() : nullptr;
        }
    };

    TEST(ClassicInteriorGeometryTest, nif_skinning_rotates_both_tangents_and_isolates_draw_buffers)
    {
        NifFixture fixture;
        osg::Geometry* first = fixture.render(1, 90.);
        ASSERT_NE(first, nullptr);
        osg::Vec4Array* firstClassic = classic(*first);
        ASSERT_NE(firstClassic, nullptr);
        EXPECT_NE(firstClassic, classic(*fixture.mSource));
        expectTangents(firstClassic, osg::Vec4f(-1.f, 0.f, 0.f, -1.f));
        expectTangents(dynamic_cast<osg::Vec4Array*>(first->getTexCoordArray(7)), osg::Vec4f(0.f, 1.f, 0.f, 1.f));

        osg::Geometry* second = fixture.render(2, 180.);
        ASSERT_NE(second, nullptr);
        ASSERT_NE(second, first);
        osg::Vec4Array* secondClassic = classic(*second);
        ASSERT_NE(secondClassic, nullptr);
        EXPECT_NE(secondClassic, firstClassic);
        EXPECT_NE(secondClassic, classic(*fixture.mSource));
        EXPECT_NE(secondClassic->getVertexBufferObject(), firstClassic->getVertexBufferObject());
        expectTangents(secondClassic, osg::Vec4f(0.f, -1.f, 0.f, -1.f));
        expectTangents(firstClassic, osg::Vec4f(-1.f, 0.f, 0.f, -1.f));
        expectTangents(dynamic_cast<osg::Vec4Array*>(second->getTexCoordArray(7)), osg::Vec4f(-1.f, 0.f, 0.f, 1.f));

        EXPECT_EQ(fixture.render(3, 270.), first);
        expectTangents(firstClassic, osg::Vec4f(1.f, 0.f, 0.f, -1.f));
        expectTangents(secondClassic, osg::Vec4f(0.f, -1.f, 0.f, -1.f));
        expectSourceUnchanged(*fixture.mSource);
    }

    TEST(ClassicInteriorGeometryTest, shallow_rig_clones_keep_template_and_draw_arrays_independent)
    {
        NifFixture original;
        osg::Geometry* originalDraw = original.render(1, 90.);
        ASSERT_NE(originalDraw, nullptr);
        osg::ref_ptr<SceneUtil::RigGeometry> rigClone
            = new SceneUtil::RigGeometry(*original.mRig, osg::CopyOp::SHALLOW_COPY);
        NifFixture clone(rigClone);
        EXPECT_EQ(clone.mSource, original.mSource);
        osg::Geometry* cloneDraw = clone.render(1, 180.);
        ASSERT_NE(cloneDraw, nullptr);
        EXPECT_NE(classic(*cloneDraw), classic(*originalDraw));
        EXPECT_NE(classic(*cloneDraw), classic(*original.mSource));
        expectTangents(classic(*cloneDraw), osg::Vec4f(0.f, -1.f, 0.f, -1.f));
        expectTangents(classic(*originalDraw), osg::Vec4f(-1.f, 0.f, 0.f, -1.f));
        expectSourceUnchanged(*original.mSource);
    }

    TEST(ClassicInteriorGeometryTest, osga_draw_buffers_do_not_share_marked_tangent_arrays)
    {
        osg::ref_ptr<osg::Geometry> base = triangle();
        osg::ref_ptr<SceneUtil::OsgaRigGeometry> source = new SceneUtil::OsgaRigGeometry;
        source->setVertexArray(base->getVertexArray());
        source->setNormalArray(base->getNormalArray(), osg::Array::BIND_PER_VERTEX);
        source->setTexCoordArray(7, base->getTexCoordArray(7), osg::Array::BIND_PER_VERTEX);
        source->setVertexAttribArray(
            SceneUtil::ClassicTangentAttribute, base->getVertexAttribArray(SceneUtil::ClassicTangentAttribute),
            osg::Array::BIND_PER_VERTEX);
        osg::ref_ptr<SceneUtil::RigGeometry::CopyBoundingBoxCallback> bounds
            = new SceneUtil::RigGeometry::CopyBoundingBoxCallback;
        bounds->boundingBox = base->getBoundingBox();
        source->setComputeBoundingBoxCallback(bounds);
        SceneUtil::RigGeometryHolder holder;
        holder.setSourceRigGeometry(source);
        osg::Vec4Array* first = classic(*holder.getGeometry(0));
        osg::Vec4Array* second = classic(*holder.getGeometry(1));
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        EXPECT_NE(first, second);
        EXPECT_NE(first, classic(*source));
        EXPECT_NE(second, classic(*source));
        EXPECT_NE(first->getVertexBufferObject(), second->getVertexBufferObject());
        (*first)[0] = osg::Vec4f(7.f, 8.f, 9.f, -1.f);
        expectTangents(second, osg::Vec4f(0.f, 1.f, 0.f, -1.f));
        expectSourceUnchanged(*base);
    }

    TEST(ClassicInteriorGeometryTest, static_flatten_rotates_marked_tangents_without_mutating_source)
    {
        osg::ref_ptr<osg::Geometry> source = triangle();
        osg::ref_ptr<osg::Geometry> copy = new osg::Geometry(*source, osg::CopyOp::SHALLOW_COPY);
        osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform;
        transform->setDataVariance(osg::Object::STATIC);
        transform->setMatrix(osg::Matrix::rotate(osg::DegreesToRadians(90.), osg::Vec3d(0., 0., 1.)));
        transform->addChild(copy);
        osg::ref_ptr<osg::Group> root = new osg::Group;
        root->addChild(transform);
        SceneUtil::Optimizer optimizer;
        optimizer.optimize(root, SceneUtil::Optimizer::FLATTEN_STATIC_TRANSFORMS);
        ASSERT_NE(classic(*copy), nullptr);
        EXPECT_NE(classic(*copy), classic(*source));
        expectTangents(classic(*copy), osg::Vec4f(-1.f, 0.f, 0.f, -1.f));
        expectTangents(dynamic_cast<osg::Vec4Array*>(copy->getTexCoordArray(7)), osg::Vec4f(0.f, 1.f, 0.f, 1.f));
        bool marked = false;
        EXPECT_TRUE(classic(*copy)->getUserValue(SceneUtil::ClassicTangentUserValue, marked));
        EXPECT_TRUE(marked);
        expectSourceUnchanged(*source);
    }

    TEST(ClassicInteriorGeometryTest, static_flatten_preserves_unmarked_attribute_six)
    {
        osg::ref_ptr<osg::Geometry> source = triangle(false);
        osg::ref_ptr<osg::Geometry> copy = new osg::Geometry(*source, osg::CopyOp::SHALLOW_COPY);
        osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform;
        transform->setDataVariance(osg::Object::STATIC);
        transform->setMatrix(osg::Matrix::rotate(osg::DegreesToRadians(90.), osg::Vec3d(0., 0., 1.)));
        transform->addChild(copy);
        osg::ref_ptr<osg::Group> root = new osg::Group;
        root->addChild(transform);
        SceneUtil::Optimizer optimizer;
        optimizer.optimize(root, SceneUtil::Optimizer::FLATTEN_STATIC_TRANSFORMS);
        expectTangents(classic(*copy), osg::Vec4f(0.f, 1.f, 0.f, -1.f));
        expectSourceUnchanged(*source);
    }
}

