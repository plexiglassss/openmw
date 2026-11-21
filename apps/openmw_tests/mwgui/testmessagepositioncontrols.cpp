#include "apps/openmw/mwgui/messagepositioncontrols.hpp"
#include "apps/openmw/mwgui/settingscategories.hpp"
#include "apps/openmw/mwgui/mapiconcontrols.hpp"
#include "apps/openmw/mwgui/mapimagemarker.hpp"
#include "apps/openmw/mwgui/smartmapcontrols.hpp"
#include "apps/openmw/mwgui/tooltipcontrols.hpp"
#include "apps/openmw/mwgui/tooltipvisibility.hpp"
#include "apps/openmw/mwgui/traveltooltipview.hpp"
#include "components/mapmarkers/classification.hpp"
#include "components/widgets/windowcaption.hpp"
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
#include <MyGUI_InputManager.h>
#include <MyGUI_IRenderTarget.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_LayerManager.h>
#include <MyGUI_LogManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ResourceManager.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_TabControl.h>
#include <MyGUI_TabItem.h>
#include <MyGUI_Window.h>

#ifndef OPENMW_CONTROLS_STANDALONE
#include <MyGUI_LayoutManager.h>
#include <components/widgets/box.hpp>
#include <components/lua_ui/util.hpp>
#endif

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
            void loadFromFile(const std::string& name) override
            {
                ++mLoadCount;
                mSource = name;
                mSize = { 16, 16 };
            }
            void saveToFile(const std::string&) override {}
            void setShader(const std::string&) override {}
            void destroy() override { mSource.clear(); }
            void* lock(MyGUI::TextureUsage) override { return nullptr; }
            void unlock() override {}
            bool isLocked() const override { return false; }
            int getWidth() const override { return mSize.width; }
            int getHeight() const override { return mSize.height; }
            MyGUI::PixelFormat getFormat() const override { return MyGUI::PixelFormat::R8G8B8A8; }
            MyGUI::TextureUsage getUsage() const override { return MyGUI::TextureUsage::Static; }
            size_t getNumElemBytes() const override { return 4; }
            size_t getLoadCount() const { return mLoadCount; }
            const std::string& getSource() const { return mSource; }

        private:
            std::string mName;
            MyGUI::IntSize mSize{ 16, 16 };
            size_t mLoadCount = 0;
            std::string mSource;
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
            MyGUI::ITexture* getTexture(const std::string& name) override
            {
                // Match the native manager's null detach and first-access
                // loading behavior, while substituting the texture data.
                if (name.empty())
                    return nullptr;
                if (const auto it = mTextures.find(name); it != mTextures.end())
                    return it->second.get();
                auto* texture = createTexture(name);
                texture->loadFromFile(name);
                return texture;
            }
            size_t textureCount() const { return mTextures.size(); }
            const ControlsTexture* findTexture(const std::string& name) const
            {
                const auto it = mTextures.find(name);
                return it != mTextures.end() ? it->second.get() : nullptr;
            }
            const ControlsTexture* findTextureBySource(const std::string& source) const
            {
                for (const auto& entry : mTextures)
                    if (entry.second->getSource() == source)
                        return entry.second.get();
                return nullptr;
            }
            std::vector<std::string> textureNames() const
            {
                std::vector<std::string> result;
                for (const auto& entry : mTextures)
                    result.push_back(entry.first);
                return result;
            }
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

        class ControlsRenderTarget final : public MyGUI::IRenderTarget
        {
        public:
            explicit ControlsRenderTarget(MyGUI::IntSize size)
            {
                mInfo.maximumDepth = 1.f;
                mInfo.pixScaleX = 1.f / size.width;
                mInfo.pixScaleY = 1.f / size.height;
                mInfo.aspectCoef = static_cast<float>(size.height) / size.width;
            }
            void begin() override {}
            void end() override {}
            const MyGUI::RenderTargetInfo& getInfo() const override { return mInfo; }
            void doRender(MyGUI::IVertexBuffer*, MyGUI::ITexture* texture, size_t count) override
            {
                mVertices[texture] += count;
            }
            size_t verticesFor(const MyGUI::ITexture* texture) const
            {
                const auto found = mVertices.find(texture);
                return found == mVertices.end() ? 0 : found->second;
            }

        private:
            MyGUI::RenderTargetInfo mInfo{};
            std::map<const MyGUI::ITexture*, size_t> mVertices;
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
                MyGUI::FactoryManager::getInstance().registerFactory<Gui::WindowCaption>("Widget");
                auto& layers = MyGUI::LayerManager::getInstance();
                layers.createLayerAt("Settings", "OverlappedLayer", 0);
                layers.createLayerAt("Popup", "OverlappedLayer", 1);
                for (const auto* file : { "core.skin", "openmw_resources.xml", "openmw_text.skin.xml",
                         "openmw_windows.skin.xml", "openmw_button.skin.xml", "openmw_list.skin.xml",
                         "openmw_box.skin.xml", "openmw_scroll.skin.xml", "openmw_edit.skin.xml" })
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
                    { "OMWEngine:Preferences", "Preferences" },
                    { "OMWEngine:Audio", "Audio" },
                    { "OMWEngine:Controls", "Controls" },
                    { "OMWEngine:Video", "Video" },
                    { "OMWEngine:Scripts", "Mods" },
                    { "OMWEngine:Language", "Language" },
                    { "OMWEngine:SettingsCustom", "Custom" },
                    { "Interface:On", "On" },
                    { "Interface:Off", "Off" },
                    { "OMWEngine:SmartMapDescription",
                        "Automatically choose the local or world view of the built-in map when you enter a cell." },
                    { "OMWEngine:SmartMapEnabled", "Enable Smart Map" },
                    { "OMWEngine:SmartMapInteriors", "Use the local map in interiors" },
                    { "OMWEngine:SmartMapExteriors", "Choose the map view outdoors" },
                    { "OMWEngine:SmartMapHostiles", "Use the local map near hostile actors" },
                    { "OMWEngine:SmartMapLocationNames", "Replace internal map titles with readable names" },
                    { "OMWEngine:SmartMapDoorThreshold", "Entrances needed for the local map" },
                    { "OMWEngine:SmartMapFightThreshold", "Minimum hostile Fight value" },
                    { "OMWEngine:SmartMapHostileDistance", "Hostile detection distance (game units)" },
                    { "OMWEngine:SmartMapReset", "Reset Smart Map options" },
                    { "OMWEngine:SmartMapHint",
                        "Changes apply immediately and are saved when you close Options. If a mod replaces the "
                        "built-in map, disable its map replacement to use automatic switching. Reset changes only this page." },
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
                    { "OMWEngine:MapIconsLocalDoors", "Show door icons on the local map and minimap" },
                    { "OMWEngine:MapIconsLocalSize", "Local door size at normal zoom" },
                    { "OMWEngine:MapIconsMaximum", "Maximum icon size on both maps" },
                    { "OMWEngine:MapIconsVanillaColours", "Original vanilla colours" },
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

        class SmartMapControlsTest : public MessagePositionControlsTest
        {
        protected:
            SmartMapControls mSmartMap;

            void createForm()
            {
                mPanel = mGui.createWidget<MyGUI::Widget>(
                    "", { 0, 0, 600, 500 }, MyGUI::Align::Default, "SmartMapSettings");
                mSmartMap = createSmartMapControls(mPanel, "SmartMapTest");
            }

            void expectReadableRows()
            {
                const std::vector<MyGUI::Widget*> rows{ mSmartMap.mDescription,
                    mSmartMap.mToggleLabels[0], mSmartMap.mToggleLabels[1], mSmartMap.mToggleLabels[2],
                    mSmartMap.mSliderLabels[0], mSmartMap.mSliders[0], mSmartMap.mToggleLabels[3],
                    mSmartMap.mSliderLabels[1], mSmartMap.mSliders[1], mSmartMap.mSliderLabels[2],
                    mSmartMap.mSliders[2], mSmartMap.mToggleLabels[4], mSmartMap.mReset, mSmartMap.mHint };
                int bottom = 0;
                for (const auto* row : rows)
                {
                    EXPECT_GE(row->getTop(), bottom);
                    EXPECT_GT(row->getWidth(), 0);
                    EXPECT_LE(row->getRight(), mSmartMap.mContent->getWidth());
                    bottom = row->getBottom();
                }
                for (size_t i = 0; i < mSmartMap.mToggles.size(); ++i)
                {
                    EXPECT_EQ(mSmartMap.mToggles[i]->getTop(), mSmartMap.mToggleLabels[i]->getTop());
                    EXPECT_LT(mSmartMap.mToggleLabels[i]->getRight(), mSmartMap.mToggles[i]->getLeft());
                    EXPECT_LE(mSmartMap.mToggles[i]->getRight(), mSmartMap.mContent->getWidth());
                    EXPECT_LE(mSmartMap.mToggles[i]->getTextSize().width, mSmartMap.mToggles[i]->getTextRegion().width);
                    expectWrappedTextFits(mSmartMap.mToggleLabels[i]);
                }
                for (size_t i = 0; i < mSmartMap.mSliders.size(); ++i)
                {
                    EXPECT_LT(mSmartMap.mSliderLabels[i]->getRight(), mSmartMap.mValues[i]->getLeft());
                    EXPECT_LE(mSmartMap.mValues[i]->getRight(), mSmartMap.mContent->getWidth());
                    EXPECT_LE(mSmartMap.mValues[i]->getTextSize().width, mSmartMap.mValues[i]->getTextRegion().width);
                    expectWrappedTextFits(mSmartMap.mSliderLabels[i]);
                }
                expectWrappedTextFits(mSmartMap.mDescription);
                expectWrappedTextFits(mSmartMap.mHint);
                EXPECT_LE(mSmartMap.mReset->getTextSize().width, mSmartMap.mReset->getTextRegion().width);
                EXPECT_LE(mSmartMap.mReset->getTextSize().height, mSmartMap.mReset->getTextRegion().height);
                EXPECT_EQ(mSmartMap.mView->getCanvasSize(), mSmartMap.mContent->getSize());
            }
        };

        TEST_F(SmartMapControlsTest, AllOptionsRemainReadableAndReachableAtSmallAndLargeSizes)
        {
            createForm();
            for (const auto size : { MyGUI::IntSize(600, 500), MyGUI::IntSize(280, 220), MyGUI::IntSize(1100, 900) })
            {
                SCOPED_TRACE(::testing::Message() << size.width << 'x' << size.height);
                mPanel->setSize(size);
                layoutSmartMapControls(mSmartMap);
                expectReadableRows();
                const auto view = mSmartMap.mView->getViewCoord();
                const auto canvas = mSmartMap.mView->getCanvasSize();
                mSmartMap.mView->setViewOffset({ std::min(0, view.width - canvas.width),
                    std::min(0, view.height - canvas.height) });
                const auto offset = mSmartMap.mView->getViewOffset();
                EXPECT_GE(mSmartMap.mHint->getTop() + offset.top, 0);
                EXPECT_LE(mSmartMap.mHint->getBottom() + offset.top, view.height);
                EXPECT_LE(mSmartMap.mHint->getRight() + offset.left, view.width);
            }
        }

        TEST_F(SmartMapControlsTest, SupportedThresholdEndpointsSurviveResizingAndScrolling)
        {
            createForm();
            const std::array<size_t, 3> ranges{ 50, 23, 6001 };
            const std::array<size_t, 3> initial{ 3, 4, 2500 };
            for (size_t i = 0; i < ranges.size(); ++i)
            {
                EXPECT_EQ(mSmartMap.mSliders[i]->getScrollRange(), ranges[i]);
                EXPECT_EQ(mSmartMap.mSliders[i]->getScrollPosition(), initial[i]);
                EXPECT_EQ(mSmartMap.mSliders[i]->getUserString("ToolTipLayout"), "TextToolTip");
                EXPECT_FALSE(mSmartMap.mSliders[i]->getUserString("Caption_Text").empty());
                for (size_t pos : { size_t(0), ranges[i] - 1 })
                {
                    mSmartMap.mSliders[i]->setScrollPosition(pos);
                    mPanel->setSize(280, 220);
                    layoutSmartMapControls(mSmartMap);
                    const auto view = mSmartMap.mView->getViewCoord();
                    const auto canvas = mSmartMap.mView->getCanvasSize();
                    mSmartMap.mView->setViewOffset({ view.width - canvas.width, view.height - canvas.height });
                    layoutSmartMapControls(mSmartMap);
                    EXPECT_EQ(mSmartMap.mSliders[i]->getScrollPosition(), pos);
                }
            }
            for (auto* button : mSmartMap.mToggles)
            {
                EXPECT_EQ(button->getUserString("ToolTipLayout"), "TextToolTip");
                EXPECT_FALSE(button->getUserString("Caption_Text").empty());
            }
        }

        TEST_F(SmartMapControlsTest, LargeFontsAndLongTranslationsDoNotOverlapOrClipControls)
        {
            createForm();
            for (auto* label : mSmartMap.mToggleLabels)
            {
                label->setFontHeight(32);
                label->setCaption("A much longer translated description of this automatic map selection option");
            }
            for (auto* button : mSmartMap.mToggles)
            {
                button->setFontHeight(32);
                button->setCaption("This option is enabled");
            }
            for (auto* label : mSmartMap.mSliderLabels)
                label->setFontHeight(32);
            for (auto* value : mSmartMap.mValues)
                value->setFontHeight(32);
            mSmartMap.mDescription->setFontHeight(32);
            mSmartMap.mHint->setFontHeight(32);
            mSmartMap.mReset->setFontHeight(32);
            mSmartMap.mReset->setCaption("Restore the default automatic map options");
            mPanel->setSize(280, 220);
            layoutSmartMapControls(mSmartMap);
            expectReadableRows();
            EXPECT_GT(mSmartMap.mView->getCanvasSize().width, mSmartMap.mView->getViewCoord().width);
            EXPECT_GT(mSmartMap.mView->getCanvasSize().height, mSmartMap.mView->getViewCoord().height);
        }

        TEST_F(SmartMapControlsTest, KeyboardFocusRevealsLastAndFirstControlsInSmallViewport)
        {
            createForm();
            mPanel->setSize(280, 220);
            layoutSmartMapControls(mSmartMap);
            mSmartMap.mView->setViewOffset({ 0, 0 });
            for (auto* button : { mSmartMap.mReset, mSmartMap.mToggles[0], mSmartMap.mToggles[4] })
            {
                MyGUI::InputManager::getInstance().setKeyFocusWidget(button);
                ASSERT_EQ(MyGUI::InputManager::getInstance().getKeyFocusWidget(), button);
                const auto offset = mSmartMap.mView->getViewOffset();
                const auto view = mSmartMap.mView->getViewCoord();
                EXPECT_GE(button->getLeft() + offset.left, 0);
                EXPECT_GE(button->getTop() + offset.top, 0);
                EXPECT_LE(button->getRight() + offset.left, view.width);
                EXPECT_LE(button->getBottom() + offset.top, view.height);
            }
            EXPECT_LT(mSmartMap.mView->getViewOffset().top, 0);
        }

        TEST_F(SmartMapControlsTest, MouseWheelOverLabelsAndButtonsScrollsWithoutChangingThresholds)
        {
            createForm();
            mPanel->setSize(280, 220);
            layoutSmartMapControls(mSmartMap);
            mSmartMap.mView->setViewOffset({ 0, 0 });
            mSmartMap.mToggleLabels[0]->_riseMouseWheel(-120);
            const int labelOffset = mSmartMap.mView->getViewOffset().top;
            EXPECT_LT(labelOffset, 0);
            mSmartMap.mToggles[1]->_riseMouseWheel(-120);
            EXPECT_LT(mSmartMap.mView->getViewOffset().top, labelOffset);
            EXPECT_EQ(mSmartMap.mSliders[0]->getScrollPosition(), 3u);
            EXPECT_EQ(mSmartMap.mSliders[1]->getScrollPosition(), 4u);
            EXPECT_EQ(mSmartMap.mSliders[2]->getScrollPosition(), 2500u);
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
                    controls.mToggleLabels[0], controls.mToggleLabels[1], controls.mToggleLabels[2], controls.mToggleLabels[3],
                    controls.mSizeLabel, controls.mSize, controls.mLocalSizeLabel, controls.mLocalSize,
                    controls.mMaximumLabel, controls.mMaximum, controls.mColourLabel, controls.mColours,
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
            ASSERT_EQ(controls.mColours->getItemCount(), 2u);
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

        TEST_F(MapIconControlsTest, IndependentLocalBaseAndMaximumSlidersRetainTheirValues)
        {
            mPanel = mGui.createWidget<MyGUI::Widget>("", { 0, 0, 380, 300 }, MyGUI::Align::Default, "Settings");
            auto controls = createMapIconControls(mPanel, "MapTest");
            EXPECT_EQ(controls.mLocalSize->getScrollRange(), 33u);
            EXPECT_EQ(controls.mMaximum->getScrollRange(), 57u);
            EXPECT_EQ(controls.mLocalSize->getScrollPosition(), 7u);
            EXPECT_EQ(controls.mMaximum->getScrollPosition(), 16u);
            controls.mMaximum->setScrollPosition(0);
            controls.mLocalSize->setScrollPosition(32);
            controls.mSize->setScrollPosition(24);
            controls.mColours->setIndexSelected(1);
            layoutMapIconControls(controls);
            EXPECT_EQ(controls.mMaximum->getScrollPosition(), 0u);
            EXPECT_EQ(controls.mLocalSize->getScrollPosition(), 32u);
            EXPECT_EQ(controls.mSize->getScrollPosition(), 24u);
            EXPECT_EQ(controls.mColours->getIndexSelected(), 1u);
        }

        TEST_F(MapIconControlsTest, RecycledMarkerKeepsOneCompleteTileWhenArtworkSizeChanges)
        {
            MyGUI::FactoryManager::getInstance().registerFactory<MapImageMarker>("Widget");
            auto* marker = mGui.createWidget<MapImageMarker>(
                "ImageBox", { 20, 20, 24, 24 }, MyGUI::Align::Default, "Settings");
            // These widths match the ordinary door square, the vanilla map icons and the old native art.
            for (const int pixels : { 8, 32, 64, 32, 8, 64 })
            {
                SCOPED_TRACE(pixels);
                const std::string path = "MarkerFixture" + std::to_string(pixels);
                auto* texture = mRenderer.getTexture(path);
                texture->createManual(pixels, pixels, MyGUI::TextureUsage::Static, MyGUI::PixelFormat::R8G8B8A8);
                marker->setMarkerImage(path, pixels);
                EXPECT_EQ(marker->getItemCount(), 1u);
                EXPECT_EQ(marker->getImageIndex(), 0u);
                ControlsRenderTarget target(mRenderer.getViewSize());
                MyGUI::LayerManager::getInstance().renderToTarget(&target, true);
                EXPECT_EQ(target.verticesFor(texture), 6u);
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

        TEST_F(TravelTooltipViewTest, HiddenHudMenuTooltipsRenderAcrossVisibilityTransitions)
        {
            // Use the native tooltip's Popup parent and its production visibility
            // policy. A locally visible preview can still be entirely suppressed
            // by this ancestor when a dialogue mod hides the gameplay HUD.
            mGui.destroyWidget(mPanel);
            mPanel = mGui.createWidget<MyGUI::Widget>(
                "", { 0, 0, 300, 300 }, MyGUI::Align::Default, "Popup");
            mPanel->setNeedMouseFocus(false);
            mRenderer.setViewSize(1280, 720);
            TravelTooltipView view(mPanel);
            const TravelTooltips::Image image{ 0, "", "textures/destination.tga", 800, 400 };
            const auto size = view.render(
                image, "Balmora", "A destination description that must remain visible.", false, TravelTooltipStyle{});
            auto* panel = mPanel->findWidget("TravelTooltipPanel");
            auto* title = panel->findWidget("TravelTooltipTitle");
            auto* description = panel->findWidget("TravelTooltipDescription");
            auto* art = panel->findWidget("TravelTooltipImage");
            const auto* imageTexture = mRenderer.findTextureBySource(image.mPath);
            ASSERT_NE(imageTexture, nullptr);
            const auto* fontTexture = mRenderer.findTexture("TestFont");
            ASSERT_NE(fontTexture, nullptr);

            const auto transition = [&](const char* state, bool hudEnabled, bool guiMode, bool loading,
                                        bool expectedVisible) {
                SCOPED_TRACE(state);
                mPanel->setVisible(isTooltipRootVisible(hudEnabled, guiMode, loading));
                // ToolTips::update hides its children before showing the current
                // hover preview. Keep reusing the same loaded image across modes.
                for (size_t i = 0; i < mPanel->getChildCount(); ++i)
                    mPanel->getChildAt(i)->setVisible(false);
                view.setVisible(true);
                mPanel->setCoord(60, 80, size.width, size.height);

                ControlsRenderTarget target(mRenderer.getViewSize());
                MyGUI::LayerManager::getInstance().renderToTarget(&target, false);
                EXPECT_TRUE(panel->getVisible());
                for (const auto* widget : { mPanel, panel, title, description, art })
                    EXPECT_EQ(widget->getInheritedVisible(), expectedVisible);
                if (expectedVisible)
                {
                    EXPECT_EQ(target.verticesFor(imageTexture), 6u);
                    EXPECT_GT(target.verticesFor(fontTexture), 0u);
                }
                else
                {
                    EXPECT_EQ(target.verticesFor(imageTexture), 0u);
                    EXPECT_EQ(target.verticesFor(fontTexture), 0u);
                }
            };

            transition("HUD hidden during gameplay", false, false, false, false);
            transition("Dialogue entered while HUD stays hidden", false, true, false, true);
            transition("Travel opened over Dialogue", false, true, false, true);
            transition("Travel closed back to Dialogue", false, true, false, true);
            transition("All menus closed while HUD stays hidden", false, false, false, false);
            transition("Ordinary gameplay with HUD enabled", true, false, false, true);
            transition("Loading with HUD enabled", true, true, true, false);
            transition("Loading wallpaper with HUD hidden", false, true, true, false);
            transition("Travel resumed with HUD hidden", false, true, false, true);
            transition("Travel and Dialogue closed again", false, false, false, false);
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

        TEST_F(TravelTooltipViewTest, DestinationChangesReuseOneTextureAndReleaseItWithTheView)
        {
            std::vector<std::string> persistentTextures;
            std::string ownedName;
            {
                TravelTooltipView view(mPanel);
                // Skin and font textures are created by the real widgets. The
                // optional destination image should add only one owned texture.
                persistentTextures = mRenderer.textureNames();
                TravelTooltips::Image image{ 0, "", "textures/first_destination.tga", 800, 400 };
                TravelTooltipStyle style;
                ASSERT_NO_THROW(view.render(image, "First port", "A short description.", false, style));
                const auto* owned = mRenderer.findTextureBySource(image.mPath);
                ASSERT_NE(owned, nullptr);
                ownedName = owned->getName();
                EXPECT_NE(ownedName, image.mPath);
                EXPECT_EQ(mRenderer.textureCount(), persistentTextures.size() + 1);
                EXPECT_EQ(owned->getLoadCount(), 1u);

                for (size_t i = 0; i < 64; ++i)
                {
                    SCOPED_TRACE(i);
                    image.mPath = "textures/destination_" + std::to_string(i) + ".tga";
                    ASSERT_NO_THROW(view.render(image, "Another port", "A short description.", false, style));
                    ASSERT_EQ(mRenderer.findTextureBySource(image.mPath), owned);
                    ASSERT_EQ(mRenderer.findTexture(ownedName), owned);
                    EXPECT_EQ(mRenderer.textureCount(), persistentTextures.size() + 1);
                    EXPECT_EQ(owned->getLoadCount(), i + 2);

                    // Geometry, colour, font and overview changes with the same
                    // source must not decode or register the image again.
                    style.mScale = std::array{ 50, 100, 200 }[i % 3];
                    style.mWide = i % 2 != 0;
                    style.mColour = static_cast<int>(i % 11);
                    style.mTitleFont = static_cast<int>(i % 3);
                    style.mTextFont = static_cast<int>((i + 1) % 3);
                    style.mViewWidth = i % 2 ? 640 : 1280;
                    style.mViewHeight = i % 2 ? 480 : 720;
                    ASSERT_NO_THROW(view.render(image, "Another port", "A short description.", false, style));
                    ASSERT_NO_THROW(view.render(image, "", "", true, style));
                    EXPECT_EQ(mRenderer.findTextureBySource(image.mPath), owned);
                    EXPECT_EQ(owned->getLoadCount(), i + 2);
                    EXPECT_EQ(mRenderer.textureCount(), persistentTextures.size() + 1);
                }
                for (const auto& name : persistentTextures)
                    EXPECT_NE(mRenderer.findTexture(name), nullptr);
            }
            EXPECT_EQ(mRenderer.findTexture(ownedName), nullptr);
            EXPECT_EQ(mRenderer.textureNames(), persistentTextures);
            // Parent-owned widgets are still alive here; destroying them after
            // the view's image release must remain a normal MyGUI teardown.
            mGui.destroyWidget(mPanel);
            mPanel = nullptr;
            EXPECT_EQ(mRenderer.textureNames(), persistentTextures);
        }
        class SettingsCategoriesTest : public MessagePositionControlsTest
        {
        protected:
            MyGUI::TabControl* mTabs = nullptr;
            int mClicks = 0;
            void createTabs()
            {
                mTabs = mGui.createWidget<MyGUI::TabControl>(
                    "TabControl", { 0, 0, 640, 500 }, MyGUI::Align::Default, "Settings");
            }
            MyGUI::TabItem* page(const std::string& id)
            {
                auto* tab = mTabs->addItem("Translated " + id);
                if (!id.empty())
                    tab->setUserString("SettingsPage", id);
                return tab;
            }
            MyGUI::TabItem* customPage(const std::string& id)
            {
                auto* tab = page(id);
                tab->setUserString("CustomSettingsPage", id);
                return tab;
            }
            MyGUI::TabItem* customTab()
            {
                return mTabs->getItemAt(mTabs->getItemCount() - 1);
            }
            MyGUI::ComboBox* selector(size_t index)
            {
                auto* tab = mTabs->getItemAt(index);
                for (size_t i = 0; i < tab->getChildCount(); ++i)
                    if (tab->getChildAt(i)->getUserString("SettingsCategorySelector") == "true")
                        return tab->getChildAt(i)->castType<MyGUI::ComboBox>();
                return nullptr;
            }
            template <class T>
            static T* ancestor(MyGUI::Widget* child)
            {
                for (auto* p = child->getParent(); p; p = p->getParent())
                    if (auto* result = p->castType<T>(false))
                        return result;
                return nullptr;
            }
            void clicked(MyGUI::Widget*) { ++mClicks; }
            void bindClick(MyGUI::Button* button)
            {
                button->eventMouseButtonClick += MyGUI::newDelegate(this, &SettingsCategoriesTest::clicked);
            }
        };

        TEST_F(SettingsCategoriesTest, StockPagesAndUnknownLayoutControlsKeepTheirParentsGeometryAndBindings)
        {
            createTabs();
            auto* prefs = page("Preferences");
            auto* game = prefs->createWidget<MyGUI::ScrollBar>("MW_HScroll", { 4, 4, 300, 14 }, MyGUI::Align::Default);
            game->setUserString("SettingCategory", "Game");
            game->setUserString("SettingName", "difficulty");
            game->setScrollRange(201);
            game->setScrollPosition(123);
            auto* hud = prefs->createWidget<MyGUI::Button>("MW_Button", { 4, 40, 200, 28 }, MyGUI::Align::Default);
            hud->setUserString("SettingCategory", "HUD");
            hud->setUserString("SettingName", "crosshair");
            bindClick(hud);
            std::vector<MyGUI::TabItem*> stock{ prefs };
            std::vector<MyGUI::Widget*> retained{ game, hud };
            for (const auto* id : { "Audio", "Controls", "Video", "Mods", "Language" })
            {
                auto* tab = page(id);
                stock.push_back(tab);
                retained.push_back(tab->createWidget<MyGUI::Widget>("", { 0, 0, 300, 300 }, MyGUI::Align::Stretch));
            }
            auto* unknown = page("");
            // Familiar control names and setting categories do not establish
            // ownership of another layout's page.
            auto* extra = unknown->createWidget<MyGUI::Button>(
                "MW_Button", { 4, 4, 100, 28 }, MyGUI::Align::Default, "OtherJournalTextAlignment");
            extra->setUserString("SettingName", "delevel creatures");
            stock.push_back(unknown);
            retained.push_back(extra);
            std::vector<MyGUI::Widget*> parents;
            std::vector<MyGUI::IntCoord> coords;
            std::vector<MyGUI::Align> aligns;
            for (auto* widget : retained)
            {
                parents.push_back(widget->getParent());
                coords.push_back(widget->getCoord());
                aligns.push_back(widget->getAlign());
            }
            customPage("Gameplay")->createWidget<MyGUI::Widget>("", { 0, 0, 100, 100 }, MyGUI::Align::Stretch);
            customPage("Journal")->createWidget<MyGUI::Widget>("", { 0, 0, 100, 100 }, MyGUI::Align::Stretch);
            auto smartMap = createSmartMapControls(customPage("SmartMap"), "CategoryTest");
            smartMap.mSliders[0]->setScrollPosition(5);
            mTabs->setItemSelected(stock[4]);
            SettingsCategories categories(mTabs);
            ASSERT_EQ(mTabs->getItemCount(), stock.size() + 1);
            EXPECT_EQ(mTabs->getItemSelected(), stock[4]);
            for (size_t i = 0; i < stock.size(); ++i)
                EXPECT_EQ(mTabs->getItemAt(i), stock[i]);
            for (size_t i = 0; i < retained.size(); ++i)
            {
                EXPECT_EQ(retained[i]->getParent(), parents[i]);
                EXPECT_EQ(retained[i]->getCoord(), coords[i]);
                EXPECT_EQ(retained[i]->getAlign(), aligns[i]);
            }
            ASSERT_EQ(selector(stock.size())->getItemCount(), 3u);
            EXPECT_EQ(customTab()->getUserString("SettingsPage"), "Custom");
            EXPECT_EQ(customTab()->getUserString("AllowTabOverflow"), "true");
            EXPECT_EQ(game->getScrollPosition(), 123u);
            hud->eventMouseButtonClick(hud);
            EXPECT_EQ(mClicks, 1);
            mTabs->setSize(960, 700);
            categories.layout();
            layoutSmartMapControls(smartMap);
            EXPECT_EQ(smartMap.mSliders[0]->getScrollPosition(), 5u);
            EXPECT_EQ(smartMap.mView->getParent()->getWidth(), customTab()->getWidth());
            EXPECT_EQ(extra->getParent(), unknown);
        }

        TEST_F(SettingsCategoriesTest, UnmarkedLegacyJournalStaysOnItsPageAndOwnedFallbacksMerge)
        {
            createTabs();
            auto* legacy = page("Journal");
            auto* alignment = legacy->createWidget<MyGUI::ComboBox>(
                "MW_ComboBox", { 4, 4, 240, 28 }, MyGUI::Align::Default, "OldJournalTextAlignment");
            alignment->addItem("Left");
            alignment->addItem("Justify");
            alignment->setIndexSelected(1);
            auto* owned = customPage("Journal");
            auto* oldFont = owned->createWidget<MyGUI::ComboBox>(
                "MW_ComboBox", { 4, 4, 240, 28 }, MyGUI::Align::Default, "OldJournalFontSize");
            auto* fallback = customPage("Journal");
            auto* font = fallback->createWidget<MyGUI::ComboBox>(
                "MW_ComboBox", { 4, 4, 240, 28 }, MyGUI::Align::Default, "NewJournalFontSizeFallback");
            SettingsCategories categories(mTabs);
            ASSERT_EQ(mTabs->getItemCount(), 2u);
            ASSERT_EQ(selector(1)->getItemCount(), 1u);
            EXPECT_EQ(alignment->getParent(), legacy);
            EXPECT_EQ(alignment->getCoord(), MyGUI::IntCoord(4, 4, 240, 28));
            EXPECT_EQ(ancestor<MyGUI::ScrollView>(font), ancestor<MyGUI::ScrollView>(oldFont));
            EXPECT_EQ(ancestor<MyGUI::TabItem>(font), customTab());
            EXPECT_GT(font->getTop(), oldFont->getBottom());
            EXPECT_EQ(alignment->getIndexSelected(), 1u);
        }

        TEST_F(SettingsCategoriesTest, NoOwnedPagesLeavesTheOriginalTabsAndSelectionIntact)
        {
            createTabs();
            auto* prefs = page("Preferences");
            auto* language = page("Language");
            auto* unknown = customPage("OtherExtension");
            mTabs->setItemSelected(language);
            SettingsCategories categories(mTabs);
            ASSERT_EQ(mTabs->getItemCount(), 3u);
            EXPECT_EQ(mTabs->getItemAt(0), prefs);
            EXPECT_EQ(mTabs->getItemAt(1), language);
            EXPECT_EQ(mTabs->getItemAt(2), unknown);
            EXPECT_EQ(mTabs->getItemSelected(), language);
        }

        TEST_F(SettingsCategoriesTest, MinimumSizeAppliesBeforeCenteringAndCustomHostsTrackProgrammaticResize)
        {
            auto* window = mGui.createWidget<MyGUI::Window>(
                "MW_Window", { 0, 0, 400, 485 }, MyGUI::Align::Default, "Settings");
            window->setMinSize({ 640, 550 });
            mTabs = window->createWidget<MyGUI::TabControl>(
                "TabControl", { 8, 8, 368, 405 }, MyGUI::Align::Stretch);
            mTabs->setButtonAutoWidth(true);
            auto* stock = page("Preferences");
            auto* control = stock->createWidget<MyGUI::Widget>("", { 4, 4, 300, 300 }, MyGUI::Align::Stretch);
            auto controls = createSmartMapControls(customPage("SmartMap"), "InitialSizeTest");
            SettingsCategories categories(mTabs);
            customTab()->setCaption(std::string(300, 'W'));
            const auto initial = control->getCoord();
            const auto initialTab = stock->getSize();
            applySettingsWindowMinimumSize(window, mTabs);
            const MyGUI::IntSize viewport(1024, 768);
            window->setPosition((viewport.width - window->getWidth()) / 2, (viewport.height - window->getHeight()) / 2);
            EXPECT_EQ(window->getWidth(), 640);
            EXPECT_GE(window->getHeight(), 550);
            EXPECT_EQ(window->getLeft() * 2 + window->getWidth(), viewport.width);
            EXPECT_EQ(window->getTop() * 2 + window->getHeight(), viewport.height);
            EXPECT_EQ(control->getParent(), stock);
            EXPECT_EQ(control->getWidth(), initial.width + stock->getWidth() - initialTab.width);
            // No manual categories.layout(): MyGUI alignment must propagate a
            // programmatic resize, which does not emit eventWindowChangeCoord.
            EXPECT_EQ(controls.mView->getParent()->getWidth(), customTab()->getWidth());
            window->setSize(900, 650);
            EXPECT_EQ(controls.mView->getParent()->getWidth(), customTab()->getWidth());
            EXPECT_EQ(controls.mView->getWidth(), customTab()->getWidth());
        }

#ifndef OPENMW_CONTROLS_STANDALONE
        TEST_F(SettingsCategoriesTest, ProductionLayoutPreservesStockHierarchyAndScriptsViewport)
        {
            auto& factories = MyGUI::FactoryManager::getInstance();
            factories.registerFactory<Gui::HBox>("Widget");
            factories.registerFactory<Gui::VBox>("Widget");
            factories.registerFactory<Gui::Spacer>("Widget");
            factories.registerFactory<Gui::AutoSizedButton>("Widget");
            factories.registerFactory<Gui::AutoSizedTextBox>("Widget");
            LuaUi::registerAllWidgets();
            auto roots = MyGUI::LayoutManager::getInstance().loadLayout("openmw_settings_window.layout", "StockTest");
            ASSERT_EQ(roots.size(), 1u);
            auto* window = roots.front()->castType<MyGUI::Window>();
            mTabs = window->findWidget("StockTestSettingsTab")->castType<MyGUI::TabControl>();
            std::vector<MyGUI::TabItem*> stock;
            struct Original
            {
                MyGUI::Widget* widget;
                MyGUI::Widget* parent;
                MyGUI::IntCoord coord;
                MyGUI::Align align;
            };
            std::vector<Original> originals;
            const auto snapshot = [&](const auto& self, MyGUI::Widget* widget) -> void {
                originals.push_back({ widget, widget->getParent(), widget->getCoord(), widget->getAlign() });
                for (size_t i = 0; i < widget->getChildCount(); ++i)
                    self(self, widget->getChildAt(i));
            };
            for (size_t i = 0; i < mTabs->getItemCount(); ++i)
            {
                auto* tab = mTabs->getItemAt(i);
                if (tab->getUserString("CustomSettingsPage").empty())
                {
                    stock.push_back(tab);
                    snapshot(snapshot, tab);
                }
            }
            ASSERT_EQ(stock.size(), 6u);
            auto messages = createMessagePositionControls(customPage("Messages"), "StockTestMessages");
            auto icons = createMapIconControls(customPage("MapIcons"), "StockTestIcons");
            auto map = createSmartMapControls(customPage("SmartMap"), "StockTestMap");
            auto tooltips = createTooltipControls(customPage("Tooltips"), "StockTestTooltips");
            customPage("Dialogue")->createWidget<MyGUI::Widget>("", { 0, 0, 100, 100 }, MyGUI::Align::Stretch);
            SettingsCategories categories(mTabs);
            ASSERT_EQ(mTabs->getItemCount(), 7u);
            for (size_t i = 0; i < stock.size(); ++i)
                EXPECT_EQ(mTabs->getItemAt(i), stock[i]);
            for (const auto& original : originals)
            {
                SCOPED_TRACE(original.widget->getName());
                EXPECT_EQ(original.widget->getParent(), original.parent);
                EXPECT_EQ(original.widget->getCoord(), original.coord);
                EXPECT_EQ(original.widget->getAlign(), original.align);
            }
            ASSERT_EQ(selector(6)->getItemCount(), 7u);
            auto* scriptBox = window->findWidget("StockTestScriptBox");
            auto* scriptView = window->findWidget("StockTestScriptView")->castType<MyGUI::ScrollView>();
            auto* scriptFilter = window->findWidget("StockTestScriptFilter")->castType<MyGUI::EditBox>();
            auto* scripts = ancestor<MyGUI::TabItem>(scriptBox);
            EXPECT_EQ(scripts, stock[4]);
            scriptFilter->setCaption("Preserved filter");
            ASSERT_EQ(scriptFilter->getCaption(), "Preserved filter");
            applySettingsWindowMinimumSize(window, mTabs);
            window->setPosition((1024 - window->getWidth()) / 2, (768 - window->getHeight()) / 2);
            categories.layout();
            for (const MyGUI::IntSize size : { MyGUI::IntSize(640, 550), MyGUI::IntSize(900, 650) })
            {
                window->setSize(size);
                categories.layout();
                layoutMessagePositionControls(messages);
                layoutMapIconControls(icons);
                layoutSmartMapControls(map);
                layoutTooltipControls(tooltips);
                EXPECT_GT(scriptBox->getWidth(), 200);
                EXPECT_GT(scriptView->getWidth(), 200);
                EXPECT_GT(scriptView->getHeight(), 300);
                EXPECT_EQ(scriptFilter->getCaption(), "Preserved filter");
                EXPECT_EQ(ancestor<MyGUI::TabItem>(scriptBox), scripts);
                EXPECT_EQ(map.mView->getParent()->getWidth(), customTab()->getWidth());
            }
        }
#endif

        TEST_F(SettingsCategoriesTest, CategorySelectionSurvivesResizeAndReleasesHiddenInputFocus)
        {
            createTabs();
            page("Preferences");
            auto* journal = customPage("Journal");
            auto* field
                = journal->createWidget<MyGUI::EditBox>("MW_TextEdit", { 4, 4, 200, 28 }, MyGUI::Align::Default);
            customPage("Tooltips")->createWidget<MyGUI::TextBox>("SandText", { 4, 4, 200, 28 }, MyGUI::Align::Default);
            SettingsCategories categories(mTabs);
            mTabs->setIndexSelected(1);
            auto* pick = selector(1);
            ASSERT_EQ(pick->getItemCount(), 2u);
            MyGUI::InputManager::getInstance().setKeyFocusWidget(field);
            pick->setIndexSelected(1);
            pick->eventComboChangePosition(pick, 1);
            EXPECT_EQ(MyGUI::InputManager::getInstance().getKeyFocusWidget(), pick);
            mTabs->setSize(700, 400);
            categories.layout();
            mTabs->setIndexSelected(0);
            mTabs->setIndexSelected(1);
            categories.layout();
            EXPECT_EQ(pick->getIndexSelected(), 1u);
            EXPECT_FALSE(ancestor<MyGUI::ScrollView>(field)->getParent()->getVisible());
        }

        TEST_F(SettingsCategoriesTest, WrappedSectionsScrollOverBodyLabelsButtonsAndRevealFocusedControls)
        {
            createTabs();
            page("Preferences");
            auto* journal = customPage("Journal");
            auto* label = journal->createWidget<MyGUI::TextBox>("SandText", { 4, 4, 250, 28 }, MyGUI::Align::Default);
            auto* button = journal->createWidget<MyGUI::Button>("MW_Button", { 4, 50, 250, 28 }, MyGUI::Align::Default);
            auto* last
                = journal->createWidget<MyGUI::ComboBox>("MW_ComboBox", { 4, 1000, 250, 28 }, MyGUI::Align::Default);
            last->addItem("Original");
            last->setIndexSelected(0);
            SettingsCategories categories(mTabs);
            mTabs->setIndexSelected(1);
            mTabs->setSize(640, 300);
            categories.layout();
            auto* view = ancestor<MyGUI::ScrollView>(last);
            ASSERT_NE(view, nullptr);
            auto* content = last->getParent();
            for (auto* target : std::array<MyGUI::Widget*, 3>{ content, label, button })
            {
                view->setViewOffset({ 0, 0 });
                target->_riseMouseWheel(-120);
                EXPECT_LT(view->getViewOffset().top, 0);
            }
            view->setViewOffset({ 0, 0 });
            MyGUI::InputManager::getInstance().setKeyFocusWidget(last);
            EXPECT_LT(view->getViewOffset().top, 0);
            const auto relative
                = last->getAbsolutePosition() - view->getClientWidget()->getAbsolutePosition() + view->getViewOffset();
            EXPECT_GE(relative.top, 0);
            EXPECT_LE(relative.top + last->getHeight(), view->getViewCoord().height);
            EXPECT_EQ(last->getIndexSelected(), 0u);
            MyGUI::InputManager::getInstance().setKeyFocusWidget(button);
            const auto first = button->getAbsolutePosition() - view->getClientWidget()->getAbsolutePosition()
                + view->getViewOffset();
            EXPECT_GE(first.top, 0);
            EXPECT_LE(first.top + button->getHeight(), view->getViewCoord().height);
        }
    }
}
