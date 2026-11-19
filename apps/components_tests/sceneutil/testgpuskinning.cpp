#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/shadowsbin.hpp>
#include <components/sceneutil/skeleton.hpp>
#include <components/sceneutil/vertexattributes.hpp>

#include <gtest/gtest.h>

#include <osg/Geode>
#include <osg/MatrixTransform>
#include <osg/TriangleFunctor>
#include <osgUtil/CullVisitor>
#include <osgUtil/RenderLeaf>
#include <osgUtil/RenderStage>
#include <osgUtil/StateGraph>

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
    struct Triangles
    {
        std::vector<osg::Vec3f> vertices;
        void operator()(const osg::Vec3f& a, const osg::Vec3f& b, const osg::Vec3f& c)
        {
            vertices.insert(vertices.end(), { a, b, c });
        }
    };

    std::vector<osg::Vec3f> posedVertices(const osg::Drawable& drawable)
    {
        osg::TriangleFunctor<Triangles> collector;
        drawable.accept(collector);
        return collector.vertices;
    }

    class CaptureCull : public osgUtil::CullVisitor
    {
    public:
        CaptureCull()
        {
            setStateGraph(new osgUtil::StateGraph);
            setRenderStage(new osgUtil::RenderStage);
        }
        void apply(osg::Node& node) override { traverse(node); }
        void apply(osg::Group& node) override { traverse(node); }
        void apply(osg::Geode& node) override { traverse(node); }
        void apply(osg::MatrixTransform& node) override { traverse(node); }
        void apply(osg::Geometry& geometry) override { captured = &geometry; }
        osg::ref_ptr<osg::Geometry> captured;
    };

    struct RigFixture
    {
        osg::ref_ptr<SceneUtil::Skeleton> skeleton = new SceneUtil::Skeleton;
        osg::ref_ptr<osg::MatrixTransform> bone = new osg::MatrixTransform;
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        osg::ref_ptr<osg::Geometry> source = new osg::Geometry;
        osg::ref_ptr<SceneUtil::RigGeometry> rig = new SceneUtil::RigGeometry;

        RigFixture()
        {
            auto* positions = new osg::Vec3Array;
            positions->push_back({ -0.8f, -0.3f, 0.f });
            positions->push_back({ -0.2f, -0.3f, 0.f });
            positions->push_back({ -0.5f, 0.3f, 0.f });
            auto* normals = new osg::Vec3Array;
            normals->assign(3, osg::Vec3f(0, 0, 1));
            auto* tangents = new osg::Vec4Array;
            tangents->assign(3, osg::Vec4f(1, 0, 0, -1));
            auto* fallback = new osg::Vec4Array;
            fallback->assign(3, osg::Vec4f(0, 1, 0, 1));
            fallback->setUserValue(SceneUtil::FallbackTangentUserValue, true);
            source->setVertexArray(positions);
            source->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
            source->setTexCoordArray(7, tangents, osg::Array::BIND_PER_VERTEX);
            source->setVertexAttribArray(SceneUtil::FallbackTangentAttribute, fallback, osg::Array::BIND_PER_VERTEX);
            source->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));
            source->setUseVertexBufferObjects(true);
            source->setUseDisplayList(false);
            rig->setSourceGeometry(source);
            rig->setBoneInfo({ { "bone", osg::BoundingSpheref(osg::Vec3f(), 2.f), osg::Matrixf() } });
            rig->setInfluences({ { { 0, 1.f } }, { { 0, 1.f } }, { { 0, 1.f } } });
            rig->setTransform(osg::Matrixf());
            bone->setName("bone");
            skeleton->addChild(bone);
            skeleton->addChild(geode);
            geode->addDrawable(rig);
        }

        osg::ref_ptr<osg::Geometry> frame(unsigned int number)
        {
            osg::NodeVisitor update(osg::NodeVisitor::UPDATE_VISITOR, osg::NodeVisitor::TRAVERSE_ALL_CHILDREN);
            update.setTraversalNumber(number);
            skeleton->accept(update);
            CaptureCull cull;
            cull.setTraversalNumber(number);
            skeleton->accept(cull);
            return cull.captured;
        }
    };

    TEST(GpuSkinning, PreservesCpuPickingAndDeferredPrimitiveBounds)
    {
        RigFixture fixture;
        const auto original = posedVertices(*fixture.rig);
        ASSERT_TRUE(fixture.rig->setGpuSkinning(true));
        EXPECT_EQ(posedVertices(*fixture.rig), original);
        fixture.bone->setMatrix(osg::Matrix::translate(1, 2, 3));
        auto rendered = fixture.frame(1);
        ASSERT_TRUE(rendered);
        // Render vertices stay immutable; both primitive interfaces expose the published skinned pose.
        EXPECT_EQ(rendered->getVertexArray(), fixture.source->getVertexArray());
        auto picked = posedVertices(*fixture.rig);
        ASSERT_EQ(picked.size(), original.size());
        for (std::size_t i = 0; i < original.size(); ++i)
            EXPECT_EQ(picked[i], original[i] + osg::Vec3f(1, 2, 3));
        EXPECT_EQ(posedVertices(*rendered), picked);
        fixture.bone->setMatrix(osg::Matrix::translate(9, 9, 9));
        EXPECT_EQ(posedVertices(*fixture.rig), picked); // no cull yet
        EXPECT_EQ(posedVertices(*rendered), picked);
        fixture.rig->setGpuSkinning(false);
        auto cpuFrame = fixture.frame(2);
        EXPECT_EQ(posedVertices(*cpuFrame), posedVertices(*fixture.rig));
        EXPECT_NE(posedVertices(*fixture.rig), picked);
    }

    TEST(GpuSkinning, PreservesEmptyMissingAndNonNormalizedInfluences)
    {
        RigFixture fixture;
        fixture.rig->setBoneInfo({ { "bone", osg::BoundingSpheref(osg::Vec3f(), 2.f), osg::Matrixf() },
            { "missing", osg::BoundingSpheref(osg::Vec3f(), 2.f), osg::Matrixf() } });
        fixture.rig->setInfluences({ {}, { { 0, .25f } }, { { 1, 1.f } } });
        fixture.rig->setTransform(osg::Matrixf::translate(2, 3, 4));
        ASSERT_TRUE(fixture.rig->setGpuSkinning(true));
        fixture.bone->setMatrix(osg::Matrix::translate(4, 0, 0));
        auto rendered = fixture.frame(1);
        const auto vertices = posedVertices(*fixture.rig);
        const auto& original = *static_cast<osg::Vec3Array*>(fixture.source->getVertexArray());
        EXPECT_EQ(vertices[0], original[0]);
        EXPECT_EQ(vertices[1], original[1] * .25f + osg::Vec3f(3, 3, 4));
        EXPECT_EQ(vertices[2], osg::Vec3f(2, 3, 4));
        EXPECT_EQ(posedVertices(*rendered), vertices);
        const auto* indices
            = static_cast<osg::Vec4Array*>(rendered->getVertexAttribArray(SceneUtil::SkinIndicesAttribute));
        EXPECT_EQ((*indices)[0].x(), -1.f);
        osg::Matrixf missing;
        ASSERT_TRUE(rendered->getStateSet()->getUniform("openmwRigPalette")->getElement(1, missing));
        EXPECT_EQ(missing, osg::Matrixf(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
    }

    TEST(GpuSkinning, FallsBackWithoutTruncatingInfluencesOrOverwritingAttributes)
    {
        RigFixture fixture;
        fixture.rig->setInfluences({ { { 0, .2f }, { 0, .2f }, { 0, .2f }, { 0, .2f }, { 0, .2f } }, {}, {} });
        EXPECT_FALSE(fixture.rig->setGpuSkinning(true));
        fixture.rig->setInfluences({ { { 0, 1.f } }, {}, {} });
        auto* custom = new osg::Vec4Array(3);
        fixture.source->setVertexAttribArray(SceneUtil::SkinWeightsAttribute, custom, osg::Array::BIND_PER_VERTEX);
        EXPECT_FALSE(fixture.rig->setGpuSkinning(true));
        EXPECT_EQ(fixture.source->getVertexAttribArray(SceneUtil::SkinWeightsAttribute), custom);
        fixture.source->setVertexAttribArray(SceneUtil::SkinWeightsAttribute, nullptr);
        fixture.rig->setBoneInfo(std::vector<SceneUtil::RigGeometry::BoneInfo>(65));
        EXPECT_FALSE(fixture.rig->setGpuSkinning(true));
    }

    TEST(GpuSkinning, KeepsClonePalettesAndSourceArraysIndependent)
    {
        RigFixture first;
        ASSERT_TRUE(first.rig->setGpuSkinning(true));
        RigFixture second;
        second.geode->removeDrawable(second.rig);
        second.rig = new SceneUtil::RigGeometry(*first.rig, osg::CopyOp::SHALLOW_COPY);
        second.geode->addDrawable(second.rig);
        ASSERT_TRUE(second.rig->getGpuSkinning());
        first.bone->setMatrix(osg::Matrix::translate(1, 0, 0));
        second.bone->setMatrix(osg::Matrix::translate(2, 0, 0));
        auto a = first.frame(1);
        auto b = second.frame(1);
        EXPECT_NE(a->getStateSet()->getUniform("openmwRigPalette"), b->getStateSet()->getUniform("openmwRigPalette"));
        EXPECT_NE(posedVertices(*a), posedVertices(*b));
        EXPECT_EQ(a->getVertexArray(), b->getVertexArray());
        EXPECT_EQ(first.source->getVertexAttribArray(SceneUtil::SkinWeightsAttribute), nullptr);
        EXPECT_EQ(first.source->getVertexAttribArray(SceneUtil::SkinIndicesAttribute), nullptr);
        EXPECT_EQ(first.source->getStateSet(), nullptr);
    }

    TEST(GpuSkinning, LazyCpuGeometryDoesNotMutateSharedSourceUserData)
    {
        RigFixture fixture;
        fixture.source->setUserValue("existing metadata", std::string("preserve"));
        const auto* original = fixture.source->getUserDataContainer();
        const auto count = original->getNumUserObjects();
        ASSERT_TRUE(fixture.rig->setGpuSkinning(true));
        auto rendered = fixture.frame(1);
        EXPECT_NE(rendered->getUserDataContainer(), original);
        posedVertices(*fixture.rig);
        posedVertices(*rendered);
        fixture.rig->setGpuSkinning(false);
        auto cpu = fixture.frame(2);
        EXPECT_NE(cpu->getUserDataContainer(), original);
        EXPECT_EQ(fixture.source->getUserDataContainer(), original);
        EXPECT_EQ(original->getNumUserObjects(), count);
        std::string value;
        EXPECT_TRUE(fixture.source->getUserValue("existing metadata", value));
        EXPECT_EQ(value, "preserve");
    }

    TEST(GpuSkinning, ReactivationDoesNotMutateSubmittedPoseWithSameFrameParity)
    {
        RigFixture fixture;
        ASSERT_TRUE(fixture.rig->setGpuSkinning(true));
        fixture.bone->setMatrix(osg::Matrix::translate(1, 0, 0));
        auto submitted = fixture.frame(1);
        const auto oldPose = posedVertices(*submitted);
        const auto oldPalette = submitted->getStateSet()->getUniform("openmwRigPalette");
        const auto modified = oldPalette->getModifiedCount();
        fixture.skeleton->setActive(SceneUtil::Skeleton::Inactive);
        EXPECT_EQ(fixture.frame(2), submitted);
        std::atomic_bool valid{ true };
        std::thread reader([&] {
            for (unsigned int i = 0; i < 2000; ++i)
                if (posedVertices(*submitted) != oldPose)
                    valid = false;
        });
        fixture.skeleton->setActive(SceneUtil::Skeleton::Active);
        fixture.bone->setMatrix(osg::Matrix::translate(3, 0, 0));
        auto next = fixture.frame(3);
        reader.join();
        EXPECT_TRUE(valid);
        EXPECT_NE(next, submitted);
        EXPECT_EQ(oldPalette->getModifiedCount(), modified);
        EXPECT_EQ(posedVertices(*submitted), oldPose);
        EXPECT_NE(posedVertices(*next), oldPose);
        EXPECT_EQ(fixture.frame(3), next); // second view/pass uses the same published pose
    }

    TEST(GpuSkinning, SourceReplacementAndGpuToggleKeepQueuedGeometryAlive)
    {
        RigFixture fixture;
        ASSERT_TRUE(fixture.rig->setGpuSkinning(true));
        fixture.bone->setMatrix(osg::Matrix::translate(1, 0, 0));
        auto old = fixture.frame(1);
        const auto oldPose = posedVertices(*old);
        osg::ref_ptr<osg::Geometry> replacement = new osg::Geometry(*fixture.source, osg::CopyOp::DEEP_COPY_ARRAYS);
        fixture.rig->setSourceGeometry(replacement);
        EXPECT_FALSE(fixture.rig->getGpuSkinning());
        EXPECT_EQ(posedVertices(*old), oldPose);
        ASSERT_TRUE(fixture.rig->setGpuSkinning(true));
        fixture.skeleton->setActive(SceneUtil::Skeleton::Inactive);
        auto next = fixture.frame(1);
        EXPECT_NE(next, old);
        EXPECT_EQ(posedVertices(*next), oldPose);
        fixture.rig->setGpuSkinning(false);
        EXPECT_EQ(posedVertices(*fixture.frame(1)), oldPose);
    }

    TEST(GpuSkinning, NonAffinePoseUsesCpuWithoutChangingSource)
    {
        RigFixture fixture;
        ASSERT_TRUE(fixture.rig->setGpuSkinning(true));
        osg::Matrix projective;
        projective(0, 3) = .5;
        fixture.bone->setMatrix(projective);
        auto rendered = fixture.frame(1);
        EXPECT_NE(rendered->getVertexArray(), fixture.source->getVertexArray());
        EXPECT_EQ(rendered->getVertexAttribArray(SceneUtil::SkinWeightsAttribute), nullptr);
        EXPECT_EQ(posedVertices(*rendered), posedVertices(*fixture.rig));
    }

    TEST(GpuSkinning, OpaqueShadowOptimizationPreservesPaletteThroughCachedAncestors)
    {
        RigFixture fixture;
        ASSERT_TRUE(fixture.rig->setGpuSkinning(true));
        auto rendered = fixture.frame(1);
        SceneUtil::ShadowsBin::CastingPrograms programs;
        for (auto& program : programs)
            program = new osg::Program;
        osg::ref_ptr<SceneUtil::ShadowsBin> bin = new SceneUtil::ShadowsBin(programs);
        osg::ref_ptr<osgUtil::StateGraph> root = new osgUtil::StateGraph;
        auto* owner = root->find_or_insert(rendered->getStateSet());
        std::unordered_set<osgUtil::StateGraph*> cache;
        for (int i = 0; i < 2; ++i)
        {
            osg::ref_ptr<osg::StateSet> childState = new osg::StateSet;
            auto* child = owner->find_or_insert(childState);
            osg::ref_ptr<osgUtil::RenderLeaf> leaf = new osgUtil::RenderLeaf(rendered, nullptr, nullptr);
            leaf->_parent = child;
            child->_leaves.push_back(leaf);
            auto* preserved = bin->cullStateGraph(child, root, cache, false);
            ASSERT_NE(preserved, nullptr);
            EXPECT_EQ(preserved->_parent, child);
            EXPECT_EQ(leaf->_parent, preserved);
            bool alphaTest = true;
            ASSERT_TRUE(preserved->getStateSet()->getUniform("alphaTestShadows")->get(alphaTest));
            EXPECT_FALSE(alphaTest);
            EXPECT_EQ(preserved->getStateSet()->getUniform("openmwRigEnabled"), nullptr);
            EXPECT_EQ(cache.count(owner), 0u);
        }
        osg::ref_ptr<osg::StateSet> staticState = new osg::StateSet;
        auto* ordinary = root->find_or_insert(staticState);
        EXPECT_EQ(bin->cullStateGraph(ordinary, root, cache, false), nullptr);
    }

    TEST(GpuSkinning, PreparationBenchmarkKeepsGpuVertexArraysImmutable)
    {
        constexpr unsigned int vertexCount = 30000;
        constexpr unsigned int frames = 120;
        auto measure = [](bool gpu) {
            RigFixture fixture;
            static_cast<osg::Vec3Array*>(fixture.source->getVertexArray())
                ->assign(vertexCount, osg::Vec3f(.1f, .2f, .3f));
            static_cast<osg::Vec3Array*>(fixture.source->getNormalArray())->assign(vertexCount, osg::Vec3f(0, 0, 1));
            static_cast<osg::Vec4Array*>(fixture.source->getTexCoordArray(7))
                ->assign(vertexCount, osg::Vec4f(1, 0, 0, -1));
            static_cast<osg::Vec4Array*>(fixture.source->getVertexAttribArray(SceneUtil::FallbackTangentAttribute))
                ->assign(vertexCount, osg::Vec4f(0, 1, 0, 1));
            fixture.rig->setSourceGeometry(fixture.source);
            fixture.rig->setInfluences(
                std::vector<SceneUtil::RigGeometry::BoneWeights>(vertexCount, { { 0, .5f }, { 0, .5f } }));
            EXPECT_EQ(fixture.rig->setGpuSkinning(gpu), gpu);
            for (unsigned int i = 1; i <= 10; ++i)
                fixture.frame(i);
            auto initial = fixture.frame(10);
            const auto modified = initial->getVertexArray()->getModifiedCount();
            const auto start = std::chrono::steady_clock::now();
            osg::ref_ptr<osg::Geometry> rendered;
            for (unsigned int i = 11; i < frames + 11; ++i)
            {
                fixture.bone->setMatrix(osg::Matrix::translate(i * .001, 0, 0));
                rendered = fixture.frame(i);
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            if (gpu)
            {
                EXPECT_EQ(rendered->getVertexArray(), fixture.source->getVertexArray());
                EXPECT_EQ(rendered->getVertexArray()->getModifiedCount(), modified);
            }
            else
                EXPECT_GT(rendered->getVertexArray()->getModifiedCount(), modified);
            return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() / frames;
        };
        // Report CPU preparation only. The test makes no assertion about time or in-game FPS.
        RecordProperty("vertices", vertexCount);
        RecordProperty("frames", frames);
        RecordProperty("cpu_preparation_ns_per_frame", std::to_string(measure(false)));
        RecordProperty("gpu_preparation_ns_per_frame", std::to_string(measure(true)));
    }
}
