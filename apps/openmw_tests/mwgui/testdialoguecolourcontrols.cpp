#include "apps/openmw/mwgui/dialoguecolourcontrols.hpp"
#include "apps/openmw/mwgui/resourceskin.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_DataFileStream.h>
#include <MyGUI_DataManager.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_EditText.h>
#include <MyGUI_FactoryManager.h>
#include <MyGUI_FontManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_LayerManager.h>
#include <MyGUI_LogManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ResourceManager.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_ScrollView.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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
        // Only the platform's file/render/font services are substitutes. The
        // actual MyGUI managers, widget factories, OpenMW skins, text wrapping,
        // child alignment and scroll canvas all run in this fixture.
        class ControlsDataManager final : public MyGUI::DataManager
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

        class ControlsTexture final : public MyGUI::ITexture
        {
        public:
            explicit ControlsTexture(std::string name)
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

        class ControlsVertexBuffer final : public MyGUI::IVertexBuffer
        {
        public:
            void setVertexCount(size_t count) override { mVertices.resize(count); }
            size_t getVertexCount() const override { return mVertices.size(); }
            MyGUI::Vertex* lock() override { return mVertices.data(); }
            void unlock() override {}

        private:
            std::vector<MyGUI::Vertex> mVertices;
        };

        class ControlsRenderManager final : public MyGUI::RenderManager
        {
        public:
            MyGUI::IVertexBuffer* createVertexBuffer() override { return new ControlsVertexBuffer; }
            void destroyVertexBuffer(MyGUI::IVertexBuffer* buffer) override { delete buffer; }
            MyGUI::ITexture* createTexture(const std::string& name) override
            {
                auto& texture = mTextures[name];
                if (!texture)
                    texture = std::make_unique<ControlsTexture>(name);
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
            std::map<std::string, std::unique_ptr<ControlsTexture>> mTextures;
        };

        class ControlsFont final : public MyGUI::IFont
        {
        public:
            explicit ControlsFont(MyGUI::ITexture* texture)
                : mTexture(texture)
            {
                setResourceName("DialogueControlsTest");
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

        class DialogueColourControlsTest : public testing::Test
        {
        protected:
            MyGUI::LogManager mLog;
            ControlsDataManager mData;
            ControlsRenderManager mRenderer;
            MyGUI::Gui mGui;
            bool mInitialised = false;
            MyGUI::Widget* mPanel = nullptr;
            DialogueColourControls mControls;

            void SetUp() override
            {
                mGui.initialise("");
                mInitialised = true;
                auto& resources = MyGUI::ResourceManager::getInstance();
                resources.addResource(new ControlsFont(mRenderer.getTexture("TestFont")));
                MyGUI::FontManager::getInstance().setDefaultFont("DialogueControlsTest");
                MyGUI::LanguageManager::getInstance().eventRequestTag
                    = MyGUI::newDelegate(this, &DialogueColourControlsTest::requestTag);
                MyGUI::FactoryManager::getInstance().registerFactory<AutoSizedResourceSkin>(
                    resources.getCategoryName());
                auto& layers = MyGUI::LayerManager::getInstance();
                layers.createLayerAt("Settings", "OverlappedLayer", 0);
                layers.createLayerAt("Popup", "OverlappedLayer", 1);
                for (const auto* file : { "core.skin", "openmw_resources.xml", "openmw_text.skin.xml",
                         "openmw_windows.skin.xml", "openmw_button.skin.xml", "openmw_list.skin.xml",
                         "openmw_box.skin.xml", "openmw_scroll.skin.xml" })
                    ASSERT_TRUE(resources.load(file)) << file;
            }

            void TearDown() override
            {
                if (mInitialised)
                    mGui.shutdown();
            }

            void requestTag(const MyGUI::UString& tag, MyGUI::UString& result)
            {
                result = tag.asUTF8().find("fontcolour=") == 0 ? "0.8 0.7 0.5" : "Test caption";
            }

            void createPanel(MyGUI::IntSize size)
            {
                mPanel = mGui.createWidget<MyGUI::Widget>(
                    "", { 0, 0, size.width, size.height }, MyGUI::Align::Default, "Settings");
                mControls = createDialogueColourControls(mPanel, "Test");
                const std::array<std::string, 4> labels{ "Use different dialogue colours",
                    "Keep the normal dialogue system colour", "Recognise descriptions of nearby activities",
                    "Colour descriptions of nearby activities in the journal" };
                for (size_t i = 0; i < labels.size(); ++i)
                {
                    mControls.mToggleLabels[i]->setCaption(labels[i]);
                    mControls.mToggles[i]->setCaption("On");
                }
                mControls.mRoleLabel->setCaption("Text type");
                const std::array<std::string, 3> channels{ "Red", "Green", "Blue" };
                for (size_t i = 0; i < channels.size(); ++i)
                {
                    mControls.mChannelLabels[i]->setCaption(channels[i]);
                    mControls.mValues[i]->setCaption("255");
                }
                mControls.mPreview->setCaption("Your journal has been updated.");
                mControls.mReset->setCaption("Reset this colour");
                mControls.mHint->setCaption("Dialogue colours update immediately. Reopen the journal to refresh its text. "
                                           "Topic links keep their colours.");
                layoutDialogueColourControls(mControls);
            }

            std::vector<MyGUI::Widget*> rows() const
            {
                return { mControls.mToggleLabels[0], mControls.mToggleLabels[1], mControls.mToggleLabels[2],
                    mControls.mToggleLabels[3], mControls.mRoleLabel, mControls.mRole, mControls.mChannelLabels[0],
                    mControls.mChannelLabels[1], mControls.mChannelLabels[2], mControls.mPreviewBackground,
                    mControls.mReset, mControls.mHint };
            }

            void expectOrderedRows() const
            {
                int bottom = 0;
                for (const auto* row : rows())
                {
                    EXPECT_GE(row->getTop(), bottom) << row->getTypeName();
                    EXPECT_GT(row->getWidth(), 0);
                    EXPECT_GT(row->getHeight(), 0);
                    EXPECT_GE(row->getLeft(), 0);
                    EXPECT_LE(row->getRight(), mControls.mContent->getWidth());
                    bottom = row->getBottom();
                }
                EXPECT_LE(bottom, mControls.mContent->getHeight());
                for (size_t i = 0; i < mControls.mToggles.size(); ++i)
                {
                    EXPECT_EQ(mControls.mToggles[i]->getTop(), mControls.mToggleLabels[i]->getTop());
                    EXPECT_LE(mControls.mToggles[i]->getRight(), mControls.mToggleLabels[i]->getLeft());
                }
                for (size_t i = 0; i < mControls.mSliders.size(); ++i)
                {
                    EXPECT_GE(mControls.mSliders[i]->getTop(), mControls.mChannelLabels[i]->getTop());
                    EXPECT_LE(mControls.mSliders[i]->getBottom(), mControls.mChannelLabels[i]->getBottom());
                    EXPECT_LE(mControls.mChannelLabels[i]->getRight(), mControls.mSliders[i]->getLeft());
                    EXPECT_LE(mControls.mSliders[i]->getRight(), mControls.mValues[i]->getLeft());
                    EXPECT_EQ(mControls.mValues[i]->getTop(), mControls.mChannelLabels[i]->getTop());
                    EXPECT_GT(mControls.mSliders[i]->getWidth(), 0);
                }
            }

            static void expectWrappedTextFits(MyGUI::EditBox* text)
            {
                // SandText uses SimpleText, which derives from EditText. Check
                // the real text subwidget and its metrics, not caption storage.
                auto* client = text->getClientWidget();
                auto* subText = client ? client->getSubWidgetText() : text->getSubWidgetText();
                ASSERT_NE(subText, nullptr);
                ASSERT_NE(subText->castType<MyGUI::EditText>(false), nullptr);
                EXPECT_FALSE(text->getCaption().empty());
                const auto measured = text->getTextSize();
                const auto region = text->getTextRegion();
                EXPECT_GT(measured.width, 0);
                EXPECT_GT(measured.height, 0);
                EXPECT_LE(measured.width, region.width);
                EXPECT_LE(measured.height, region.height);
            }
        };

        TEST_F(DialogueColourControlsTest, RowsStaySeparatedWhenParentsAndCanvasResize)
        {
            createPanel({ 800, 650 });
            for (const auto size : { MyGUI::IntSize(800, 650), MyGUI::IntSize(360, 250), MyGUI::IntSize(1100, 800) })
            {
                SCOPED_TRACE(::testing::Message() << size.width << 'x' << size.height);
                mPanel->setSize(size);
                layoutDialogueColourControls(mControls);
                expectOrderedRows();
                const auto orderedRows = rows();
                std::vector<int> tops;
                for (const auto* row : orderedRows)
                    tops.push_back(row->getTop());
                const auto contentSize = mControls.mContent->getSize();
                // Exercise MyGUI's alignment callbacks without a second layout
                // hiding them. HStretch alone recentres these children here.
                mControls.mContent->setSize(contentSize.width, contentSize.height + 100);
                mControls.mContent->setSize(contentSize);
                for (size_t i = 0; i < orderedRows.size(); ++i)
                    EXPECT_EQ(orderedRows[i]->getTop(), tops[i]);
                expectOrderedRows();
            }
        }

        TEST_F(DialogueColourControlsTest, LargeFontsAndWrappedCaptionsFitTheirTextClients)
        {
            createPanel({ 420, 300 });
            const auto before = mControls.mContent->getHeight();
            for (auto* row : rows())
                if (auto* text = row->castType<MyGUI::TextBox>(false))
                    text->setFontHeight(32);
            for (auto* button : mControls.mToggles)
                button->setFontHeight(32);
            for (auto* value : mControls.mValues)
                value->setFontHeight(32);
            mControls.mPreview->setFontHeight(32);
            mControls.mPreview->setCaption("You hear a quiet cough from the traveller standing beside you. "
                                          "A note has been added to your inventory.");
            layoutDialogueColourControls(mControls);
            expectOrderedRows();
            EXPECT_GT(mControls.mContent->getHeight(), before);
            for (auto* text : mControls.mToggleLabels)
            {
                expectWrappedTextFits(text);
                EXPECT_GT(text->getTextSize().height, text->getFontHeight());
            }
            expectWrappedTextFits(mControls.mPreview);
            expectWrappedTextFits(mControls.mHint);
            EXPECT_LE(mControls.mPreview->getBottom(), mControls.mPreviewBackground->getHeight());
            EXPECT_LE(mControls.mPreview->getRight(), mControls.mPreviewBackground->getWidth());
            EXPECT_GT(mControls.mPreview->getTextSize().height, mControls.mPreview->getFontHeight());
            EXPECT_LE(mControls.mReset->getTextSize().width, mControls.mReset->getTextRegion().width);
            EXPECT_LE(mControls.mReset->getTextSize().height, mControls.mReset->getTextRegion().height);

            auto* role = mControls.mRole;
            const size_t selected = role->getItemCount() - 1;
            for (const char* caption : { "Your topics/questions", "My selected questions and conversation topics" })
            {
                SCOPED_TRACE(caption);
                role->setItemNameAt(selected, caption);
                role->setIndexSelected(selected);
                layoutDialogueColourControls(mControls);
                EXPECT_EQ(role->getIndexSelected(), selected);
                EXPECT_EQ(role->getCaption(), MyGUI::UString(caption));
                EXPECT_GT(role->getTextSize().width, 0);
                EXPECT_LE(role->getTextSize().width, role->getTextRegion().width);
                EXPECT_LE(role->getTextSize().height, role->getTextRegion().height);
                expectOrderedRows();
            }
        }

        TEST_F(DialogueColourControlsTest, SmallViewportCanReachTheLastRowAndWidenAgain)
        {
            createPanel({ 320, 240 });
            expectOrderedRows();
            const auto view = mControls.mView->getViewCoord();
            const auto canvas = mControls.mView->getCanvasSize();
            EXPECT_GT(canvas.width, view.width);
            EXPECT_GT(canvas.height, view.height);
            mControls.mView->setViewOffset({ view.width - canvas.width, view.height - canvas.height });
            const auto offset = mControls.mView->getViewOffset();
            EXPECT_LT(offset.left, 0);
            EXPECT_LT(offset.top, 0);
            EXPECT_GE(mControls.mHint->getTop() + offset.top, 0);
            EXPECT_LE(mControls.mHint->getBottom() + offset.top, view.height);
            EXPECT_LE(mControls.mHint->getRight() + offset.left, view.width);
            EXPECT_EQ(mControls.mView->getCanvasSize(), mControls.mContent->getSize());

            mPanel->setSize(1100, 900);
            layoutDialogueColourControls(mControls);
            expectOrderedRows();
            EXPECT_EQ(mControls.mView->getViewOffset(), MyGUI::IntPoint());
            EXPECT_EQ(mControls.mView->getCanvasSize().width, mControls.mView->getViewCoord().width);
            EXPECT_LE(mControls.mView->getCanvasSize().height, mControls.mView->getViewCoord().height);
        }
    }
}
