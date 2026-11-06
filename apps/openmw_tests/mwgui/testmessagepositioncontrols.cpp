#include "apps/openmw/mwgui/messagepositioncontrols.hpp"
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
#include <MyGUI_LanguageManager.h>
#include <MyGUI_LayerManager.h>
#include <MyGUI_LogManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ResourceManager.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>

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
                setResourceName("MessagePositionControlsFont");
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

        class MessagePositionControlsTest : public testing::Test
        {
        protected:
            MyGUI::LogManager mLog;
            ControlsDataManager mData;
            ControlsRenderManager mRenderer;
            MyGUI::Gui mGui;
            bool mInitialised = false;
            MyGUI::Widget* mPanel = nullptr;
            MessagePositionControls mControls;

            void SetUp() override
            {
                mGui.initialise("");
                mInitialised = true;
                auto& resources = MyGUI::ResourceManager::getInstance();
                resources.addResource(new ControlsFont(mRenderer.getTexture("TestFont")));
                MyGUI::FontManager::getInstance().setDefaultFont("MessagePositionControlsFont");
                MyGUI::LanguageManager::getInstance().eventRequestTag
                    = MyGUI::newDelegate(this, &MessagePositionControlsTest::requestTag);
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
                if (tag.asUTF8().find("fontcolour=") == 0)
                {
                    result = "0.8 0.7 0.5";
                    return;
                }
                const std::map<std::string, std::string> captions{
                    { "OMWEngine:MessagePositionDescription",
                        "Choose where brief game messages appear, including item pickups and lockpicking notices." },
                    { "OMWEngine:MessagePositionAnchor", "Screen position" },
                    { "OMWEngine:MessagePositionTopLeft", "Top left" },
                    { "OMWEngine:MessagePositionTopCentre", "Top centre" },
                    { "OMWEngine:MessagePositionTopRight", "Top right" },
                    { "OMWEngine:MessagePositionMiddleLeft", "Middle left" },
                    { "OMWEngine:MessagePositionCentre", "Centre" },
                    { "OMWEngine:MessagePositionMiddleRight", "Middle right" },
                    { "OMWEngine:MessagePositionBottomLeft", "Bottom left" },
                    { "OMWEngine:MessagePositionBottomCentre", "Bottom centre" },
                    { "OMWEngine:MessagePositionBottomRight", "Bottom right" },
                    { "OMWEngine:MessagePositionHorizontalOffset", "Horizontal offset" },
                    { "OMWEngine:MessagePositionVerticalOffset", "Vertical offset" },
                    { "OMWEngine:MessagePositionHorizontalHint", "Negative moves left; positive moves right." },
                    { "OMWEngine:MessagePositionVerticalHint", "Negative moves up; positive moves down." },
                    { "OMWEngine:MessagePositionReset", "Reset position" },
                    { "OMWEngine:MessagePositionShowTest", "Show test message" },
                    { "OMWEngine:MessagePositionHint",
                        "Offsets use interface pixels and follow your interface scaling. Changes apply immediately "
                        "and are saved when you close Options. Dialogue choices, subtitles and separate mod "
                        "interfaces use their own layouts." },
                };
                const auto caption = captions.find(tag.asUTF8());
                result = caption != captions.end() ? caption->second : "Test caption";
            }

            void createPanel(MyGUI::IntSize size)
            {
                mPanel = mGui.createWidget<MyGUI::Widget>(
                    "", { 0, 0, size.width, size.height }, MyGUI::Align::Default, "Settings");
                mControls = createMessagePositionControls(mPanel, "Test");
            }

            std::vector<MyGUI::Widget*> rows() const
            {
                return { mControls.mDescription, mControls.mPositionLabel, mControls.mPosition,
                    mControls.mOffsetLabels[0], mControls.mSliders[0], mControls.mOffsetHints[0],
                    mControls.mOffsetLabels[1], mControls.mSliders[1], mControls.mOffsetHints[1],
                    mControls.mReset, mControls.mHint };
            }

            std::vector<MyGUI::EditBox*> wrappedTexts() const
            {
                return { mControls.mDescription, mControls.mPositionLabel, mControls.mOffsetLabels[0],
                    mControls.mOffsetHints[0], mControls.mOffsetLabels[1], mControls.mOffsetHints[1],
                    mControls.mHint };
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
                for (size_t i = 0; i < mControls.mValues.size(); ++i)
                {
                    EXPECT_EQ(mControls.mValues[i]->getTop(), mControls.mOffsetLabels[i]->getTop());
                    EXPECT_LE(mControls.mOffsetLabels[i]->getRight(), mControls.mValues[i]->getLeft());
                    EXPECT_LE(mControls.mValues[i]->getRight(), mControls.mContent->getWidth());
                    EXPECT_LE(mControls.mValues[i]->getBottom(), mControls.mSliders[i]->getTop());
                }
                if (mControls.mTest->getTop() == mControls.mReset->getTop())
                    EXPECT_LE(mControls.mReset->getRight(), mControls.mTest->getLeft());
                else
                    EXPECT_LE(mControls.mReset->getBottom(), mControls.mTest->getTop());
                EXPECT_GE(mControls.mTest->getLeft(), 0);
                EXPECT_LE(mControls.mTest->getRight(), mControls.mContent->getWidth());
                EXPECT_LE(mControls.mTest->getBottom(), mControls.mHint->getTop());
            }

            static void expectWrappedTextFits(MyGUI::EditBox* text)
            {
                // Inspect the actual text client and wrapping, not just the
                // parent rectangle or stored caption.
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

        TEST_F(MessagePositionControlsTest, RowsStaySeparatedWhenParentsAndCanvasResize)
        {
            createPanel({ 800, 650 });
            for (const auto size : { MyGUI::IntSize(800, 650), MyGUI::IntSize(360, 250), MyGUI::IntSize(1100, 800) })
            {
                SCOPED_TRACE(::testing::Message() << size.width << 'x' << size.height);
                mPanel->setSize(size);
                layoutMessagePositionControls(mControls);
                expectOrderedRows();
                for (auto* text : wrappedTexts())
                    expectWrappedTextFits(text);
                auto widgets = rows();
                widgets.push_back(mControls.mTest);
                for (auto* value : mControls.mValues)
                    widgets.push_back(value);
                std::vector<int> tops;
                for (const auto* widget : widgets)
                    tops.push_back(widget->getTop());
                const auto contentSize = mControls.mContent->getSize();
                // Exercise real child alignment without relayout hiding a
                // widget drifting down when a scroll canvas changes height.
                mControls.mContent->setSize(contentSize.width, contentSize.height + 100);
                mControls.mContent->setSize(contentSize);
                for (size_t i = 0; i < widgets.size(); ++i)
                    EXPECT_EQ(widgets[i]->getTop(), tops[i]);
                expectOrderedRows();
            }
        }

        TEST_F(MessagePositionControlsTest, EveryAnchorRetainsItsSelectionAndCaptionAcrossLayouts)
        {
            createPanel({ 500, 400 });
            const std::array<std::string, 9> expected{ "Top left", "Top centre", "Top right", "Middle left",
                "Centre", "Middle right", "Bottom left", "Bottom centre", "Bottom right" };
            auto* position = mControls.mPosition;
            ASSERT_EQ(position->getItemCount(), expected.size());
            EXPECT_EQ(position->getCaption(), MyGUI::UString("Bottom centre"));
            for (size_t i = 0; i < expected.size(); ++i)
            {
                SCOPED_TRACE(expected[i]);
                EXPECT_EQ(position->getItemNameAt(i), MyGUI::UString(expected[i]));
                position->setIndexSelected(i);
                for (const auto size : { MyGUI::IntSize(360, 250), MyGUI::IntSize(1100, 800) })
                {
                    mPanel->setSize(size);
                    layoutMessagePositionControls(mControls);
                    EXPECT_EQ(position->getIndexSelected(), i);
                    EXPECT_EQ(position->getCaption(), MyGUI::UString(expected[i]));
                    EXPECT_LE(position->getTextSize().width, position->getTextRegion().width);
                    EXPECT_LE(position->getTextSize().height, position->getTextRegion().height);
                    expectOrderedRows();
                }
            }
            for (auto* slider : mControls.mSliders)
            {
                // The track has an addressable position for every signed
                // logical-pixel value, including both endpoints and zero.
                EXPECT_EQ(slider->getScrollRange(), 2001u);
                EXPECT_EQ(slider->getScrollPosition(), 1000u);
                slider->setScrollPosition(0);
                layoutMessagePositionControls(mControls);
                EXPECT_EQ(slider->getScrollPosition(), 0u);
                slider->setScrollPosition(2000);
                layoutMessagePositionControls(mControls);
                EXPECT_EQ(slider->getScrollPosition(), 2000u);
            }
        }

        TEST_F(MessagePositionControlsTest, LargeFontsAndLongCaptionsFitTheirTextClients)
        {
            createPanel({ 420, 300 });
            const int before = mControls.mContent->getHeight();
            for (auto* text : wrappedTexts())
                text->setFontHeight(32);
            mControls.mPosition->setFontHeight(32);
            mControls.mReset->setFontHeight(32);
            mControls.mTest->setFontHeight(32);
            for (auto* value : mControls.mValues)
                value->setFontHeight(32);
            mControls.mValues[0]->setCaption("-1000");
            mControls.mValues[1]->setCaption("1000");
            mControls.mOffsetLabels[0]->setCaption("Horizontal distance from the selected screen position");
            mControls.mOffsetLabels[1]->setCaption("Vertical distance from the selected screen position");
            layoutMessagePositionControls(mControls);
            expectOrderedRows();
            EXPECT_GT(mControls.mContent->getHeight(), before);
            EXPECT_GE(mControls.mTest->getTop(), mControls.mReset->getBottom());
            for (auto* text : wrappedTexts())
                expectWrappedTextFits(text);
            for (const MyGUI::TextBox* text : { mControls.mValues[0], mControls.mValues[1],
                     static_cast<MyGUI::TextBox*>(mControls.mReset), static_cast<MyGUI::TextBox*>(mControls.mTest) })
            {
                EXPECT_LE(text->getTextSize().width, text->getTextRegion().width);
                EXPECT_LE(text->getTextSize().height, text->getTextRegion().height);
            }
            auto* position = mControls.mPosition;
            position->setItemNameAt(7, "At the bottom edge, horizontally centred");
            position->setIndexSelected(7);
            const MyGUI::UString selectedCaption = position->getCaption();
            layoutMessagePositionControls(mControls);
            EXPECT_EQ(position->getIndexSelected(), 7u);
            EXPECT_EQ(position->getCaption(), selectedCaption);
            EXPECT_LE(position->getTextSize().width, position->getTextRegion().width);
            EXPECT_LE(position->getTextSize().height, position->getTextRegion().height);
            EXPECT_GT(mControls.mView->getCanvasSize().width, mControls.mView->getViewCoord().width);
            expectOrderedRows();
        }

        TEST_F(MessagePositionControlsTest, SmallViewportCanReachTheLastRowAndWidenAgain)
        {
            createPanel({ 320, 240 });
            expectOrderedRows();
            const auto view = mControls.mView->getViewCoord();
            const auto canvas = mControls.mView->getCanvasSize();
            EXPECT_GT(canvas.width, view.width);
            EXPECT_GT(canvas.height, view.height);
            mControls.mView->setViewOffset({ view.width - canvas.width, view.height - canvas.height });
            const auto offset = mControls.mView->getViewOffset();
            EXPECT_EQ(offset, MyGUI::IntPoint(view.width - canvas.width, view.height - canvas.height));
            EXPECT_LT(offset.left, 0);
            EXPECT_LT(offset.top, 0);
            EXPECT_GE(mControls.mHint->getTop() + offset.top, 0);
            EXPECT_LE(mControls.mHint->getBottom() + offset.top, view.height);
            EXPECT_LE(mControls.mHint->getRight() + offset.left, view.width);
            EXPECT_EQ(mControls.mView->getCanvasSize(), mControls.mContent->getSize());

            mPanel->setSize(1100, 900);
            layoutMessagePositionControls(mControls);
            expectOrderedRows();
            EXPECT_EQ(mControls.mView->getViewOffset(), MyGUI::IntPoint());
            EXPECT_EQ(mControls.mView->getCanvasSize().width, mControls.mView->getViewCoord().width);
            EXPECT_LE(mControls.mView->getCanvasSize().height, mControls.mView->getViewCoord().height);
        }
    }
}
