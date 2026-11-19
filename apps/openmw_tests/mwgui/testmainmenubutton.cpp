#include "apps/openmw/mwgui/mainmenutextbutton.hpp"

#include <MyGUI_CommonStateInfo.h>
#include <MyGUI_DataFileStream.h>
#include <MyGUI_DataManager.h>
#include <MyGUI_FactoryManager.h>
#include <MyGUI_FontManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_LayerManager.h>
#include <MyGUI_LogManager.h>
#include <MyGUI_OverlappedLayer.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ResourceManager.h>
#include <MyGUI_ResourceSkin.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MWGui
{
    namespace
    {
        // Substitute only platform services. These tests drive the production
        // button and skin through real MyGUI input, resource and widget managers.
        class MenuDataManager final : public MyGUI::DataManager
        {
        public:
            MyGUI::IDataStream* getData(const std::string& name) const override
            {
                auto stream = std::make_unique<std::ifstream>(mRoot / name, std::ios::binary);
                return *stream ? new MyGUI::DataFileStream(std::move(stream)) : nullptr;
            }
            void freeData(MyGUI::IDataStream* data) override { delete data; }
            bool isDataExist(const std::string& name) const override { return std::filesystem::exists(mRoot / name); }
            const MyGUI::VectorString& getDataListNames(const std::string&) const override { return mNames; }
            std::string getDataPath(const std::string& name) const override { return (mRoot / name).string(); }

        private:
            const std::filesystem::path mRoot
                = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files/data/mygui";
            const MyGUI::VectorString mNames;
        };

        class MenuTexture final : public MyGUI::ITexture
        {
        public:
            explicit MenuTexture(std::string name)
                : mName(std::move(name))
            {
            }
            const std::string& getName() const override { return mName; }
            void createManual(int width, int height, MyGUI::TextureUsage, MyGUI::PixelFormat) override
            {
                mSize = { width, height };
            }
            void loadFromFile(const std::string&) override {}
            void saveToFile(const std::string&) override {}
            void setShader(const std::string&) override {}
            void destroy() override {}
            void* lock(MyGUI::TextureUsage) override { return nullptr; }
            void unlock() override {}
            bool isLocked() const override { return false; }
            int getWidth() const override { return mSize.width; }
            int getHeight() const override { return mSize.height; }
            MyGUI::PixelFormat getFormat() const override { return MyGUI::PixelFormat::R8G8B8A8; }
            MyGUI::TextureUsage getUsage() const override { return MyGUI::TextureUsage::Static; }
            size_t getNumElemBytes() const override { return 4; }

        private:
            std::string mName;
            MyGUI::IntSize mSize{ 16, 16 };
        };

        class MenuVertexBuffer final : public MyGUI::IVertexBuffer
        {
        public:
            void setVertexCount(size_t count) override { mVertices.resize(count); }
            size_t getVertexCount() const override { return mVertices.size(); }
            MyGUI::Vertex* lock() override { return mVertices.data(); }
            void unlock() override {}

        private:
            std::vector<MyGUI::Vertex> mVertices;
        };

        class MenuRenderManager final : public MyGUI::RenderManager
        {
        public:
            MyGUI::IVertexBuffer* createVertexBuffer() override { return new MenuVertexBuffer; }
            void destroyVertexBuffer(MyGUI::IVertexBuffer* buffer) override { delete buffer; }
            MyGUI::ITexture* createTexture(const std::string& name) override
            {
                auto& texture = mTextures[name];
                if (!texture)
                    texture = std::make_unique<MenuTexture>(name);
                return texture.get();
            }
            void destroyTexture(MyGUI::ITexture* texture) override { mTextures.erase(texture->getName()); }
            MyGUI::ITexture* getTexture(const std::string& name) override { return createTexture(name); }
            const MyGUI::IntSize& getViewSize() const override { return mSize; }
            MyGUI::VertexColourType getVertexFormat() const override { return MyGUI::VertexColourType::ColourABGR; }
            void setViewSize(int width, int height) override
            {
                mSize = { width, height };
                onResizeView(mSize);
            }
            void registerShader(const std::string&, const std::string&, const std::string&) override {}

        private:
            MyGUI::IntSize mSize{ 1280, 1024 };
            std::map<std::string, std::unique_ptr<MenuTexture>> mTextures;
        };

        class MenuFont final : public MyGUI::IFont
        {
        public:
            explicit MenuFont(MyGUI::ITexture* texture)
                : mTexture(texture)
            {
                setResourceName("Default");
            }
            const MyGUI::GlyphInfo* getGlyphInfo(MyGUI::Char codePoint) const override
            {
                return codePoint == ' ' ? &mSpace : &mLetter;
            }
            MyGUI::ITexture* getTextureFont() const override { return mTexture; }
            int getDefaultHeight() const override { return 16; }

        private:
            MyGUI::ITexture* mTexture;
            MyGUI::GlyphInfo mLetter{ 0, 8.f, 16.f, 8.f };
            MyGUI::GlyphInfo mSpace{ ' ', 4.f, 16.f, 4.f };
        };

        class MainMenuTextButtonTest : public testing::Test
        {
        protected:
            MyGUI::LogManager mLog;
            MenuDataManager mData;
            MenuRenderManager mRenderer;
            MyGUI::Gui mGui;
            bool mInitialised = false;
            MainMenuTextButton* mButton = nullptr;
            std::map<std::string, MyGUI::Colour> mColours;
            int mClicks = 0;

            void SetUp() override
            {
                mGui.initialise("");
                mInitialised = true;
                auto& resources = MyGUI::ResourceManager::getInstance();
                resources.addResource(new MenuFont(mRenderer.getTexture("MenuTestFont")));
                MyGUI::FontManager::getInstance().setDefaultFont("Default");
                MyGUI::LanguageManager::getInstance().eventRequestTag
                    = MyGUI::newDelegate(this, &MainMenuTextButtonTest::requestTag);
                MyGUI::FactoryManager::getInstance().registerFactory<MainMenuTextButton>("Widget");
                // createLayerAt does not load XML's Pick property. Enable the
                // same hit testing as the production MainMenu layer explicitly.
                MyGUI::LayerManager::getInstance()
                    .createLayerAt("MainMenuTest", "OverlappedLayer", 0)
                    ->castType<MyGUI::OverlappedLayer>()
                    ->setPick(true);
                ASSERT_TRUE(resources.load("openmw_mainmenu.skin.xml"));

                const auto* skin = resources.getByName("MW_MainMenuTextButton")->castType<MyGUI::ResourceSkin>();
                const auto& states = skin->getStateInfo();
                for (const auto* name : { "normal", "focused", "highlighted", "pushed", "disabled" })
                {
                    const auto found = states.find(name);
                    ASSERT_NE(found, states.end()) << name;
                    ASSERT_FALSE(found->second.empty()) << name;
                    ASSERT_NE(found->second.front(), nullptr) << name;
                    const auto* text = found->second.front()->castType<MyGUI::EditTextStateInfo>();
                    mColours.emplace(name, text->getColour());
                }

                mButton = mGui.createWidget<MainMenuTextButton>("MW_MainMenuTextButton",
                    { 40, 40, 256, 48 }, MyGUI::Align::Default, "MainMenuTest");
                mButton->setCaptionWithReplacing("#{Interface:Continue}");
                mButton->setFontHeight(32);
                mButton->setTextShadow(true);
                mButton->setTextShadowColour(MyGUI::Colour(0, 0, 0, 1));
                mButton->eventMouseButtonClick += MyGUI::newDelegate(this, &MainMenuTextButtonTest::onClick);
                MyGUI::InputManager::getInstance().injectMouseMove(600, 450, 0);
            }

            void TearDown() override
            {
                if (mInitialised)
                    mGui.shutdown();
            }

            void requestTag(const MyGUI::UString& tag, MyGUI::UString& result)
            {
                result = tag == "Interface:Continue" ? "Fortsetzen" : "0.45 0.40 0.30";
            }

            void onClick(MyGUI::Widget* sender)
            {
                EXPECT_EQ(sender, mButton);
                ++mClicks;
            }

            void expectState(const std::string& name) const
            {
                ASSERT_NE(mButton->getSubWidgetText(), nullptr);
                EXPECT_EQ(mButton->getTextColour(), mColours.at(name)) << name;
            }
        };

        TEST_F(MainMenuTextButtonTest, MouseHoverChangesColourWhileKeyboardFocusRemains)
        {
            EXPECT_TRUE(mButton->getNeedKeyFocus());
            EXPECT_TRUE(mButton->getNeedMouseFocus());
            EXPECT_EQ(mButton->getCaption().asUTF8(), "Fortsetzen");
            EXPECT_NE(mButton->getTypeName().find("Button"), std::string::npos);
            EXPECT_NE(mColours.at("normal"), mColours.at("focused"));
            EXPECT_NE(mColours.at("focused"), mColours.at("highlighted"));
            EXPECT_NE(mColours.at("normal"), mColours.at("highlighted"));
            expectState("normal");

            auto& input = MyGUI::InputManager::getInstance();
            input.setKeyFocusWidget(mButton);
            expectState("focused");
            input.injectMouseMove(60, 60, 0);
            ASSERT_EQ(input.getMouseFocusWidget(), mButton);
            EXPECT_EQ(input.getKeyFocusWidget(), mButton);
            expectState("highlighted");
            input.injectMouseMove(600, 450, 0);
            EXPECT_EQ(input.getKeyFocusWidget(), mButton);
            expectState("focused");
            input.resetKeyFocusWidget();
            expectState("normal");
        }

        TEST_F(MainMenuTextButtonTest, LosingKeyboardFocusKeepsMouseHighlight)
        {
            auto& input = MyGUI::InputManager::getInstance();
            input.setKeyFocusWidget(mButton);
            input.injectMouseMove(60, 60, 0);
            input.resetKeyFocusWidget();
            expectState("highlighted");
            input.injectMouseMove(600, 450, 0);
            expectState("normal");
        }

        TEST_F(MainMenuTextButtonTest, LeftPressReleaseUsesPressedColourAndClicksOnce)
        {
            auto& input = MyGUI::InputManager::getInstance();
            input.injectMouseMove(60, 60, 0);
            expectState("highlighted");
            EXPECT_NE(mColours.at("pushed"), mColours.at("highlighted"));
            input.injectMousePress(60, 60, MyGUI::MouseButton::Left);
            expectState("pushed");
            EXPECT_EQ(mClicks, 0);
            input.injectMouseRelease(60, 60, MyGUI::MouseButton::Left);
            expectState("highlighted");
            EXPECT_EQ(mClicks, 1);
            input.injectMouseMove(600, 450, 0);
            expectState("focused");
        }

        TEST_F(MainMenuTextButtonTest, RightClickDoesNotUsePressedColourOrActivate)
        {
            auto& input = MyGUI::InputManager::getInstance();
            input.injectMouseMove(60, 60, 0);
            input.injectMousePress(60, 60, MyGUI::MouseButton::Right);
            expectState("highlighted");
            input.injectMouseRelease(60, 60, MyGUI::MouseButton::Right);
            expectState("highlighted");
            EXPECT_EQ(mClicks, 0);
        }

        TEST_F(MainMenuTextButtonTest, DisableDuringPressClearsTransientStateBeforeReenable)
        {
            auto& input = MyGUI::InputManager::getInstance();
            input.injectMousePress(60, 60, MyGUI::MouseButton::Left);
            expectState("pushed");
            mButton->setEnabled(false);
            expectState("disabled");
            input.injectMouseRelease(600, 450, MyGUI::MouseButton::Left);
            input.injectMouseMove(600, 450, 0);
            EXPECT_EQ(mClicks, 0);
            mButton->setEnabled(true);
            expectState("normal");
            input.setKeyFocusWidget(mButton);
            expectState("focused");
            input.injectMouseMove(60, 60, 0);
            expectState("highlighted");
        }
    }
}
