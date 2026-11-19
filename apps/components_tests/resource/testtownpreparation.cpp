#include <components/resource/imagemanager.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/shader/shadervisitor.hpp>
#include <components/terrain/compositemaprenderer.hpp>
#include <components/testing/util.hpp>

#include <osg/Geometry>
#include <osg/Group>
#include <osg/Program>
#include <osg/ValueObject>

#include <fstream>

namespace
{
    TEST(TownPreparationTest, TextureCompanionsRespectPatternsAndVfsRebuilds)
    {
        TestingOpenMW::VFSTestFile image("fixture");
        constexpr VFS::Path::NormalizedView normal("textures/x_n.dds"), specular("textures/x_spec.dds");
        auto vfs = TestingOpenMW::createTestVFS({ {normal, &image} });
        Resource::ImageManager manager(vfs.get(), 1);
        EXPECT_EQ(manager.findCompanion("Textures/X.DDS", "_n"), VFS::Path::Normalized(normal));
        EXPECT_FALSE(manager.findCompanion("Textures/X.DDS", "_spec"));
        vfs->reset();
        vfs->addArchive(std::make_unique<TestingOpenMW::VFSTestData>(
            VFS::FileMap{ { VFS::Path::Normalized(specular), &image } }));
        vfs->buildIndex();
        EXPECT_FALSE(manager.findCompanion("Textures/X.DDS", "_n"));
        EXPECT_EQ(manager.findCompanion("Textures/X.DDS", "_spec"), VFS::Path::Normalized(specular));
        EXPECT_FALSE(manager.findCompanion("Textures/X.DDS", "_nh"));
    }

    TEST(TownPreparationTest, SharedShaderStateCanBeOverriddenAndRestoredByASibling)
    {
        const auto dir = TestingOpenMW::currentTestDirPath();
        std::filesystem::create_directories(dir / "compatibility");
        for (std::string prefix : { "parent", "branch" })
            for (std::string stage : { ".vert", ".frag" })
                std::ofstream(dir / "compatibility" / (prefix + stage))
                    << "#version 120\n// " << prefix << "_tag\nvoid main(){}\n";
        VFS::Manager vfs;
        Resource::ImageManager images(&vfs, 1);
        Shader::ShaderManager manager;
        manager.setShaderPath(dir);
        for (bool aliased : { true, false })
        {
            osg::ref_ptr<osg::Group> owner = new osg::Group, branch = new osg::Group;
            osg::StateSet* shared = owner->getOrCreateStateSet();
            branch->setStateSet(aliased ? shared : new osg::StateSet);
            owner->setUserValue("shaderPrefix", std::string("parent"));
            branch->setUserValue("shaderPrefix", std::string("branch"));
            owner->addChild(new osg::Geometry);
            branch->addChild(new osg::Geometry);
            owner->addChild(branch);
            owner->addChild(new osg::Geometry);
            Shader::ShaderVisitor visitor(manager, images, "parent");
            owner->accept(visitor);
            auto program = dynamic_cast<const osg::Program*>(owner->getStateSet()->getAttribute(osg::StateAttribute::PROGRAM));
            ASSERT_NE(program, nullptr);
            EXPECT_NE(program->getShader(0)->getShaderSource().find("parent_tag"), std::string::npos);
            auto nested = dynamic_cast<const osg::Program*>(branch->getStateSet()->getAttribute(osg::StateAttribute::PROGRAM));
            ASSERT_NE(nested, nullptr);
            EXPECT_NE(nested->getShader(0)->getShaderSource().find(aliased ? "parent_tag" : "branch_tag"), std::string::npos);
        }
    }

    TEST(TownPreparationTest, BackgroundLayersResumeAndPromotionFinishesInDrawOrder)
    {
        Terrain::CompositeMap map;
        std::vector<osg::ref_ptr<osg::Drawable>> original;
        for (int i = 0; i < 5; ++i)
            original.emplace_back(new osg::Geometry);
        map.mDrawables = original;
        std::vector<osg::Drawable*> drawn;
        auto draw = [&](osg::Drawable& layer) { drawn.push_back(&layer); };
        map.drawPendingLayers(draw, [] { return true; });
        ASSERT_EQ(map.mCompiled, 1u);
        ASSERT_EQ(map.mDrawables[0], nullptr);
        map.drawPendingLayers([&](osg::Drawable& layer) {
            draw(layer);
            map.mImmediate = true;
        }, [] { return true; });
        EXPECT_TRUE(map.mDrawables.empty());
        ASSERT_EQ(drawn.size(), original.size());
        for (std::size_t i = 0; i < original.size(); ++i)
            EXPECT_EQ(drawn[i], original[i].get());
    }
}
