#include <components/resource/imagemanager.hpp>
#include <components/sceneutil/optimizer.hpp>
#include <components/sceneutil/texturetype.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/shader/shadervisitor.hpp>
#include <components/testing/util.hpp>

#include <osg/Geometry>
#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/Program>
#include <osg/Texture2D>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    struct ClassicBindingTest : testing::Test
    {
        std::unique_ptr<VFS::Manager> mVfs = TestingOpenMW::createTestVFS(VFS::FileMap{});
        Resource::ImageManager mImages{ mVfs.get(), 0.0 };
        Shader::ShaderManager mShaders;

        ClassicBindingTest()
        {
            const auto path = TestingOpenMW::currentTestDirPath();
            std::filesystem::create_directories(path / "compatibility");
            mShaders.setShaderPath(path);
            mShaders.setMaxTextureUnits(16);
            auto defines = Shader::getDefaultDefines();
            defines["forcePPL"] = "1";
            defines["clamp"] = "0";
            mShaders.setGlobalDefines(defines);
            for (const auto* extension : { ".vert", ".frag" })
            {
                std::ofstream file(path / "compatibility" / (std::string("objects") + extension));
                file << "#version 120\n"
                        "#define ORDINARY_NORMAL @normalMap\n"
                        "#define ORDINARY_NORMAL_UV @normalMapUV\n"
                        "#define AUTOMATIC_NORMAL @autoNormalMap\n"
                        "#define CLASSIC_NORMAL @classicNormalMap\n"
                        "#define CLASSIC_NORMAL_UV @classicNormalMapUV\n"
                        "#define CLASSIC_NORMAL_UNIT @classicNormalMapUnit\n"
                        "#define CLASSIC_HEIGHT @classicParallax\n"
                        "void main() {}\n";
            }
        }

        static osg::ref_ptr<osg::Texture2D> texture()
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            image->data()[0] = 128;
            image->data()[1] = 128;
            image->data()[2] = 255;
            image->data()[3] = 128;
            return new osg::Texture2D(image);
        }

        static void bind(osg::Node& node, unsigned int unit, const char* type, osg::Texture2D* texture)
        {
            auto* state = node.getOrCreateStateSet();
            state->setTextureAttribute(unit, texture, osg::StateAttribute::ON);
            state->setTextureAttribute(unit, new SceneUtil::TextureType(type), osg::StateAttribute::ON);
        }

        static osg::ref_ptr<osg::Geometry> triangle()
        {
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
            vertices->push_back(osg::Vec3(0, 0, 0));
            vertices->push_back(osg::Vec3(1, 0, 0));
            vertices->push_back(osg::Vec3(0, 1, 0));
            geometry->setVertexArray(vertices);
            osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
            normals->assign(3, osg::Vec3(0, 0, 1));
            geometry->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
            osg::ref_ptr<osg::Vec2Array> uv = new osg::Vec2Array;
            uv->push_back(osg::Vec2(0, 0));
            uv->push_back(osg::Vec2(1, 0));
            uv->push_back(osg::Vec2(0, 1));
            geometry->setTexCoordArray(0, uv, osg::Array::BIND_PER_VERTEX);
            geometry->setTexCoordArray(1, uv, osg::Array::BIND_PER_VERTEX);
            osg::ref_ptr<osg::Vec2Array> rotatedUv = new osg::Vec2Array;
            rotatedUv->push_back(osg::Vec2(0, 0));
            rotatedUv->push_back(osg::Vec2(0, 1));
            rotatedUv->push_back(osg::Vec2(-1, 0));
            geometry->setTexCoordArray(2, rotatedUv, osg::Array::BIND_PER_VERTEX);
            geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));
            return geometry;
        }

        void visit(osg::Node& node, bool canModifyGeometry = true)
        {
            Shader::ShaderVisitor visitor(mShaders, mImages, "objects");
            visitor.setAllowedToModifyStateSets(canModifyGeometry);
            node.accept(visitor);
        }

        static const osg::Program* program(const osg::Geometry& geometry)
        {
            const osg::StateSet* state = geometry.getStateSet();
            if (!state)
                throw std::runtime_error("Fixture geometry has no StateSet");
            const auto* result = dynamic_cast<const osg::Program*>(state->getAttribute(osg::StateAttribute::PROGRAM));
            if (!result || result->getNumShaders() == 0)
                throw std::runtime_error("Fixture geometry has no shader program");
            return result;
        }

        static int define(const osg::Geometry& geometry, std::string_view name)
        {
            const std::string source = program(geometry)->getShader(0)->getShaderSource();
            const std::string marker = "#define " + std::string(name) + " ";
            const auto offset = source.find(marker);
            if (offset == std::string::npos)
                throw std::runtime_error("Missing fixture shader define: " + std::string(name));
            return std::stoi(source.substr(offset + marker.size()));
        }

        static const osg::Texture* boundTexture(const osg::Geometry& geometry, unsigned int unit)
        {
            const auto* attribute = geometry.getStateSet()->getTextureAttribute(unit, osg::StateAttribute::TEXTURE);
            return attribute ? attribute->asTexture() : nullptr;
        }
    };

    TEST_F(ClassicBindingTest, equalUvsWithDifferentFallbackSamplerUnitsDoNotAliasPrograms)
    {
        auto explicitTexture = texture();
        auto automaticTexture = texture();
        osg::ref_ptr<osg::Group> shadowedParent = new osg::Group;
        auto shadowed = triangle();
        bind(*shadowedParent, 1, "normalMap", explicitTexture);
        bind(*shadowed, 1, "autoNormalMap", automaticTexture);
        bind(*shadowed, 2, "autoNormalMap", automaticTexture);
        shadowedParent->addChild(shadowed);
        visit(*shadowedParent);

        auto visible = triangle();
        bind(*visible, 1, "normalMap", explicitTexture);
        bind(*visible, 2, "autoNormalMap", automaticTexture);
        visit(*visible);

        for (const auto* geometry : { shadowed.get(), visible.get() })
        {
            EXPECT_EQ(define(*geometry, "ORDINARY_NORMAL_UV"), 2);
            EXPECT_EQ(define(*geometry, "AUTOMATIC_NORMAL"), 1);
            EXPECT_EQ(define(*geometry, "CLASSIC_NORMAL"), 1);
            EXPECT_EQ(define(*geometry, "CLASSIC_NORMAL_UV"), 1);
        }
        const int shadowedUnit = define(*shadowed, "CLASSIC_NORMAL_UNIT");
        const int visibleUnit = define(*visible, "CLASSIC_NORMAL_UNIT");
        ASSERT_GE(shadowedUnit, 0);
        ASSERT_GE(visibleUnit, 0);
        EXPECT_NE(shadowedUnit, visibleUnit);
        EXPECT_EQ(visibleUnit, 1);
        EXPECT_EQ(boundTexture(*shadowed, shadowedUnit), explicitTexture.get());
        EXPECT_EQ(boundTexture(*visible, visibleUnit), explicitTexture.get());
        EXPECT_EQ(boundTexture(*shadowed, 1), automaticTexture.get());
        EXPECT_NE(program(*shadowed), program(*visible));

        // A second pass must retain both layouts in the same program cache.
        visit(*shadowedParent, false);
        visit(*visible, false);
        EXPECT_EQ(define(*shadowed, "CLASSIC_NORMAL_UNIT"), shadowedUnit);
        EXPECT_EQ(define(*visible, "CLASSIC_NORMAL_UNIT"), visibleUnit);
        EXPECT_EQ(boundTexture(*shadowed, shadowedUnit), explicitTexture.get());
    }

    TEST_F(ClassicBindingTest, inheritedExplicitFallbackSurvivesTemplateOptimizationAndRecreation)
    {
        auto explicitTexture = texture();
        auto automaticTexture = texture();
        osg::ref_ptr<osg::Group> root = new osg::Group;
        osg::ref_ptr<osg::MatrixTransform> parent = new osg::MatrixTransform;
        osg::ref_ptr<osg::MatrixTransform> child = new osg::MatrixTransform;
        parent->setMatrix(osg::Matrix::rotate(0.7, osg::Vec3(0, 0, 1)));
        child->setMatrix(osg::Matrix::translate(1, 2, 3));
        parent->setDataVariance(osg::Object::STATIC);
        child->setDataVariance(osg::Object::STATIC);
        bind(*parent, 1, "normalHeightMap", explicitTexture);
        bind(*child, 1, "autoNormalMap", automaticTexture);
        auto geometry = triangle();
        bind(*geometry, 0, "diffuseMap", texture());
        child->addChild(geometry);
        parent->addChild(child);
        root->addChild(parent);
        visit(*root);

        ASSERT_EQ(define(*geometry, "CLASSIC_NORMAL"), 1);
        EXPECT_EQ(define(*geometry, "CLASSIC_HEIGHT"), 1);
        const int firstUnit = define(*geometry, "CLASSIC_NORMAL_UNIT");
        ASSERT_GE(firstUnit, 0);
        EXPECT_EQ(boundTexture(*geometry, firstUnit), explicitTexture.get());

        SceneUtil::Optimizer optimizer;
        optimizer.optimize(root, SceneUtil::Optimizer::FLATTEN_STATIC_TRANSFORMS
                | SceneUtil::Optimizer::REMOVE_REDUNDANT_NODES | SceneUtil::Optimizer::MERGE_GEOMETRY);
        visit(*root, false);

        EXPECT_EQ(define(*geometry, "ORDINARY_NORMAL_UV"), 1);
        EXPECT_EQ(define(*geometry, "AUTOMATIC_NORMAL"), 1);
        EXPECT_EQ(define(*geometry, "CLASSIC_NORMAL"), 1);
        EXPECT_EQ(define(*geometry, "CLASSIC_NORMAL_UV"), 1);
        EXPECT_EQ(define(*geometry, "CLASSIC_HEIGHT"), 1);
        const int restoredUnit = define(*geometry, "CLASSIC_NORMAL_UNIT");
        ASSERT_GE(restoredUnit, 0);
        EXPECT_EQ(boundTexture(*geometry, restoredUnit), explicitTexture.get());
    }

    TEST_F(ClassicBindingTest, deepClonedResolvedMaterialRetainsItsExplicitAlternativeAfterRecreation)
    {
        auto explicitTexture = texture();
        auto automaticTexture = texture();
        osg::ref_ptr<osg::Group> parent = new osg::Group;
        bind(*parent, 1, "normalHeightMap", explicitTexture);
        auto geometry = triangle();
        bind(*geometry, 0, "diffuseMap", texture());
        bind(*geometry, 1, "autoNormalMap", automaticTexture);
        parent->addChild(geometry);
        visit(*parent);

        // All ordinary bindings and the compiled explicit alternative are already
        // on this leaf. Preserve deep-copied user data while detaching the leaf
        // from the hierarchy that originally supplied the explicit texture.
        osg::ref_ptr<osg::Geometry> clone = new osg::Geometry(*geometry,
            osg::CopyOp::DEEP_COPY_STATESETS | osg::CopyOp::DEEP_COPY_USERDATA);
        visit(*clone, false);
        ASSERT_EQ(define(*clone, "CLASSIC_NORMAL"), 1);
        EXPECT_EQ(define(*clone, "CLASSIC_HEIGHT"), 1);
        EXPECT_EQ(define(*clone, "CLASSIC_NORMAL_UV"), 1);
        const int samplerUnit = define(*clone, "CLASSIC_NORMAL_UNIT");
        ASSERT_GE(samplerUnit, 0);
        EXPECT_EQ(boundTexture(*clone, samplerUnit), explicitTexture.get());
        EXPECT_EQ(boundTexture(*clone, 1), automaticTexture.get());

        visit(*clone, false);
        EXPECT_EQ(define(*clone, "CLASSIC_NORMAL_UNIT"), samplerUnit);
        EXPECT_EQ(boundTexture(*clone, samplerUnit), explicitTexture.get());
        EXPECT_EQ(boundTexture(*geometry, 1), automaticTexture.get());
    }

    TEST_F(ClassicBindingTest, cachedFallbackDoesNotOverrideCurrentExplicitAncestor)
    {
        auto original = texture();
        auto replacement = texture();
        auto automaticTexture = texture();
        osg::ref_ptr<osg::Group> parent = new osg::Group;
        bind(*parent, 1, "normalMap", original);
        auto geometry = triangle();
        bind(*geometry, 0, "diffuseMap", texture());
        bind(*geometry, 1, "autoNormalMap", automaticTexture);
        parent->addChild(geometry);
        visit(*parent);
        const int originalUnit = define(*geometry, "CLASSIC_NORMAL_UNIT");
        ASSERT_GE(originalUnit, 0);
        EXPECT_EQ(boundTexture(*geometry, originalUnit), original.get());

        // Reparent an already compiled leaf under a different authored material.
        // Its cached alternative describes history, while this new ancestor is
        // the current source of the explicit binding.
        parent->removeChild(geometry);
        osg::ref_ptr<osg::Group> newParent = new osg::Group;
        bind(*newParent, 1, "normalMap", replacement);
        newParent->addChild(geometry);
        visit(*newParent, false);
        EXPECT_EQ(define(*geometry, "CLASSIC_NORMAL"), 1);
        const int replacementUnit = define(*geometry, "CLASSIC_NORMAL_UNIT");
        ASSERT_GE(replacementUnit, 0);
        EXPECT_EQ(boundTexture(*geometry, replacementUnit), replacement.get());
        EXPECT_EQ(boundTexture(*geometry, 1), automaticTexture.get());

        visit(*newParent, false);
        EXPECT_EQ(boundTexture(*geometry, define(*geometry, "CLASSIC_NORMAL_UNIT")), replacement.get());
    }
}
