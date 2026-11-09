#include <components/resource/imagemanager.hpp>
#include <components/sceneutil/texturetype.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/shader/shadervisitor.hpp>
#include <components/testing/util.hpp>

#include <osg/Geometry>
#include <osg/Group>
#include <osg/Texture2D>

#include <fstream>

#include <gtest/gtest.h>

namespace
{
    using namespace testing;

    std::string onePixelTga()
    {
        const unsigned char data[] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 24, 0, 255, 128, 128 };
        std::string result(reinterpret_cast<const char*>(data), sizeof(data));
        // A complete TGA 2 footer also lets readers inspect the signature on this tiny image.
        result.append(8, '\0');
        result.append("TRUEVISION-XFILE.", 18);
        return result;
    }

    struct ShaderVisitorTest : Test
    {
        TestingOpenMW::VFSTestFile mNormalFile{ onePixelTga() };
        TestingOpenMW::VFSTestFile mSpecularFile{ onePixelTga() };
        std::unique_ptr<VFS::Manager> mVfs = TestingOpenMW::createTestVFS(
            { { VFS::Path::NormalizedView("textures/test_n.tga"), &mNormalFile },
                { VFS::Path::NormalizedView("textures/test_s.tga"), &mSpecularFile } });
        Resource::ImageManager mImages{ mVfs.get(), 0.0 };
        Shader::ShaderManager mShaders;

        ShaderVisitorTest()
        {
            const auto path = TestingOpenMW::currentTestDirPath();
            mShaders.setShaderPath(path);
            mShaders.setMaxTextureUnits(16);
            std::filesystem::create_directories(path / "compatibility");
            auto defines = Shader::getDefaultDefines();
            mShaders.setGlobalDefines(defines);
            for (const auto* extension : { ".vert", ".frag" })
            {
                std::ofstream file(path / "compatibility" / (std::string("probe") + extension));
                file << "#version 120\n"
                        "#define NORMAL @normalMap\n"
                        "#define AUTO_NORMAL @autoNormalMap\n"
                        "#define SPECULAR @specularMap\n"
                        "#define AUTO_SPECULAR @autoSpecularMap\n"
                        "#define PARALLAX @parallax\n"
                        "#define CLASSIC_NORMAL @classicNormalMap\n"
                        "#define CLASSIC_NORMAL_UV @classicNormalMapUV\n"
                        "#define CLASSIC_NORMAL_UNIT @classicNormalMapUnit\n"
                        "#define CLASSIC_SPECULAR @classicSpecularMap\n"
                        "#define CLASSIC_SPECULAR_UV @classicSpecularMapUV\n"
                        "#define CLASSIC_PARALLAX @classicParallax\n"
                        "#define CLASSIC_AVAILABLE @classicInteriorAvailable\n"
                        "void main() {}\n";
            }
        }

        static osg::ref_ptr<osg::Texture2D> texture()
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(1, 1, 1, GL_RGB, GL_UNSIGNED_BYTE);
            image->setFileName("textures/test.tga");
            return new osg::Texture2D(image);
        }

        static void bind(osg::Node& node, unsigned int unit, const char* type, osg::Texture2D* texture)
        {
            auto* state = node.getOrCreateStateSet();
            state->setTextureAttribute(unit, texture, osg::StateAttribute::ON);
            state->setTextureAttribute(unit, new SceneUtil::TextureType(type), osg::StateAttribute::ON);
        }

        void visit(osg::Node& node)
        {
            Shader::ShaderVisitor visitor(mShaders, mImages, "probe");
            visitor.setAutoUseNormalMaps(true);
            visitor.setNormalMapPattern("_n");
            visitor.setNormalHeightMapPattern("_nh");
            visitor.setAutoUseSpecularMaps(true);
            visitor.setSpecularMapPattern("_s");
            node.accept(visitor);
        }

        static std::string source(const osg::Geometry& geometry)
        {
            const auto* program
                = static_cast<const osg::Program*>(geometry.getStateSet()->getAttribute(osg::StateAttribute::PROGRAM));
            return program->getShader(0)->getShaderSource();
        }
    };

    TEST_F(ShaderVisitorTest, discovered_maps_keep_provenance_after_revisit_and_clone)
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        bind(*geometry, 0, "diffuseMap", texture());
        visit(*geometry);
        for (unsigned int unit : { 1u, 2u })
        {
            const auto* map = dynamic_cast<const osg::Texture2D*>(
                geometry->getStateSet()->getTextureAttribute(unit, osg::StateAttribute::TEXTURE));
            ASSERT_NE(map, nullptr);
            ASSERT_NE(map->getImage(), nullptr);
            EXPECT_EQ(map->getImage()->s(), 1);
            EXPECT_EQ(map->getImage()->t(), 1);
        }
        EXPECT_NE(source(*geometry).find("#define AUTO_NORMAL 1"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define AUTO_SPECULAR 1"), std::string::npos);

        visit(*geometry);
        EXPECT_NE(source(*geometry).find("#define AUTO_NORMAL 1"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define AUTO_SPECULAR 1"), std::string::npos);

        osg::ref_ptr<osg::Geometry> clone = new osg::Geometry(*geometry, osg::CopyOp::SHALLOW_COPY);
        visit(*clone);
        EXPECT_NE(source(*clone).find("#define AUTO_NORMAL 1"), std::string::npos);
        EXPECT_NE(source(*clone).find("#define AUTO_SPECULAR 1"), std::string::npos);
    }

    TEST_F(ShaderVisitorTest, same_texture_has_independent_explicit_and_automatic_bindings)
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        auto sharedTexture = texture();
        bind(*geometry, 1, "normalMap", sharedTexture);
        bind(*geometry, 2, "autoSpecularMap", sharedTexture);
        visit(*geometry);
        EXPECT_NE(source(*geometry).find("#define NORMAL 1"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define AUTO_NORMAL 0"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define SPECULAR 1"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define AUTO_SPECULAR 1"), std::string::npos);
    }

    TEST_F(ShaderVisitorTest, explicit_child_binding_replaces_inherited_automatic_provenance)
    {
        osg::ref_ptr<osg::Group> group = new osg::Group;
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        auto sharedTexture = texture();
        bind(*group, 1, "autoNormalHeightMap", sharedTexture);
        bind(*geometry, 1, "normalMap", sharedTexture);
        group->addChild(geometry);
        visit(*group);
        EXPECT_NE(source(*geometry).find("#define AUTO_NORMAL 0"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define PARALLAX 1"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define CLASSIC_PARALLAX 0"), std::string::npos);
    }

    TEST_F(ShaderVisitorTest, last_normal_binding_supplies_its_own_provenance)
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        auto sharedTexture = texture();
        bind(*geometry, 1, "autoNormalMap", sharedTexture);
        bind(*geometry, 2, "normalHeightMap", sharedTexture);
        visit(*geometry);
        EXPECT_NE(source(*geometry).find("#define AUTO_NORMAL 0"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define PARALLAX 1"), std::string::npos);
    }

    TEST_F(ShaderVisitorTest, explicit_maps_are_not_reclassified_when_automatic_files_exist)
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        bind(*geometry, 0, "diffuseMap", texture());
        bind(*geometry, 1, "normalHeightMap", texture());
        bind(*geometry, 2, "specularMap", texture());
        visit(*geometry);
        EXPECT_NE(source(*geometry).find("#define AUTO_NORMAL 0"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define AUTO_SPECULAR 0"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define PARALLAX 1"), std::string::npos);
    }

    TEST_F(ShaderVisitorTest, automatic_higher_units_do_not_hide_explicit_normal_or_specular)
    {
        osg::ref_ptr<osg::Group> group = new osg::Group;
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        bind(*group, 4, "autoNormalHeightMap", texture());
        bind(*group, 5, "autoSpecularMap", texture());
        bind(*geometry, 1, "normalMap", texture());
        bind(*geometry, 2, "specularMap", texture());
        group->addChild(geometry);
        visit(*group);
        EXPECT_NE(source(*geometry).find("#define AUTO_NORMAL 1"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define AUTO_SPECULAR 1"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define CLASSIC_NORMAL_UV 1"), std::string::npos);
        EXPECT_NE(source(*geometry).find("#define CLASSIC_SPECULAR_UV 2"), std::string::npos);
    }

    TEST_F(ShaderVisitorTest, physically_shadowed_binding_survives_deep_userdata_copy)
    {
        osg::ref_ptr<osg::Group> group = new osg::Group;
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        bind(*group, 1, "normalHeightMap", texture());
        bind(*geometry, 1, "autoNormalMap", texture());
        group->addChild(geometry);
        visit(*group);
        EXPECT_NE(source(*geometry).find("#define CLASSIC_NORMAL_UNIT 0"), std::string::npos);

        osg::ref_ptr<osg::Geometry> clone = new osg::Geometry(*geometry,
            osg::CopyOp::DEEP_COPY_STATESETS | osg::CopyOp::DEEP_COPY_USERDATA);
        visit(*clone); // No parent remains: the fallback carries its explicit source metadata.
        EXPECT_NE(source(*clone).find("#define CLASSIC_NORMAL 1"), std::string::npos);
        EXPECT_NE(source(*clone).find("#define CLASSIC_NORMAL_UV 1"), std::string::npos);
        EXPECT_NE(source(*clone).find("#define CLASSIC_PARALLAX 1"), std::string::npos);
        EXPECT_NE(source(*clone).find("#define CLASSIC_NORMAL_UNIT 0"), std::string::npos);
    }

    TEST_F(ShaderVisitorTest, authored_edit_removes_stale_fallback_across_repeated_visits)
    {
        osg::ref_ptr<osg::Group> group = new osg::Group;
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        bind(*group, 1, "normalMap", texture());
        bind(*geometry, 1, "autoNormalMap", texture());
        group->addChild(geometry);
        visit(*group);
        ASSERT_NE(geometry->getStateSet()->getTextureAttribute(0, osg::StateAttribute::TEXTURE), nullptr);

        bind(*geometry, 1, "normalMap", texture());
        visit(*group);
        EXPECT_EQ(geometry->getStateSet()->getTextureAttribute(0, osg::StateAttribute::TEXTURE), nullptr);
        visit(*group);
        EXPECT_EQ(geometry->getStateSet()->getTextureAttribute(0, osg::StateAttribute::TEXTURE), nullptr);
        EXPECT_NE(source(*geometry).find("#define CLASSIC_NORMAL_UNIT 1"), std::string::npos);
    }

    TEST_F(ShaderVisitorTest, fallback_uses_low_free_unit_below_reserved_slots)
    {
        mShaders.setMaxTextureUnits(8);
        mShaders.reserveGlobalTextureUnits(Shader::ShaderManager::Slot::ShadowMaps, 2);
        osg::ref_ptr<osg::Group> group = new osg::Group;
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        bind(*group, 1, "normalMap", texture());
        bind(*geometry, 1, "autoNormalMap", texture());
        bind(*geometry, 5, "autoSpecularMap", texture());
        group->addChild(geometry);
        visit(*group);
        EXPECT_NE(source(*geometry).find("#define CLASSIC_NORMAL_UNIT 0"), std::string::npos);
        EXPECT_EQ(geometry->getStateSet()->getTextureAttribute(6, osg::StateAttribute::TEXTURE), nullptr);
    }

    TEST_F(ShaderVisitorTest, exhausted_units_preserve_ordinary_material_without_overwriting_reserved_slot)
    {
        mShaders.setMaxTextureUnits(4);
        mShaders.reserveGlobalTextureUnits(Shader::ShaderManager::Slot::ShadowMaps, 1);
        osg::ref_ptr<osg::Group> group = new osg::Group;
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        bind(*group, 1, "normalMap", texture());
        bind(*geometry, 0, "diffuseMap", texture());
        bind(*geometry, 1, "autoNormalMap", texture());
        bind(*geometry, 2, "autoSpecularMap", texture());
        group->addChild(geometry);
        visit(*group);
        EXPECT_NE(source(*geometry).find("#define CLASSIC_AVAILABLE 0"), std::string::npos);
        EXPECT_EQ(geometry->getStateSet()->getUniform("classicInteriorLighting"), nullptr);
        EXPECT_EQ(geometry->getStateSet()->getTextureAttribute(3, osg::StateAttribute::TEXTURE), nullptr);
    }
}
