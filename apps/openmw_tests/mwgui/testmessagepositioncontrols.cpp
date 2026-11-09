#include "apps/openmw/mwgui/messagepositioncontrols.hpp"
#include "apps/openmw/mwgui/mapiconcontrols.hpp"
#include "apps/openmw/mwgui/tooltipcontrols.hpp"
#include "apps/openmw/mwgui/traveltooltipview.hpp"
#include "components/mapmarkers/classification.hpp"
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
#include <MyGUI_TextBox.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
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
            explicit ControlsFont(MyGUI::ITexture* texture, const std::string& name = "MessagePositionControlsFont")
                : mTexture(texture)
            {
                setResourceName(name);
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
                    { "Interface:On", "On" },
                    { "Interface:Off", "Off" },
                    { "OMWEngine:TooltipOptionsDescription",
                        "Choose travel previews, matching-key hints and cleared-location labels. Changes apply "
                        "immediately and are saved when you close Options." },
                    { "OMWEngine:TravelTooltipSection", "Travel Tooltips" },
                    { "OMWEngine:KeyTooltipSection", "If The Key Fits" },
                    { "OMWEngine:ClearedDungeonSection", "Been There, Done That" },
                    { "OMWEngine:TravelTooltipEnabled", "Show illustrated destination previews" },
                    { "OMWEngine:TravelTooltipOverview", "Show the travel overview map" },
                    { "OMWEngine:TravelTooltipGondoliers", "Show gondolier destination previews" },
                    { "OMWEngine:TravelTooltipFallback", "Use Stuporstar art when an image is missing" },
                    { "OMWEngine:TravelTooltipWide", "Use a wide preview layout" },
                    { "OMWEngine:KeyTooltipEnabled", "Show when you carry the matching key" },
                    { "OMWEngine:ClearedDungeonTooltips", "Show cleared status on entrance tooltips" },
                    { "OMWEngine:ClearedDungeonMapTitle", "Show cleared status in the map title" },
                    { "OMWEngine:TravelTooltipPack", "Destination art pack" },
                    { "OMWEngine:TravelTooltipOverviewMap", "Overview map" },
                    { "OMWEngine:TravelTooltipColour", "Artwork tint" },
                    { "OMWEngine:TravelTooltipTitleFont", "Destination title font" },
                    { "OMWEngine:TravelTooltipTextFont", "Description font" },
                    { "OMWEngine:TravelTooltipRoutes", "Travel routes" },
                    { "OMWEngine:TravelTooltipVvardenfell", "Vvardenfell map" },
                    { "OMWEngine:TravelTooltipBookMap", "Book map" },
                    { "OMWEngine:TravelTooltipDefaultFont", "Interface font" },
                    { "OMWEngine:TravelTooltipMonoFont", "Monospace" },
                    { "OMWEngine:TravelTooltipDaedricFont", "Daedric" },
                    { "OMWEngine:TravelTooltipScale", "Preview size" },
                    { "OMWEngine:TooltipOptionsReset", "Reset tooltip options" },
                    { "OMWEngine:TooltipOptionsHint",
                        "Travel previews require the Travel Tooltips data folder. Cleared status tracks hostiles in each "
                        "visited interior, rather than quests or loot. Tracking starts with visits in this build; hiding "
                        "the labels keeps your history. Reset changes only the options on this page." },
                    { "OMWEngine:MapIconsTab", "Map icons" },
                    { "OMWEngine:MapIconsDescription", "Use location symbols on the built-in world map. Advanced World Map is not required." },
                    { "OMWEngine:MapIconsEnabled", "Enable location icons" },
                    { "OMWEngine:MapIconsEntrances", "Show discovered entrance icons" },
                    { "OMWEngine:MapIconsTooltips", "Show location type in tooltips" },
                    { "OMWEngine:MapIconsSize", "Icon size" },
                    { "OMWEngine:MapIconsColours", "Icon colours" },
                    { "OMWEngine:MapIconsMapColours", "Map colours" },
                    { "OMWEngine:MapIconsWhite", "White" },
                    { "OMWEngine:MapIconsCategoryColours", "Category colours" },
                    { "OMWEngine:MapIconsCategory", "Location type" },
                    { "OMWEngine:MapIconsCategoryEnabled", "Use an icon for this type" },
                    { "OMWEngine:MapIconsReset", "Reset icon options" },
                    { "OMWEngine:MapIconsHint", "Entrance markers are discovered in exterior cells you enter. Turning off a type restores a square marker. Sizes follow interface scaling. Changes apply immediately and are saved when you close Options." },
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

        using MapIconControlsTest = MessagePositionControlsTest;

        TEST_F(MapIconControlsTest, NativeFormKeepsAllControlsReachableAtSmallAndLargeSizes)
        {
            mPanel = mGui.createWidget<MyGUI::Widget>(
                "", { 0, 0, 600, 500 }, MyGUI::Align::Default, "Settings");
            auto controls = createMapIconControls(mPanel, "MapTest");
            for (const auto size : { MyGUI::IntSize(600, 500), MyGUI::IntSize(280, 220), MyGUI::IntSize(1100, 900) })
            {
                SCOPED_TRACE(::testing::Message() << size.width << 'x' << size.height);
                mPanel->setSize(size);
                layoutMapIconControls(controls);
                const std::vector<MyGUI::Widget*> rows{ controls.mDescription,
                    controls.mToggleLabels[0], controls.mToggleLabels[1], controls.mToggleLabels[2],
                    controls.mSizeLabel, controls.mSize, controls.mColourLabel, controls.mColours,
                    controls.mCategoryLabel, controls.mCategory, controls.mCategoryToggleLabel,
                    controls.mReset, controls.mHint };
                int bottom = 0;
                for (const auto* row : rows)
                {
                    EXPECT_GE(row->getTop(), bottom);
                    EXPECT_GT(row->getWidth(), 0);
                    EXPECT_LE(row->getRight(), controls.mContent->getWidth());
                    bottom = row->getBottom();
                }
                for (size_t i = 0; i < controls.mToggles.size(); ++i)
                {
                    EXPECT_EQ(controls.mToggles[i]->getTop(), controls.mToggleLabels[i]->getTop());
                    EXPECT_LT(controls.mToggleLabels[i]->getRight(), controls.mToggles[i]->getLeft());
                    EXPECT_LE(controls.mToggles[i]->getRight(), controls.mContent->getWidth());
                }
                for (auto* label : { controls.mDescription, controls.mToggleLabels[0], controls.mToggleLabels[1],
                         controls.mToggleLabels[2], controls.mSizeLabel, controls.mColourLabel,
                         controls.mCategoryLabel, controls.mCategoryToggleLabel, controls.mHint })
                    expectWrappedTextFits(label);
                EXPECT_EQ(controls.mView->getCanvasSize(), controls.mContent->getSize());
                const auto view = controls.mView->getViewCoord();
                const auto canvas = controls.mView->getCanvasSize();
                controls.mView->setViewOffset({ std::min(0, view.width - canvas.width),
                    std::min(0, view.height - canvas.height) });
                const auto offset = controls.mView->getViewOffset();
                EXPECT_LE(controls.mHint->getBottom() + offset.top, view.height);
                EXPECT_LE(controls.mHint->getRight() + offset.left, view.width);
            }
        }

        TEST_F(MapIconControlsTest, EveryCategoryAndColourRetainsSelectionAfterRelayout)
        {
            mPanel = mGui.createWidget<MyGUI::Widget>(
                "", { 0, 0, 360, 240 }, MyGUI::Align::Default, "Settings");
            auto controls = createMapIconControls(mPanel, "MapTest");
            ASSERT_EQ(controls.mCategory->getItemCount(), MapMarkers::categoryCount);
            ASSERT_EQ(controls.mColours->getItemCount(), 3u);
            EXPECT_EQ(controls.mSize->getScrollRange(), 25u);
            EXPECT_EQ(controls.mSize->getScrollPosition(), 8u);
            for (auto* choice : { controls.mCategory, controls.mColours })
            {
                for (size_t i = 0; i < choice->getItemCount(); ++i)
                {
                    choice->setIndexSelected(i);
                    const auto caption = choice->getCaption();
                    layoutMapIconControls(controls);
                    EXPECT_EQ(choice->getIndexSelected(), i);
                    EXPECT_EQ(choice->getCaption(), caption);
                    EXPECT_LE(choice->getTextSize().width, choice->getTextRegion().width);
                    EXPECT_LE(choice->getTextSize().height, choice->getTextRegion().height);
                }
            }
            for (size_t position : { size_t(0), size_t(24) })
            {
                controls.mSize->setScrollPosition(position);
                layoutMapIconControls(controls);
                EXPECT_EQ(controls.mSize->getScrollPosition(), position);
            }
        }

        TEST_F(MapIconControlsTest, LargeFontsAndLongCategoryNamesRemainReadable)
        {
            mPanel = mGui.createWidget<MyGUI::Widget>(
                "", { 0, 0, 360, 240 }, MyGUI::Align::Default, "Settings");
            auto controls = createMapIconControls(mPanel, "MapTest");
            for (auto* label : { controls.mDescription, controls.mToggleLabels[0], controls.mToggleLabels[1],
                     controls.mToggleLabels[2], controls.mSizeLabel, controls.mColourLabel,
                     controls.mCategoryLabel, controls.mCategoryToggleLabel, controls.mHint })
                label->setFontHeight(32);
            for (auto* button : controls.mToggles)
                button->setFontHeight(32);
            controls.mCategoryToggle->setFontHeight(32);
            controls.mReset->setFontHeight(32);
            controls.mCategory->setFontHeight(32);
            controls.mColours->setFontHeight(32);
            controls.mCategory->setItemNameAt(0, "A long translated location category name");
            controls.mCategory->setIndexSelected(0);
            layoutMapIconControls(controls);
            EXPECT_GT(controls.mView->getCanvasSize().width, controls.mView->getViewCoord().width);
            EXPECT_LE(controls.mCategory->getTextSize().width, controls.mCategory->getTextRegion().width);
            EXPECT_LE(controls.mCategory->getTextSize().height, controls.mCategory->getTextRegion().height);
            EXPECT_LT(controls.mCategoryToggleLabel->getRight(), controls.mCategoryToggle->getLeft());
            for (auto* label : { controls.mDescription, controls.mToggleLabels[0], controls.mToggleLabels[1],
                     controls.mToggleLabels[2], controls.mHint })
                expectWrappedTextFits(label);
        }

        TEST_F(MapIconControlsTest, LongCategoryToggleDoesNotGiveItsLabelNegativeWidth)
        {
            mPanel = mGui.createWidget<MyGUI::Widget>(
                "", { 0, 0, 280, 220 }, MyGUI::Align::Default, "Settings");
            auto controls = createMapIconControls(mPanel, "MapTest");
            for (auto* button : controls.mToggles)
                button->setCaption("Off");
            controls.mCategoryToggle->setCaption("A deliberately long translated enabled caption");
            layoutMapIconControls(controls);
            EXPECT_GT(controls.mCategoryToggleLabel->getWidth(), 0);
            EXPECT_LT(controls.mCategoryToggleLabel->getRight(), controls.mCategoryToggle->getLeft());
            EXPECT_LE(controls.mCategoryToggle->getRight(), controls.mContent->getWidth());
            EXPECT_LE(controls.mCategoryToggle->getTextSize().width, controls.mCategoryToggle->getTextRegion().width);
            expectWrappedTextFits(controls.mCategoryToggleLabel);
        }

        class TooltipControlsTest : public MessagePositionControlsTest
        {
        protected:
            TooltipControls mTooltip;

            void createTooltipPanel(MyGUI::IntSize size)
            {
                mPanel = mGui.createWidget<MyGUI::Widget>(
                    "", { 0, 0, size.width, size.height }, MyGUI::Align::Default, "Settings");
                mTooltip = createTooltipControls(mPanel, "TooltipTest");
            }

            std::vector<std::vector<MyGUI::Widget*>> tooltipRows() const
            {
                std::vector<std::vector<MyGUI::Widget*>> result{
                    { mTooltip.mDescription }, { mTooltip.mSections[0] } };
                for (size_t i = 0; i < 5; ++i)
                    result.push_back({ mTooltip.mToggleLabels[i], mTooltip.mToggles[i] });
                for (size_t i = 0; i < mTooltip.mChoices.size(); ++i)
                {
                    result.push_back({ mTooltip.mChoiceLabels[i] });
                    result.push_back({ mTooltip.mChoices[i] });
                }
                result.push_back({ mTooltip.mScaleLabel, mTooltip.mScaleValue });
                result.push_back({ mTooltip.mScale });
                result.push_back({ mTooltip.mSections[1] });
                result.push_back({ mTooltip.mToggleLabels[5], mTooltip.mToggles[5] });
                result.push_back({ mTooltip.mSections[2] });
                for (size_t i = 6; i < mTooltip.mToggles.size(); ++i)
                    result.push_back({ mTooltip.mToggleLabels[i], mTooltip.mToggles[i] });
                result.push_back({ mTooltip.mReset });
                result.push_back({ mTooltip.mHint });
                return result;
            }

            std::vector<MyGUI::EditBox*> tooltipWrappedTexts() const
            {
                std::vector<MyGUI::EditBox*> result{ mTooltip.mDescription, mTooltip.mScaleLabel, mTooltip.mHint };
                result.insert(result.end(), mTooltip.mSections.begin(), mTooltip.mSections.end());
                result.insert(result.end(), mTooltip.mToggleLabels.begin(), mTooltip.mToggleLabels.end());
                result.insert(result.end(), mTooltip.mChoiceLabels.begin(), mTooltip.mChoiceLabels.end());
                return result;
            }

            void expectTooltipRowsSeparated() const
            {
                int bottom = 0;
                for (const auto& row : tooltipRows())
                {
                    int rowBottom = bottom;
                    int right = 0;
                    for (const auto* widget : row)
                    {
                        SCOPED_TRACE(widget->getTypeName());
                        EXPECT_GE(widget->getTop(), bottom);
                        EXPECT_EQ(widget->getTop(), row.front()->getTop());
                        EXPECT_GE(widget->getLeft(), right);
                        EXPECT_GT(widget->getWidth(), 0);
                        EXPECT_GT(widget->getHeight(), 0);
                        EXPECT_LE(widget->getRight(), mTooltip.mContent->getWidth());
                        EXPECT_LE(widget->getBottom(), mTooltip.mContent->getHeight());
                        rowBottom = std::max(rowBottom, widget->getBottom());
                        right = widget->getRight();
                    }
                    bottom = rowBottom;
                }
                EXPECT_EQ(mTooltip.mView->getCanvasSize(), mTooltip.mContent->getSize());
            }

            void expectTooltipTextFits() const
            {
                for (auto* label : tooltipWrappedTexts())
                    expectWrappedTextFits(label);
                const auto fits = [](const MyGUI::TextBox* text) {
                    EXPECT_FALSE(text->getCaption().empty());
                    EXPECT_LE(text->getTextSize().width, text->getTextRegion().width);
                    EXPECT_LE(text->getTextSize().height, text->getTextRegion().height);
                };
                for (const auto* toggle : mTooltip.mToggles)
                    fits(toggle);
                for (const auto* choice : mTooltip.mChoices)
                    fits(choice);
                fits(mTooltip.mScaleValue);
                fits(mTooltip.mReset);
            }

            void expectEveryTooltipRowReachable() const
            {
                const auto view = mTooltip.mView->getViewCoord();
                const auto canvas = mTooltip.mView->getCanvasSize();
                const int minLeft = std::min(0, view.width - canvas.width);
                const int minTop = std::min(0, view.height - canvas.height);
                for (const auto& row : tooltipRows())
                    for (const auto* widget : row)
                    {
                        // Exercise MyGUI's actual canvas clamping, including
                        // horizontal scrolling for an oversized translated row.
                        mTooltip.mView->setViewOffset({ std::clamp(view.width - widget->getRight(), minLeft, 0),
                            std::clamp(view.height - widget->getBottom(), minTop, 0) });
                        const auto offset = mTooltip.mView->getViewOffset();
                        EXPECT_GT(widget->getBottom() + offset.top, 0);
                        EXPECT_LE(widget->getBottom() + offset.top, view.height);
                        EXPECT_GT(widget->getRight() + offset.left, 0);
                        EXPECT_LE(widget->getRight() + offset.left, view.width);
                        if (widget->getHeight() <= view.height)
                            EXPECT_GE(widget->getTop() + offset.top, 0);
                        if (widget->getWidth() <= view.width)
                            EXPECT_GE(widget->getLeft() + offset.left, 0);
                    }
            }
        };

        TEST_F(TooltipControlsTest, AllFourteenControlsStaySeparatedAndReachableAcrossParentSizes)
        {
            createTooltipPanel({ 600, 500 });
            EXPECT_EQ(mTooltip.mToggles.size() + mTooltip.mChoices.size() + 1, 14u);
            for (const auto size : { MyGUI::IntSize(600, 500), MyGUI::IntSize(280, 220),
                     MyGUI::IntSize(1100, 900), MyGUI::IntSize(1100, 1800) })
            {
                SCOPED_TRACE(::testing::Message() << size.width << 'x' << size.height);
                mPanel->setSize(size);
                layoutTooltipControls(mTooltip);
                expectTooltipRowsSeparated();
                expectTooltipTextFits();
                expectEveryTooltipRowReachable();
                std::vector<std::pair<MyGUI::Widget*, MyGUI::IntCoord>> positions;
                for (const auto& row : tooltipRows())
                    for (auto* widget : row)
                        positions.emplace_back(widget, widget->getCoord());
                const auto contentSize = mTooltip.mContent->getSize();
                mTooltip.mContent->setSize(contentSize.width, contentSize.height + 100);
                mTooltip.mContent->setSize(contentSize);
                for (const auto& [widget, position] : positions)
                    EXPECT_EQ(widget->getCoord(), position);
            }
            // A large viewport removes both scroll offsets after the narrow
            // form has scrolled to the end of its final help text.
            EXPECT_EQ(mTooltip.mView->getViewOffset(), MyGUI::IntPoint());
            EXPECT_EQ(mTooltip.mView->getCanvasSize().width, mTooltip.mView->getViewCoord().width);
            EXPECT_LE(mTooltip.mView->getCanvasSize().height, mTooltip.mView->getViewCoord().height);
        }

        TEST_F(TooltipControlsTest, EveryChoiceRetainsItsSelectionAndCaptionAfterRelayout)
        {
            createTooltipPanel({ 360, 240 });
            const std::array<size_t, 5> counts{ 3, 3, 11, 3, 3 };
            const std::array<size_t, 5> defaults{ 2, 0, 0, 2, 0 };
            for (size_t c = 0; c < mTooltip.mChoices.size(); ++c)
            {
                auto* choice = mTooltip.mChoices[c];
                SCOPED_TRACE(c);
                ASSERT_EQ(choice->getItemCount(), counts[c]);
                EXPECT_EQ(choice->getIndexSelected(), defaults[c]);
                for (size_t i = 0; i < choice->getItemCount(); ++i)
                {
                    choice->setIndexSelected(i);
                    const auto caption = choice->getCaption();
                    EXPECT_EQ(caption, choice->getItemNameAt(i));
                    for (const auto size : { MyGUI::IntSize(280, 220), MyGUI::IntSize(1100, 900) })
                    {
                        mPanel->setSize(size);
                        layoutTooltipControls(mTooltip);
                        EXPECT_EQ(choice->getIndexSelected(), i);
                        EXPECT_EQ(choice->getCaption(), caption);
                        expectTooltipRowsSeparated();
                        expectTooltipTextFits();
                    }
                }
            }
        }

        TEST_F(TooltipControlsTest, ScaleAndToggleValuesSurviveRelayoutAndScroll)
        {
            createTooltipPanel({ 360, 240 });
            EXPECT_EQ(mTooltip.mScale->getScrollRange(), 151u);
            EXPECT_EQ(mTooltip.mScale->getScrollPosition(), 50u);
            EXPECT_EQ(mTooltip.mScaleValue->getCaption(), MyGUI::UString("100%"));
            for (size_t i = 0; i < mTooltip.mToggles.size(); ++i)
            {
                mTooltip.mToggles[i]->setStateSelected(i % 2 != 0);
                mTooltip.mToggles[i]->setCaption(i % 2 ? "On" : "Off");
            }
            for (size_t position : { size_t(0), size_t(50), size_t(149), size_t(150) })
            {
                mTooltip.mScale->setScrollPosition(position);
                mTooltip.mScaleValue->setCaption(std::to_string(position + 50) + "%");
                for (const auto size : { MyGUI::IntSize(280, 220), MyGUI::IntSize(1100, 900) })
                {
                    mPanel->setSize(size);
                    layoutTooltipControls(mTooltip);
                    expectEveryTooltipRowReachable();
                    layoutTooltipControls(mTooltip);
                    EXPECT_EQ(mTooltip.mScale->getScrollPosition(), position);
                    EXPECT_EQ(mTooltip.mScaleValue->getCaption(), MyGUI::UString(std::to_string(position + 50) + "%"));
                    for (size_t i = 0; i < mTooltip.mToggles.size(); ++i)
                    {
                        EXPECT_EQ(mTooltip.mToggles[i]->getStateSelected(), i % 2 != 0);
                        EXPECT_EQ(mTooltip.mToggles[i]->getCaption(), MyGUI::UString(i % 2 ? "On" : "Off"));
                    }
                    expectTooltipRowsSeparated();
                    expectTooltipTextFits();
                }
            }
        }

        TEST_F(TooltipControlsTest, LargeFontsAndLongTranslationsFitAndRemainReachable)
        {
            createTooltipPanel({ 360, 240 });
            const int before = mTooltip.mContent->getHeight();
            for (auto* label : tooltipWrappedTexts())
                label->setFontHeight(32);
            for (auto* toggle : mTooltip.mToggles)
            {
                toggle->setFontHeight(32);
                toggle->setCaption("This option is enabled");
            }
            for (auto* label : mTooltip.mToggleLabels)
                label->setCaption("Show this extra hint when it applies to the place or item in view");
            mTooltip.mReset->setFontHeight(32);
            mTooltip.mReset->setCaption("Restore the default tooltip options");
            mTooltip.mScaleValue->setFontHeight(32);
            mTooltip.mScaleValue->setCaption("200%");
            mTooltip.mScale->setScrollPosition(150);
            for (auto* choice : mTooltip.mChoices)
            {
                choice->setFontHeight(32);
                choice->setItemNameAt(0, "A long translated name for this artwork or font choice");
                choice->setIndexSelected(0);
            }
            for (const auto size : { MyGUI::IntSize(280, 220), MyGUI::IntSize(1100, 900) })
            {
                mPanel->setSize(size);
                layoutTooltipControls(mTooltip);
                EXPECT_GT(mTooltip.mContent->getHeight(), before);
                expectTooltipRowsSeparated();
                expectTooltipTextFits();
                expectEveryTooltipRowReachable();
                for (const auto* choice : mTooltip.mChoices)
                {
                    EXPECT_EQ(choice->getIndexSelected(), 0u);
                    EXPECT_EQ(choice->getCaption(), choice->getItemNameAt(0));
                }
                EXPECT_EQ(mTooltip.mScale->getScrollPosition(), 150u);
            }
        }

        class TravelTooltipViewTest : public MessagePositionControlsTest
        {
        protected:
            void SetUp() override
            {
                MessagePositionControlsTest::SetUp();
                auto& resources = MyGUI::ResourceManager::getInstance();
                ASSERT_TRUE(resources.load("openmw_hud_box.skin.xml"));
                // These aliases exercise the real font-selection and wrapping
                // paths; their glyph metrics are substitutes, not game fonts.
                for (const auto* name : { "DefaultFont", "MonoFont", "ScrollFont" })
                    resources.addResource(new ControlsFont(mRenderer.getTexture("TestFont"), name));
                mPanel = mGui.createWidget<MyGUI::Widget>(
                    "", { 0, 0, 1280, 720 }, MyGUI::Align::Default, "Settings");
            }

            static std::string longDescription()
            {
                std::string result;
                for (int i = 0; i < 8; ++i)
                    result += "The road leads past old stone walls to a busy port. Travellers can rest at the inn, "
                              "visit the market, or find a guide for the next stage of their journey. ";
                return result;
            }

            void expectTravelGeometry(MyGUI::IntSize size, const TravelTooltipStyle& style, bool overview) const
            {
                auto* panel = mPanel->findWidget("TravelTooltipPanel");
                auto* title = panel->findWidget("TravelTooltipTitle")->castType<MyGUI::EditBox>();
                auto* description = panel->findWidget("TravelTooltipDescription")->castType<MyGUI::EditBox>();
                auto* image = panel->findWidget("TravelTooltipImage")->castType<MyGUI::ImageBox>();
                EXPECT_EQ(panel->getSize(), size);
                EXPECT_GT(size.width, 0);
                EXPECT_GT(size.height, 0);
                EXPECT_LE(size.width, style.mViewWidth - 16);
                EXPECT_LE(size.height, style.mViewHeight - 16);
                EXPECT_GE(image->getLeft(), 0);
                EXPECT_GE(image->getTop(), 0);
                EXPECT_GT(image->getWidth(), 0);
                EXPECT_GT(image->getHeight(), 0);
                EXPECT_LE(image->getRight(), size.width);
                EXPECT_LE(image->getBottom(), size.height);
                EXPECT_EQ(title->getVisible(), !overview);
                EXPECT_EQ(description->getVisible(), !overview);
                if (overview)
                    return;
                EXPECT_LE(title->getBottom(), image->getTop());
                EXPECT_LE(image->getBottom(), description->getTop());
                for (auto* text : { title, description })
                {
                    EXPECT_GE(text->getLeft(), 0);
                    EXPECT_GE(text->getTop(), 0);
                    EXPECT_LE(text->getRight(), size.width);
                    EXPECT_LE(text->getBottom(), size.height);
                    if (!text->getCaption().empty())
                        expectWrappedTextFits(text);
                }
            }
        };

        TEST_F(TravelTooltipViewTest, LongDestinationDescriptionsFitWithoutClippingOrOverlap)
        {
            TravelTooltipView view(mPanel);
            const TravelTooltips::Image image{ 0, "", "textures/example.tga", 800, 400 };
            const std::string title = "A long destination name, the traveller's guild and market square";
            const std::string description = longDescription();
            for (const auto viewport : { MyGUI::IntSize(480, 360), MyGUI::IntSize(640, 480),
                     MyGUI::IntSize(1280, 720) })
                for (bool wide : { false, true })
                    for (int scale : { 50, 100, 200 })
                        for (int font : { 0, 1, 2 })
                        {
                            SCOPED_TRACE(::testing::Message() << viewport.width << 'x' << viewport.height
                                << " wide=" << wide << " scale=" << scale << " font=" << font);
                            TravelTooltipStyle style;
                            style.mViewWidth = viewport.width;
                            style.mViewHeight = viewport.height;
                            style.mWide = wide;
                            style.mScale = scale;
                            style.mTitleFont = font;
                            style.mTextFont = (font + 1) % 3;
                            style.mFontSize = 32;
                            MyGUI::IntSize size;
                            ASSERT_NO_THROW(size = view.render(image, title, description, false, style));
                            view.setVisible(true);
                            expectTravelGeometry(size, style, false);
                            auto* panel = mPanel->findWidget("TravelTooltipPanel");
                            auto* text = panel->findWidget("TravelTooltipDescription")->castType<MyGUI::EditBox>();
                            EXPECT_EQ(text->getCaption(), MyGUI::UString(description));
                            auto* art = panel->findWidget("TravelTooltipImage")->castType<MyGUI::ImageBox>();
                            EXPECT_NEAR(static_cast<double>(art->getWidth()) / art->getHeight(), 2.0, .01);
                        }
        }

        TEST_F(TravelTooltipViewTest, OverviewAndDestinationTransitionsResetTextVisibility)
        {
            TravelTooltipView view(mPanel);
            const TravelTooltips::Image image{ 0, "", "textures/example.tga", 800, 400 };
            const TravelTooltips::Image overview{ 0, "", "textures/overview.tga", 512, 512 };
            TravelTooltipStyle style;
            style.mViewWidth = 480;
            style.mViewHeight = 360;
            style.mScale = 200;
            auto size = view.render(image, "Balmora", "A short destination description.", false, style);
            view.setVisible(true);
            expectTravelGeometry(size, style, false);
            size = view.render(overview, "Unused title", "Unused description", true, style);
            expectTravelGeometry(size, style, true);
            auto* panel = mPanel->findWidget("TravelTooltipPanel");
            auto* art = panel->findWidget("TravelTooltipImage")->castType<MyGUI::ImageBox>();
            EXPECT_EQ(art->getWidth(), art->getHeight());
            size = view.render(image, "An unknown destination", "", false, style);
            expectTravelGeometry(size, style, false);
            EXPECT_TRUE(panel->findWidget("TravelTooltipDescription")->castType<MyGUI::EditBox>()->getCaption().empty());
            view.setVisible(false);
            EXPECT_FALSE(panel->getVisible());
            view.setVisible(true);
            EXPECT_TRUE(panel->getVisible());
        }

        TEST_F(TravelTooltipViewTest, TooSmallViewportRejectsThePreviewInsteadOfClippingItsBody)
        {
            TravelTooltipView view(mPanel);
            const TravelTooltips::Image image{ 0, "", "textures/example.tga", 800, 400 };
            TravelTooltipStyle style;
            style.mViewWidth = 80;
            style.mViewHeight = 80;
            EXPECT_THROW(view.render(image, "A distant port", longDescription(), false, style), std::runtime_error);
            // Failure is recoverable when the logical viewport becomes larger.
            style.mViewWidth = 640;
            style.mViewHeight = 480;
            MyGUI::IntSize size;
            ASSERT_NO_THROW(size = view.render(image, "A distant port", longDescription(), false, style));
            view.setVisible(true);
            expectTravelGeometry(size, style, false);
        }
    }
}
