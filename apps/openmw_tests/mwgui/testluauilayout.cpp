#include <components/lua_ui/content.hpp>
#include <components/lua_ui/element.hpp>
#include <components/lua_ui/flex.hpp>
#include <components/lua_ui/image.hpp>
#include <components/lua_ui/properties.hpp>
#include <components/lua_ui/resources.hpp>
#include <components/lua_ui/util.hpp>
#include <components/lua_ui/widget.hpp>
#include <components/misc/combinedperformance.hpp>

#include <MyGUI_DataFileStream.h>
#include <MyGUI_DataManager.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_FactoryManager.h>
#include <MyGUI_FontManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LayerManager.h>
#include <MyGUI_LogManager.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ResourceManager.h>
#include <MyGUI_TextBox.h>

#include <gtest/gtest.h>
#include <sol/sol.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace LuaUi
{
    namespace
    {
        void registerTextureResources(sol::state& lua, ResourceManager& resources)
        {
            lua.new_usertype<TextureResource>("TextureResource", sol::no_constructor);
            lua["texture"] = [&resources](const std::string& path) {
                return resources.registerTexture({ VFS::Path::Normalized(path), {}, {} });
            };
        }

        // Only the platform's file/render/font services are substitutes. The
        // actual MyGUI managers, widget factories, OpenMW skins, text wrapping,
        // child alignment and scroll canvas all run in this fixture.
        class LayoutDataManager final : public MyGUI::DataManager
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
            const std::filesystem::path mRoot = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files/data/mygui";
            const MyGUI::VectorString mNames;
        };

        class LayoutTexture final : public MyGUI::ITexture
        {
        public:
            explicit LayoutTexture(std::string name)
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

        class LayoutVertexBuffer final : public MyGUI::IVertexBuffer
        {
        public:
            void setVertexCount(size_t count) override { mVertices.resize(count); }
            size_t getVertexCount() const override { return mVertices.size(); }
            MyGUI::Vertex* lock() override { return mVertices.data(); }
            void unlock() override {}

        private:
            std::vector<MyGUI::Vertex> mVertices;
        };

        class LayoutRenderManager final : public MyGUI::RenderManager
        {
        public:
            MyGUI::IVertexBuffer* createVertexBuffer() override { return new LayoutVertexBuffer; }
            void destroyVertexBuffer(MyGUI::IVertexBuffer* buffer) override { delete buffer; }
            MyGUI::ITexture* createTexture(const std::string& name) override
            {
                auto& texture = mTextures[name];
                if (!texture)
                    texture = std::make_unique<LayoutTexture>(name);
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
            std::map<std::string, std::unique_ptr<LayoutTexture>> mTextures;
        };

        class LayoutFont final : public MyGUI::IFont
        {
        public:
            explicit LayoutFont(MyGUI::ITexture* texture)
                : mTexture(texture)
            {
                setResourceName("LuaUiLayoutFont");
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

        class LuaUiLayoutTest : public testing::Test
        {
        protected:
            MyGUI::LogManager mLog;
            LayoutDataManager mData;
            LayoutRenderManager mRenderer;
            MyGUI::Gui mGui;
            sol::state mLua;
            ResourceManager mResources;
            bool mInitialised = false;
            std::string mSnapshots;
            std::vector<std::shared_ptr<Element>> mElements;

            static WidgetExtension* find(WidgetExtension* root, std::string_view name)
            {
                if (root->getLayout().get_or("name", std::string()) == name)
                    return root;
                for (auto* child : root->templateChildren())
                    if (auto* result = find(child, name))
                        return result;
                for (auto* child : root->children())
                    if (auto* result = find(child, name))
                        return result;
                return nullptr;
            }

            static WidgetExtension* require(const std::shared_ptr<Element>& root, std::string_view name)
            {
                auto* result = find(root->mRoot, name);
                if (!result)
                    throw std::runtime_error("Missing test widget: " + std::string(name));
                return result;
            }

            static void refresh(const std::shared_ptr<Element>& element)
            {
                if (element->mState == Element::New)
                    element->create();
                else
                {
                    element->mState = Element::Update;
                    element->update();
                }
            }

            static void snapshotWidget(std::ostringstream& out, MyGUI::Widget* widget, const std::string& path)
            {
                const auto coord = widget->getCoord();
                const auto absolute = widget->getAbsolutePosition();
                out << path << ':' << widget->getTypeName() << ':' << coord.left << ',' << coord.top << ','
                    << coord.width << ',' << coord.height << ':' << absolute.left << ',' << absolute.top << ':'
                    << widget->getVisible() << ',' << widget->getInheritedVisible() << ','
                    << widget->getNeedMouseFocus();
                if (auto* text = dynamic_cast<MyGUI::TextBox*>(widget))
                    out << ':' << text->getCaption().asUTF8();
                if (auto* edit = dynamic_cast<MyGUI::EditBox*>(widget))
                    out << ":edit=" << edit->getTextCursor() << ',' << edit->getTextSelectionStart() << ','
                        << edit->getTextSelectionEnd() << ',' << edit->getHScrollPosition() << ','
                        << edit->getVScrollPosition() << ',' << edit->getHScrollRange() << ','
                        << edit->getVScrollRange();
                if (auto* image = dynamic_cast<LuaImage*>(widget))
                    out << ":image=" << image->_getTextureName() << ":alpha=" << image->getAlpha()
                        << ",inherit=" << image->getInheritsAlpha();
                out << '\n';
                for (size_t i = 0; i < widget->getChildCount(); ++i)
                    snapshotWidget(out, widget->getChildAt(i), path + '/' + std::to_string(i));
            }

            void snapshot(const std::string& label)
            {
                std::ostringstream out;
                out << '[' << label << "]\n";
                const sol::table roots = mLua["roots"];
                for (size_t i = 1; i <= roots.size(); ++i)
                {
                    auto root = roots.get<std::shared_ptr<Element>>(i);
                    snapshotWidget(out, root->mRoot->widget(), std::to_string(i));
                }
                mSnapshots += out.str();
            }

            void SetUp() override
            {
                mGui.initialise("");
                mInitialised = true;
                auto& resources = MyGUI::ResourceManager::getInstance();
                resources.addResource(new LayoutFont(mRenderer.getTexture("LayoutFontTexture")));
                MyGUI::FontManager::getInstance().setDefaultFont("LuaUiLayoutFont");
                MyGUI::LayerManager::getInstance().createLayerAt("LuaUiTest", "OverlappedLayer", 0);
                registerAllWidgets();
                ASSERT_TRUE(resources.load("openmw_lua.xml"));
                mLua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::math);
                mLua["vec2"] = [](float x, float y) { return osg::Vec2f(x, y); };
                mLua["vec4"] = [](float x, float y, float z, float w) { return osg::Vec4f(x, y, z, w); };
                mLua["colour"] = [](float r, float g, float b, float a) { return Misc::Color(r, g, b, a); };
                registerTextureResources(mLua, mResources);
                const auto contentPath
                    = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "components/lua_ui/content.lua";
                sol::table contentModule = mLua.safe_script_file(contentPath.string());
                mLua["content"] = contentModule["new"];
                auto elementType = mLua.new_usertype<Element>("UiElement", sol::no_constructor);
                elementType["layout"] = sol::property([](Element& element) { return element.mLayout; });
                elementType["name"] = sol::property(
                    [](Element& element) { return element.mLayout.as<sol::table>().get_or("name", std::string()); });
                mLua["element"] = [this](sol::table layout) {
                    sol::table options = mLua.create_table_with("noWarnUnused", true);
                    auto element = Element::make(layout, false, options);
                    mElements.push_back(element);
                    return element;
                };
                mLua["refresh"] = &refresh;
                mLua["resizeRoot"] = [](const std::shared_ptr<Element>& root, int width, int height) {
                    root->mRoot->forceSize({ width, height });
                    root->mRoot->updateCoord();
                };
                mLua["rect"] = [](const std::shared_ptr<Element>& root, const std::string& name) {
                    const auto coord = require(root, name)->widget()->getCoord();
                    return std::make_tuple(coord.left, coord.top, coord.width, coord.height);
                };
                mLua["sameWidget"]
                    = [](const std::shared_ptr<Element>& root, const std::string& name,
                          const std::shared_ptr<Element>& child) { return require(root, name) == child->mRoot; };
                mLua["snapshot"] = [this](const std::string& label) { snapshot(label); };
            }

            void TearDown() override
            {
                collectLayoutUpdateStatistics(false);
                // Full snapshots are compared between the diagnostic legacy
                // path and the normal path by the CI differential check.
                const char* legacy = std::getenv("OPENMW_LUA_UI_LEGACY_LAYOUT");
                RecordProperty(
                    "layout_mode", legacy != nullptr && std::string_view(legacy) == "1" ? "legacy" : "optimized");
                RecordProperty("native_update_mode", useNativeLayoutReuse() ? "reuse" : "legacy");
                RecordProperty("combined_performance_mode", Misc::useCombinedPerformance() ? "enabled" : "disabled");
                RecordProperty("layout_snapshots", mSnapshots);
                clearGameInterface();
                mElements.clear();
                if (mInitialised)
                    mGui.shutdown();
            }

            void run(const char* script) { ASSERT_NO_THROW(mLua.safe_script(script)); }

            void expectTemplateOrder(WidgetExtension* root)
            {
                const auto& children = root->templateChildren();
                ASSERT_EQ(root->widget()->getChildCount(), children.size() + 1);
                for (size_t i = 0; i < children.size(); ++i)
                {
                    EXPECT_EQ(root->widget()->getChildAt(i), children[i]->widget());
                    EXPECT_EQ(children[i]->getParent(), root);
                    EXPECT_EQ(children[i]->widget()->getWidgetStyle(), MyGUI::WidgetStyle::Child);
                    EXPECT_EQ(children[i]->widget()->getLayer(), nullptr);
                    EXPECT_EQ(children[i]->widget()->getLayerNode(), root->widget()->getLayerNode());
                }
                EXPECT_NE(root->widget()->getChildAt(children.size()), nullptr);
                EXPECT_EQ(root->widget()->getChildAt(children.size())->getWidgetStyle(), MyGUI::WidgetStyle::Child);
                EXPECT_EQ(root->widget()->getChildAt(children.size())->getLayerNode(), root->widget()->getLayerNode());
            }
        };

        TEST_F(LuaUiLayoutTest, StableTemplateHierarchyAndSiblingOrderRepair)
        {
            run(R"(
                root = element {
                    name='root', layer='LuaUiTest', props={size=vec2(180,90)},
                    template={content=content {
                        {name='background', props={size=vec2(180,90)}},
                        {name='border', props={size=vec2(178,88), position=vec2(1,1)}}
                    }},
                    content=content {{name='label', type='LuaText', props={text='A steady HUD', textSize=16}}}
                }
                roots={root}; refresh(root); snapshot('created')
                for i=1,4 do refresh(root); snapshot('unchanged-'..i) end
            )");
            const auto root = mLua["root"].get<std::shared_ptr<Element>>();
            auto* first = root->mRoot->templateChildren().front();
            auto* second = root->mRoot->templateChildren().back();
            expectTemplateOrder(root->mRoot);
            // Native widgets can reorder the actual hierarchy without changing
            // Lua's child vector. The fast path must notice and repair it.
            first->widget()->detachFromWidget();
            first->widget()->attachToWidget(root->mRoot->widget());
            EXPECT_EQ(root->mRoot->widget()->getChildAt(0), second->widget());
            refresh(root);
            expectTemplateOrder(root->mRoot);
            snapshot("repaired-order");
            // A moved template widget must likewise be restored, even though
            // its logical metadata still names the old parent.
            second->widget()->detachFromWidget();
            second->widget()->attachToWidget(first->widget());
            refresh(root);
            expectTemplateOrder(root->mRoot);
            snapshot("repaired-parent");
            // Attachment also restores Child style and inherited layer
            // membership. Equal parent pointers alone are not sufficient.
            second->widget()->setWidgetStyle(MyGUI::WidgetStyle::Overlapped);
            refresh(root);
            expectTemplateOrder(root->mRoot);
            snapshot("repaired-style");
            auto* wrapper = root->mRoot->widget()->getChildAt(root->mRoot->templateChildren().size());
            wrapper->setWidgetStyle(MyGUI::WidgetStyle::Overlapped);
            refresh(root);
            expectTemplateOrder(root->mRoot);
            snapshot("repaired-wrapper-style");
        }

        TEST_F(LuaUiLayoutTest, MutableTemplateSlotsAndReplacementKeepRetainedChild)
        {
            run(R"(
                kept=element {name='kept', type='LuaText', props={text='Retained', textSize=16}}
                first={name='first', external={slot=true}, props={size=vec2(70,40),position=vec2(5,5)}}
                second={name='second', props={size=vec2(90,40),position=vec2(85,5)}}
                root=element {
                    name='root', layer='LuaUiTest', props={size=vec2(190,60),padding=vec4(2,3,4,5)},
                    template={content=content {first,second}}, content=content {kept}
                }
                roots={root}; refresh(root); snapshot('first-slot')
                assert(sameWidget(root,'kept',kept))
                first.external.slot=false; second.external={slot=true}
                refresh(root); snapshot('second-slot')
                assert(sameWidget(root,'kept',kept))
                first.type='LuaContainer'
                first.content=content {{name='new-inner',props={size=vec2(20,30)}}}
                refresh(root); snapshot('template-type-change')
                assert(sameWidget(root,'kept',kept))
                root.layout.template.content['first']=nil
                refresh(root); snapshot('template-removal')
                assert(sameWidget(root,'kept',kept))
                root.layout.template=nil
                refresh(root); snapshot('no-template')
                assert(sameWidget(root,'kept',kept))
            )");
        }

        TEST_F(LuaUiLayoutTest, MovingRetainedElementRelayoutsThePreviousRootImmediately)
        {
            run(R"(
                moved=element {name='moved',props={size=vec2(30,10)}}
                first=element {
                    name='first',type='LuaFlex',layer='LuaUiTest',
                    props={size=vec2(100,40),autoSize=false,horizontal=true},
                    content=content {moved,{name='remaining',props={size=vec2(20,10)}}}
                }
                second=element {
                    name='second',type='LuaFlex',layer='LuaUiTest',
                    props={size=vec2(100,40),position=vec2(0,80),autoSize=false,horizontal=true},
                    content=content {}
                }
                roots={first,second}; refresh(first); refresh(second); snapshot('before-move')
                local x=rect(first,'remaining'); assert(x==30)
                second.layout.content:add(moved); refresh(second)
                local after=rect(first,'remaining'); assert(after==0)
                assert(sameWidget(second,'moved',moved)); snapshot('after-one-update')
            )");
        }

        TEST_F(LuaUiLayoutTest, NestedAutoSizeAndRelativeGrowRespondToASingleRootResize)
        {
            run(R"(
                root=element {
                    name='root',type='LuaFlex',layer='LuaUiTest',
                    props={size=vec2(100,100),autoSize=false},
                    content=content {{
                        name='outer',type='LuaFlex',
                        props={size=vec2(0,30),relativeSize=vec2(1,0),horizontal=true,autoSize=false},
                        content=content {
                            {name='inner',type='LuaFlex',external={grow=1},content=content {
                                {name='leaf',props={size=vec2(0,10),relativeSize=vec2(0.5,0)}}
                            }},
                            {name='sibling',props={size=vec2(10,10)}}
                        }
                    }}
                }
                roots={root}; refresh(root); snapshot('width-100')
                resizeRoot(root,50,100); snapshot('single-shrink')
                local _,_,innerWidth=rect(root,'inner')
                local _,_,leafWidth=rect(root,'leaf')
                local siblingX=rect(root,'sibling')
                assert(innerWidth==40 and leafWidth==20 and siblingX==40)
                resizeRoot(root,140,100); snapshot('single-grow')
                resizeRoot(root,50,100); snapshot('second-single-shrink')
                local siblingAgain=rect(root,'sibling'); assert(siblingAgain==40)
            )");
        }

        TEST_F(LuaUiLayoutTest, NestedWrappingTextPaddingVisibilityAndReorderStayConsistent)
        {
            run(R"(
                left={name='left',type='LuaFlex',props={horizontal=true,gap=3},content=content {
                    {name='a',type='LuaText',props={text='Alpha',textSize=16}},
                    {name='b',type='LuaText',props={text='Beta',textSize=16}}
                }}
                right={name='right',type='LuaContainer',content=content {
                    {name='relative',props={size=vec2(40,15),relativePosition=vec2(0.5,0.5),anchor=vec2(0.5,0.5)}},
                    {name='fixed',props={size=vec2(20,10),position=vec2(45,10)}}
                }}
                root=element {name='root',type='LuaFlex',layer='LuaUiTest',
                    props={size=vec2(180,100),autoSize=false,horizontal=true,wrap=true,gap=5,align=1,arrange=2,
                           padding=vec4(3,4,5,6)},
                    template={content=content {{name='background',props={relativeSize=vec2(1,1)}}}},
                    content=content {left,right,{name='third',props={size=vec2(45,20)},external={grow=1,stretch=0.6}}}
                }
                roots={root}; refresh(root); snapshot('initial')
                root.layout.props.size=vec2(90,80); refresh(root); snapshot('wrap-narrow')
                left.content[1].props.text='A longer caption'; left.content[2].props.textSize=22
                refresh(root); snapshot('text-change')
                root.layout.props.visible=false; refresh(root); snapshot('hidden')
                root.layout.props.visible=true; root.layout.props.size=vec2(250,140)
                root.layout.props.padding=vec4(8,2,1,9); refresh(root); snapshot('shown-wider')
                root.layout.content=content {right,left}; refresh(root); snapshot('reorder-remove')
                root.layout.content:insert(2,{name='inserted',type='LuaText',props={text='New',textSize=12}})
                refresh(root); snapshot('insert')
                root.layout.props.horizontal=false; root.layout.props.wrap=false
                refresh(root); snapshot('vertical')
            )");
        }

        TEST_F(LuaUiLayoutTest, RepeatedHudUpdatesBenchmark)
        {
            run(R"(
                local rows={}
                for i=1,24 do
                    rows[i]={name='row'..i,type='LuaFlex',props={horizontal=true,gap=4},
                        template={content=content {{props={relativeSize=vec2(1,1)}}}},
                        content=content {
                            {type='LuaText',props={text='Quest objective '..i,textSize=16}},
                            {type='LuaText',props={text='120 m',textSize=16}},
                            {props={size=vec2(12,12)}}
                        }
                    }
                end
                root=element {name='hud',type='LuaFlex',layer='LuaUiTest',props={gap=2},content=content(rows)}
                roots={root}; refresh(root); snapshot('before-updates')
            )");
            const auto root = mLua["root"].get<std::shared_ptr<Element>>();
            constexpr int iterations = 64;
            std::array<long long, 7> durations;
            for (auto& duration : durations)
            {
                const auto start = std::chrono::steady_clock::now();
                for (int i = 0; i < iterations; ++i)
                    refresh(root);
                duration
                    = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
                          .count();
            }
            std::sort(durations.begin(), durations.end());
            RecordProperty("median_update_ns", std::to_string(durations[durations.size() / 2] / iterations));
            RecordProperty("minimum_update_ns", std::to_string(durations.front() / iterations));
            RecordProperty("updates_per_batch", iterations);
            snapshot("after-updates");
            // Timings are evidence, not pass/fail thresholds. Correctness is
            // checked by full native hierarchy snapshots in both modes.
        }

        TEST_F(LuaUiLayoutTest, LayoutReuseTracksConstraintsAndMutatedRetainedExternalFields)
        {
            run(R"(
                left=element {name='retained-left',type='LuaFlex',
                    props={horizontal=true,autoSize=false,size=vec2(20,24)},external={grow=1},
                    content=content {{name='relative-leaf',props={relativeSize=vec2(.5,1)}}}}
                right=element {name='retained-right',props={size=vec2(20,24)},external={grow=1}}
                root=element {name='root',type='LuaFlex',layer='LuaUiTest',
                    props={horizontal=true,autoSize=false,size=vec2(200,60)},
                    content=content {left,right}}
                roots={root}; refresh(root); snapshot('initial-retained-growth')
                -- External fields are live layout inputs even when the child
                -- Element itself is not refreshed.
                left.layout.external.grow=3
                root.layout.props.size=vec2(140,60)
                refresh(root); snapshot('mutated-external-and-parent')
                local _,_,leftWidth=rect(root,'retained-left')
                local _,_,rightWidth=rect(root,'retained-right')
                assert(leftWidth==95 and rightWidth==45)
                local _,_,leafWidth=rect(root,'relative-leaf')
                assert(leafWidth==47)
                root.layout.props.visible=false; refresh(root); snapshot('retained-hidden')
                root.layout.props.visible=true; refresh(root); snapshot('retained-shown')
                left.layout.props.size=vec2(30,24); refresh(left)
                snapshot('child-action-updates-outer-root')
                root.layout.content=content {right,left}; refresh(root); snapshot('retained-reordered')
            )");
        }

        TEST_F(LuaUiLayoutTest, ReparentIntoGrowingSiblingUnderTheSameRoot)
        {
            run(R"(
                moving=element {name='moving',props={size=vec2(18,9)}}
                old=element {name='old',type='LuaFlex',content=content {moving}}
                dest=element {name='dest',type='LuaFlex',content=content {}}
                root=element {name='outer',type='LuaFlex',layer='LuaUiTest',
                    props={size=vec2(160,80),autoSize=false},content=content {dest,old}}
                roots={root}; refresh(root)
                -- attach() removes moving from old immediately. Both containers share outer,
                -- and dest still has an unfilled second slot at that point.
                dest.layout.content=content {moving,{name='tail',props={size=vec2(11,7)}}}
                refresh(dest); snapshot('after-moving-into-growing-sibling')
                assert(sameWidget(dest,'moving',moving))
            )");
            const auto dest = mLua["dest"].get<std::shared_ptr<Element>>();
            const auto old = mLua["old"].get<std::shared_ptr<Element>>();
            const auto moving = mLua["moving"].get<std::shared_ptr<Element>>();
            ASSERT_EQ(dest->mRoot->children().size(), 2u);
            EXPECT_TRUE(old->mRoot->children().empty());
            EXPECT_EQ(dest->mRoot->children().front(), moving->mRoot);
            EXPECT_EQ(moving->mRoot->getParent(), dest->mRoot);
            EXPECT_EQ(dest->mRoot->children().back(), require(dest, "tail"));
            // An ordinary update in the next action can still reuse a stable layout.
            collectLayoutUpdateStatistics(true);
            run("refresh(dest); snapshot('stable-after-reparent')");
            const auto stats = takeLayoutUpdateStatistics();
            if (useNativeLayoutReuse())
            {
                EXPECT_GT(stats.reused, 0u);
            }
        }

        TEST_F(LuaUiLayoutTest, LayoutReuseFallsBackForLiveMetatableReads)
        {
            run(R"(
                child=element {name='dynamic',props={size=vec2(20,10)},external={grow=1}}
                root=element {name='root',type='LuaFlex',layer='LuaUiTest',
                    props={horizontal=true,autoSize=false,size=vec2(120,30)},content=content {child}}
                roots={root}; refresh(root)
                externalReads=0
                setmetatable(child.layout.external,{__index=function(_,key)
                    externalReads=externalReads+1
                    if key=='grow' then return 1 end
                    if key=='stretch' then return 0 end
                end})
            )");
            collectLayoutUpdateStatistics(true);
            run("refresh(root); snapshot('dynamic-external-reads-'..externalReads)");
            const auto stats = takeLayoutUpdateStatistics();
            EXPECT_EQ(stats.reused, 0u);
            EXPECT_GT(stats.calculated, 0u);
            RecordProperty("fallback_layout_calculations", std::to_string(stats.calculated));
            RecordProperty("fallback_layout_reuses", std::to_string(stats.reused));
            collectLayoutUpdateStatistics(false);
            run(R"(
                -- A template lookup can execute Lua before its children are
                -- processed; this is another deliberately conservative fallback.
                local template={props={padding=vec4(1,2,3,4)}}
                templateReads=0
                root.layout.template=setmetatable({}, {__index=function(_,key)
                    templateReads=templateReads+1; return template[key]
                end})
                refresh(root); snapshot('dynamic-template-reads-'..templateReads)
                plain=element {name='plain',props={size=vec2(10,10)}}
                other=element {name='other',type='LuaFlex',layer='LuaUiTest',
                    props={horizontal=true,autoSize=false,size=vec2(150,40)},content=content {plain}}
                roots={root,other}; refresh(other)
                other.layout.content:add(child); externalReads=0
            )");
            collectLayoutUpdateStatistics(true);
            run(R"(
                refresh(other)
                -- Moving a plain child back must also preserve dynamic reads
                -- on siblings left in its previous parent.
                root.layout.template=nil; root.layout.content=content {plain}
                refresh(root); snapshot('moved-dynamic-external-reads-'..externalReads)
            )");
            const auto movedStats = takeLayoutUpdateStatistics();
            collectLayoutUpdateStatistics(false);
            EXPECT_EQ(movedStats.reused, 0u);
            EXPECT_GT(movedStats.calculated, 0u);
            RecordProperty("moved_layout_calculations", std::to_string(movedStats.calculated));
            RecordProperty("moved_layout_reuses", std::to_string(movedStats.reused));
        }

        TEST_F(LuaUiLayoutTest, RetainedTextEditorsKeepCaretSelectionAndScrollPasses)
        {
            run(R"(
                text='First wrapped line with more words\nSecond wrapped line with more words\nThird long line'
                readonly=element {name='readonly',type='LuaTextEdit',props={text=text,textSize=16,
                    readOnly=true,multiline=true,wordWrap=true,autoSize=true,relativeSize=vec2(1,0)}}
                editable=element {name='editable',type='LuaTextEdit',props={text=text,textSize=16,
                    multiline=true,size=vec2(0,25),relativeSize=vec2(1,0)}}
                root=element {name='root',type='LuaFlex',layer='LuaUiTest',
                    props={size=vec2(180,240),autoSize=false,gap=4},content=content {readonly,editable}}
                roots={root}; refresh(root); snapshot('editors-created')
            )");
            const auto root = mLua["root"].get<std::shared_ptr<Element>>();
            const auto nativeEdit = [&](const std::string& name) {
                MyGUI::Widget* wrapper = require(root, name)->widget();
                for (size_t i = 0; i < wrapper->getChildCount(); ++i)
                    if (auto* edit = dynamic_cast<MyGUI::EditBox*>(wrapper->getChildAt(i)))
                        return edit;
                throw std::runtime_error("Missing native editor: " + name);
            };
            auto* edit = nativeEdit("editable");
            edit->setTextCursor(42);
            edit->setTextSelection(7, 18);
            edit->setHScrollPosition(std::min<size_t>(24, edit->getHScrollRange()));
            edit->setVScrollPosition(std::min<size_t>(12, edit->getVScrollRange()));
            snapshot("editors-caret-and-scroll");
            collectLayoutUpdateStatistics(true);
            refresh(root);
            const auto stats = takeLayoutUpdateStatistics();
            collectLayoutUpdateStatistics(false);
            EXPECT_EQ(stats.reused, 0u);
            EXPECT_GT(stats.calculated, 0u);
            RecordProperty("fallback_layout_calculations", std::to_string(stats.calculated));
            RecordProperty("fallback_layout_reuses", std::to_string(stats.reused));
            snapshot("editors-retained-refresh");
            run(R"(
                root.layout.props.size=vec2(90,240); refresh(root); snapshot('editors-narrow')
                root.layout.props.size=vec2(220,240); refresh(root); snapshot('editors-wide')
                readonly.layout.props.textSize=20; refresh(readonly); snapshot('editor-font-change')
            )");
        }

        TEST_F(LuaUiLayoutTest, NestedRetainedHudSparseAndFullUpdatesBenchmark)
        {
            run(R"(
                rows={}; distances={}; arrows={}; labels={}
                for i=1,32 do
                    local label=element {name='label'..i,type='LuaText',
                        props={text='A retained quest objective '..i,textSize=16}}
                    local detail=element {name='detail'..i,type='LuaText',
                        props={text='A static secondary description',textSize=12}}
                    labels[i]=label
                    distances[i]={name='distance'..i,type='LuaText',props={text='120 m',textSize=16}}
                    arrows[i]={name='arrow'..i,type='LuaImage',
                        props={resource=texture('textures/layout_arrow.dds'),size=vec2(14,14)}}
                    rows[i]={name='row'..i,type='LuaFlex',props={horizontal=true,gap=4},
                        template={content=content {{name='background'..i,type='LuaImage',
                            props={resource=texture('textures/layout_row.dds'),relativeSize=vec2(1,1)}}}},
                        content=content {
                            {name='labels'..i,type='LuaFlex',props={gap=1},content=content {label,detail}},
                            {name='values'..i,type='LuaFlex',props={horizontal=true,gap=2},
                                content=content {distances[i],arrows[i]}}
                        }}
                end
                root=element {name='nested-hud',type='LuaFlex',layer='LuaUiTest',
                    props={gap=3},content=content(rows)}
                roots={root}; refresh(root); snapshot('nested-created')
                function mutateHud(kind,iteration)
                    if kind=='sparse' then
                        local i=iteration%32+1
                        distances[i].props.text=tostring(100+iteration%19)..' m'
                        arrows[i].props.alpha=.5+.05*(iteration%9)
                    elseif kind=='full' then
                        for i=1,32 do
                            distances[i].props.text=tostring(100+(iteration+i)%19)..' m'
                            arrows[i].props.alpha=.5+.05*((iteration+i)%9)
                        end
                    elseif kind=='retained' then
                        labels[iteration%32+1].layout.props.text='Changed retained objective '..iteration
                    end
                end
            )");
            const auto root = mLua["root"].get<std::shared_ptr<Element>>();
            const sol::protected_function mutate = mLua["mutateHud"];
            std::vector<std::shared_ptr<Element>> retained;
            const sol::table labels = mLua["labels"];
            for (size_t i = 1; i <= labels.size(); ++i)
                retained.push_back(labels.get<std::shared_ptr<Element>>(i));
            constexpr int iterations = 24;
            RecordProperty("hud_rows", 32);
            RecordProperty("timed_batches", 5);
            RecordProperty("updates_per_batch", iterations);
            RecordProperty("warmup_updates", 8);
            for (const std::string kind : { "unchanged", "sparse", "full", "retained" })
            {
                const auto update = [&](int i) { refresh(kind == "retained" ? retained[i % retained.size()] : root); };
                for (int i = 0; i < 8; ++i)
                {
                    ASSERT_TRUE(mutate(kind, i).valid());
                    update(i);
                }
                std::array<long long, 5> durations;
                for (auto& duration : durations)
                {
                    const auto start = std::chrono::steady_clock::now();
                    for (int i = 0; i < iterations; ++i)
                    {
                        ASSERT_TRUE(mutate(kind, i).valid());
                        update(i);
                    }
                    duration
                        = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
                              .count();
                }
                std::sort(durations.begin(), durations.end());
                RecordProperty(kind + "_median_update_ns", std::to_string(durations[2] / iterations));
                collectLayoutUpdateStatistics(true);
                update(0);
                const auto stats = takeLayoutUpdateStatistics();
                collectLayoutUpdateStatistics(false);
                RecordProperty(kind + "_layout_calculations", std::to_string(stats.calculated));
                RecordProperty(kind + "_layout_reuses", std::to_string(stats.reused));
                if (useNativeLayoutReuse())
                    EXPECT_GT(stats.reused, 0u);
                else
                    EXPECT_EQ(stats.reused, 0u);
                snapshot("nested-" + kind);
            }
        }

        TEST_F(LuaUiLayoutTest, ImageTemplatesInheritResourcesAcrossOverridesAndUpdates)
        {
            // MWUI boxes use this structure: the image instance supplies its
            // geometry, while a template supplies the border/background texture.
            run(R"(
                backgroundTemplate={type='LuaImage',props={
                    resource=texture('textures/layout_background.dds'),
                    color=colour(0,0,0,1),alpha=0.75}}
                borderTemplate={type='LuaImage',props={
                    resource=texture('textures/layout_border.dds'),tileH=true}}
                background={name='background',template=backgroundTemplate,
                            props={relativeSize=vec2(1,1)}}
                border={name='border',template=borderTemplate,
                        props={relativeSize=vec2(1,0),size=vec2(0,4)}}
                root=element {name='root',layer='LuaUiTest',props={size=vec2(180,90)},
                    template={content=content {background,border}},
                    content=content {
                        {name='icon',type='LuaImage',props={size=vec2(16,16),
                            resource=texture('textures/layout_icon.dds')}},
                        {name='label',type='LuaText',props={text='Visible content',textSize=16}}
                    }}
                roots={root}; refresh(root); snapshot('image-inherited')
            )");
            const auto root = mLua["root"].get<std::shared_ptr<Element>>();
            const auto checkImage = [&](std::string_view name, const char* texture, MyGUI::IntSize size, float alpha) {
                auto* image = dynamic_cast<LuaImage*>(require(root, name)->widget());
                ASSERT_NE(image, nullptr);
                EXPECT_EQ(image->_getTextureName(), texture);
                EXPECT_EQ(image->getSize(), size);
                EXPECT_FLOAT_EQ(image->getAlpha(), alpha);
                EXPECT_TRUE(image->getInheritedVisible());
            };
            const auto check = [&](const char* backgroundTexture, const char* borderTexture, MyGUI::IntSize size) {
                checkImage("background", backgroundTexture, size, 0.75f);
                checkImage("border", borderTexture, { size.width, 4 }, 1.f);
                checkImage("icon", "textures/layout_icon.dds", { 16, 16 }, 1.f);
                expectTemplateOrder(root->mRoot);
            };
            check("textures/layout_background.dds", "textures/layout_border.dds", { 180, 90 });

            run(R"(
                background.props.resource=texture('textures/layout_override.dds')
                border.props.resource=texture('textures/layout_override.dds')
                refresh(root); snapshot('image-overridden')
            )");
            check("textures/layout_override.dds", "textures/layout_override.dds", { 180, 90 });

            run(R"(
                background.props.resource=nil; border.props.resource=nil
                refresh(root); snapshot('image-override-removed')
            )");
            check("textures/layout_background.dds", "textures/layout_border.dds", { 180, 90 });

            run(R"(
                backgroundTemplate.props.resource=texture('textures/layout_background_updated.dds')
                borderTemplate.props.resource=texture('textures/layout_border_updated.dds')
                root.layout.props.size=vec2(220,110)
                refresh(root); snapshot('image-template-updated')
            )");
            check("textures/layout_background_updated.dds", "textures/layout_border_updated.dds", { 220, 110 });
            run("refresh(root); snapshot('image-unchanged')");
            check("textures/layout_background_updated.dds", "textures/layout_border_updated.dds", { 220, 110 });
        }

        class LuaUiPropertyTest : public testing::Test
        {
        protected:
            sol::state mLua;
            ResourceManager mResources;

            void SetUp() override
            {
                mLua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::math);
                mLua["vec2"] = [](float x, float y) { return osg::Vec2f(x, y); };
                mLua["vec4"] = [](float x, float y, float z, float w) { return osg::Vec4f(x, y, z, w); };
                mLua["colour"] = [](float r, float g, float b, float a) { return Misc::Color(r, g, b, a); };
                registerTextureResources(mLua, mResources);
            }

            void TearDown() override
            {
                const char* legacy = std::getenv("OPENMW_LUA_UI_LEGACY_LAYOUT");
                RecordProperty(
                    "layout_mode", legacy != nullptr && std::string_view(legacy) == "1" ? "legacy" : "optimized");
            }

            template <typename T>
            void compare(const sol::main_object& props, const sol::main_object& fallback, std::string_view field,
                const T& defaultValue)
            {
                const int top = lua_gettop(mLua);
                EXPECT_EQ(parseProperty(props, fallback, field, defaultValue),
                    parseProperty(sol::object(props), sol::object(fallback), field, defaultValue));
                EXPECT_EQ(lua_gettop(mLua), top);
                EXPECT_EQ(
                    parseExternal(props, field, defaultValue), parseExternal(sol::object(props), field, defaultValue));
                EXPECT_EQ(lua_gettop(mLua), top);
            }

            template <class Callable>
            static std::string error(Callable&& callable)
            {
                try
                {
                    callable();
                }
                catch (const std::exception& exception)
                {
                    return exception.what();
                }
                ADD_FAILURE() << "Expected a property lookup error";
                return {};
            }
        };

        TEST_F(LuaUiPropertyTest, PlainTablesPreserveTypesDefaultsAndImmediateMutation)
        {
            mLua.safe_script(R"(
                props={number=17, fraction=1.75, text='stored text', flag=false,
                       vector=vec2(12.75,-8.75), padding=vec4(1,2,3,4), color=colour(.1,.2,.3,.4), align=2}
                fallback={number=4, text='template text', flag=true}
            )");
            const sol::main_object props = mLua["props"];
            const sol::main_object fallback = mLua["fallback"];
            compare(props, fallback, "number", 0);
            compare(props, fallback, "fraction", 0.f);
            compare(props, fallback, "fraction", 0.0);
            compare(props, fallback, "text", std::string());
            compare(props, fallback, "flag", true);
            compare(props, fallback, "vector", MyGUI::IntPoint());
            compare(props, fallback, "vector", MyGUI::IntSize());
            compare(props, fallback, "vector", MyGUI::FloatPoint());
            compare(props, fallback, "vector", MyGUI::FloatSize());
            compare(props, fallback, "padding", osg::Vec4f());
            compare(props, fallback, "color", MyGUI::Colour());
            compare(props, fallback, "align", Alignment::Start);
            compare(props, fallback, "missing", 9);
            EXPECT_EQ(parseProperty(props, fallback, "vector", MyGUI::IntSize()), MyGUI::IntSize(12, -8));
            EXPECT_FALSE(parseProperty(props, fallback, "flag", true));
            mLua.safe_script("props.number=29; props.text=nil; fallback.text='changed template'");
            EXPECT_EQ(parseProperty(props, fallback, "number", 0), 29);
            EXPECT_EQ(parseProperty(props, fallback, "text", std::string()), "changed template");
            mLua.safe_script("props.number=nil; fallback.number=nil");
            EXPECT_EQ(parseProperty(props, fallback, "number", 81), 81);
        }

        TEST_F(LuaUiPropertyTest, TextureResourcesPreserveInheritanceDefaultsAndErrors)
        {
            mLua.safe_script(R"(
                templateResource=texture('textures/property_template.dds')
                overrideResource=texture('textures/property_override.dds')
                defaultResource=texture('textures/property_default.dds')
                props={}; fallback={resource=templateResource}
            )");
            const auto templateResource = mLua["templateResource"].get<std::shared_ptr<TextureResource>>();
            const auto overrideResource = mLua["overrideResource"].get<std::shared_ptr<TextureResource>>();
            const auto defaultResource = mLua["defaultResource"].get<std::shared_ptr<TextureResource>>();
            const sol::main_object props = mLua["props"];
            const sol::main_object fallback = mLua["fallback"];
            lua_pushliteral(mLua, "texture stack sentinel");
            const int top = lua_gettop(mLua);
            const auto check = [&](TextureResource* expected) {
                compare(props, fallback, "resource", defaultResource.get());
                EXPECT_EQ(parseProperty(props, fallback, "resource", defaultResource.get()), expected);
                EXPECT_EQ(lua_gettop(mLua), top);
                EXPECT_STREQ(lua_tostring(mLua, -1), "texture stack sentinel");
            };
            check(templateResource.get());
            mLua.safe_script("props.resource=overrideResource");
            check(overrideResource.get());
            mLua.safe_script("props.resource=nil");
            check(templateResource.get());
            mLua.safe_script("fallback.resource=nil");
            check(defaultResource.get());
            const sol::main_object noTemplate;
            compare(props, noTemplate, "resource", defaultResource.get());
            EXPECT_EQ(parseProperty(props, noTemplate, "resource", defaultResource.get()), defaultResource.get());
            EXPECT_EQ(parseExternal(props, "resource", defaultResource.get()), defaultResource.get());

            mLua.safe_script("fallback.resource=templateResource");
            const sol::main_object nil = sol::make_object(mLua, sol::nil);
            const sol::main_object scalar = sol::make_object(mLua, 7);
            for (const sol::main_object& source : { sol::main_object(), nil, scalar })
            {
                compare(source, fallback, "resource", defaultResource.get());
                EXPECT_EQ(parseProperty(source, fallback, "resource", defaultResource.get()), templateResource.get());
                EXPECT_EQ(parseExternal(source, "resource", defaultResource.get()), defaultResource.get());
            }

            const auto checkError = [&](std::string_view prefix) {
                const auto actual = error([&] { parseProperty(props, fallback, "resource", defaultResource.get()); });
                EXPECT_EQ(actual, error([&] {
                    parseProperty(sol::object(props), sol::object(fallback), "resource", defaultResource.get());
                }));
                EXPECT_EQ(actual.find(prefix), 0u);
                EXPECT_EQ(lua_gettop(mLua), top);
                EXPECT_STREQ(lua_tostring(mLua, -1), "texture stack sentinel");
            };
            mLua.safe_script("props.resource='invalid property'; fallback.resource='invalid template'");
            checkError("Property \"resource\"");
            EXPECT_EQ(error([&] { parseExternal(props, "resource", defaultResource.get()); }),
                error([&] { parseExternal(sol::object(props), "resource", defaultResource.get()); }));
            mLua.safe_script("props.resource=overrideResource");
            checkError("Template property \"resource\"");
            lua_pop(mLua, 1);
        }

        TEST_F(LuaUiPropertyTest, MissingFieldsPreserveLegacyNilTypeSemantics)
        {
            const sol::main_object props = mLua.create_table();
            const sol::main_object fallback = mLua.create_table_with("value", 73);
            const sol::main_object empty = mLua.create_table();
            const sol::object objectDefault = sol::make_object(mLua, 91);
            const sol::optional<int> optionalDefault = 91;
            lua_pushliteral(mLua, "nil stack sentinel");
            const int top = lua_gettop(mLua);
            // Like pointer types, stack objects accept nil for these types even
            // though the legacy registry-owned nil is not a supplied value.
            compare(props, fallback, "value", objectDefault);
            compare(props, empty, "value", objectDefault);
            EXPECT_EQ(parseProperty(props, fallback, "value", objectDefault).as<int>(), 73);
            EXPECT_EQ(parseProperty(props, empty, "value", objectDefault).as<int>(), 91);
            EXPECT_EQ(parseExternal(props, "value", objectDefault).as<int>(), 91);
            compare(props, fallback, "value", optionalDefault);
            compare(props, empty, "value", optionalDefault);
            EXPECT_EQ(parseProperty(props, fallback, "value", optionalDefault), sol::optional<int>(73));
            EXPECT_EQ(parseProperty(props, empty, "value", optionalDefault), optionalDefault);
            EXPECT_EQ(parseExternal(props, "value", optionalDefault), optionalDefault);

            // A blanket "nil is absent" check would also change the sentinel
            // types that the legacy reader does accept as supplied nil values.
            props.push(mLua);
            const sol::stack_object owner(mLua, lua_gettop(mLua));
            EXPECT_TRUE(parseValue<std::nullptr_t>(sol::object(props), "value", "Property").has_value());
            EXPECT_TRUE(Detail::parseStoredValue<std::nullptr_t>(owner, "value", "Property").has_value());
            EXPECT_TRUE(parseValue<sol::lua_nil_t>(sol::object(props), "value", "Property").has_value());
            EXPECT_TRUE(Detail::parseStoredValue<sol::lua_nil_t>(owner, "value", "Property").has_value());
            EXPECT_EQ(lua_gettop(mLua), top + 1);
            lua_pop(mLua, 1);
            EXPECT_STREQ(lua_tostring(mLua, -1), "nil stack sentinel");
            lua_pop(mLua, 1);
        }

        TEST_F(LuaUiPropertyTest, InvalidValuesAndOverriddenTemplateErrorsRestoreTheStack)
        {
            mLua.safe_script("props={value=31}; fallback={value='invalid template'}");
            const sol::main_object props = mLua["props"];
            const sol::main_object fallback = mLua["fallback"];
            lua_pushliteral(mLua, "stack sentinel");
            const int top = lua_gettop(mLua);
            const auto check = [&] {
                EXPECT_EQ(error([&] { parseProperty(props, fallback, "value", 0); }),
                    error([&] { parseProperty(sol::object(props), sol::object(fallback), "value", 0); }));
                EXPECT_EQ(lua_gettop(mLua), top);
                EXPECT_STREQ(lua_tostring(mLua, -1), "stack sentinel");
                EXPECT_EQ(error([&] { parseExternal(fallback, "value", 0); }),
                    error([&] { parseExternal(sol::object(fallback), "value", 0); }));
                EXPECT_EQ(lua_gettop(mLua), top);
            };
            check(); // Template validation is required even though props overrides it.
            mLua.safe_script("fallback.value={}; props.value=false");
            check();
            mLua.safe_script("fallback.value=vec2(1,2); props.value=function() end");
            check();
            mLua.safe_script("fallback.value=setmetatable({}, {__tostring=function() error('format failure') end})");
            EXPECT_EQ(error([&] { parseExternal(fallback, "value", 0); }),
                error([&] { parseExternal(sol::object(fallback), "value", 0); }));
            EXPECT_EQ(lua_gettop(mLua), top);
            lua_pop(mLua, 1);
        }

        TEST_F(LuaUiPropertyTest, MetatableLookupsPreserveOrderSideEffectsAndProtectedErrors)
        {
            mLua.safe_script(R"(
                calls={}; nextValue=0
                props=setmetatable({value=900}, {__index=function(_,key)
                    calls[#calls+1]='property:'..key; nextValue=nextValue+1; return nextValue
                end})
                fallback=setmetatable({}, {__index=function(_,key)
                    calls[#calls+1]='template:'..key; return 99
                end})
            )");
            const sol::main_object props = mLua["props"];
            const sol::main_object fallback = mLua["fallback"];
            const int top = lua_gettop(mLua);
            // The established safeGet invokes functional __index even for an
            // existing raw field. Do not replace it with ordinary lua_gettable.
            EXPECT_EQ(parseProperty(props, fallback, "value", 0), 1);
            EXPECT_EQ(parseProperty(props, fallback, "value", 0), 2);
            EXPECT_EQ(parseExternal(props, "value", 0), 3);
            mLua.safe_script(
                "assert(table.concat(calls,',')=='property:value,template:value,property:value,template:value,property:"
                "value')");
            mLua.safe_script("calls={}; nextValue=0");
            EXPECT_EQ(parseProperty(sol::object(props), sol::object(fallback), "value", 0), 1);
            EXPECT_EQ(parseProperty(sol::object(props), sol::object(fallback), "value", 0), 2);
            EXPECT_EQ(parseExternal(sol::object(props), "value", 0), 3);
            mLua.safe_script(
                "assert(table.concat(calls,',')=='property:value,template:value,property:value,template:value,property:"
                "value')");
            mLua.safe_script("getmetatable(fallback).__index=function() error('lookup failure') end");
            EXPECT_EQ(error([&] { parseProperty(props, fallback, "value", 0); }),
                error([&] { parseProperty(sol::object(props), sol::object(fallback), "value", 0); }));
            EXPECT_EQ(lua_gettop(mLua), top);
            mLua.safe_script("setmetatable(props,{__index={value=123}}); props.value=45; setmetatable(fallback,{})");
            compare(props, fallback, "value", 0);
            mLua.safe_script("props.value=nil");
            compare(props, fallback, "value", 0);
        }

        TEST_F(LuaUiPropertyTest, ReturnedOwningValuesSurviveFieldReplacementAndCollection)
        {
            mLua.safe_script("props={text=string.rep('a',128), child={value=42}, value={marker=27}}; fallback={}");
            const sol::main_object props = mLua["props"];
            const sol::main_object fallback = mLua["fallback"];
            const std::string text = parseProperty(props, fallback, "text", std::string());
            const sol::table child = parseProperty(props, fallback, "child", mLua.create_table());
            const sol::object object = parseProperty(props, fallback, "value", sol::make_object(mLua, sol::nil));
            mLua.safe_script("props.text=nil; props.child=nil; props.value=nil; collectgarbage('collect')");
            EXPECT_EQ(text, std::string(128, 'a'));
            EXPECT_EQ(child.get<int>("value"), 42);
            EXPECT_EQ(object.as<sol::table>().get<int>("marker"), 27);
            const sol::main_object absent;
            const sol::main_object scalar = sol::make_object(mLua, 7);
            const sol::main_object nil = sol::make_object(mLua, sol::nil);
            compare(absent, fallback, "missing", 12);
            compare(scalar, fallback, "missing", 12);
            compare(nil, fallback, "missing", 12);
            sol::state otherLua;
            const sol::main_object otherFallback = otherLua.create_table_with("missing", 73);
            compare(props, otherFallback, "missing", 12);
        }

        TEST_F(LuaUiPropertyTest, MetatableOwnerReplacementUsesTheOriginalTemplate)
        {
            sol::main_object fallback = mLua.create_table_with("value", 17);
            const sol::main_object originalFallback = fallback;
            mLua["replaceFallback"] = [&] { fallback = sol::main_object(mLua.create_table_with("value", 999)); };
            mLua.safe_script("props=setmetatable({}, {__index=function() replaceFallback(); return nil end})");
            const sol::main_object props = mLua["props"];
            const int top = lua_gettop(mLua);
            EXPECT_EQ(parseProperty(props, fallback, "value", 0), 17);
            EXPECT_EQ(lua_gettop(mLua), top);
            fallback = originalFallback;
            EXPECT_EQ(parseProperty(sol::object(props), sol::object(fallback), "value", 0), 17);
            EXPECT_EQ(lua_gettop(mLua), top);

            mLua["originalTemplate"] = originalFallback;
            mLua.safe_script(R"(
                mutatingProps=setmetatable({}, {__index=function()
                    setmetatable(originalTemplate, {__index=function() return 71 end})
                    return nil
                end})
            )");
            const sol::main_object mutatingProps = mLua["mutatingProps"];
            fallback = originalFallback;
            EXPECT_EQ(parseProperty(mutatingProps, fallback, "value", 0), 71);
            EXPECT_EQ(lua_gettop(mLua), top);
            mLua.safe_script("setmetatable(originalTemplate,nil)");
            EXPECT_EQ(parseProperty(sol::object(mutatingProps), sol::object(fallback), "value", 0), 71);
            EXPECT_EQ(lua_gettop(mLua), top);

            // A userdata finalizer can replace the native owner during a
            // protected lookup too. Both originals must remain rooted.
            mLua.safe_script("setmetatable(originalTemplate,nil)");
            lua_newuserdata(mLua, 1);
            lua_newtable(mLua);
            const sol::function replacement = mLua["replaceFallback"];
            replacement.push();
            lua_setfield(mLua, -2, "__gc");
            lua_setmetatable(mLua, -2);
            lua_setglobal(mLua, "garbage");
            mLua.safe_script(R"(
                collectingProps=setmetatable({}, {__index=function()
                    garbage=nil; collectgarbage('collect'); return nil
                end})
            )");
            const sol::main_object collectingProps = mLua["collectingProps"];
            fallback = originalFallback;
            EXPECT_EQ(parseProperty(collectingProps, fallback, "value", 0), 17);
            EXPECT_EQ(fallback.as<sol::table>().get<int>("value"), 999);
            EXPECT_EQ(lua_gettop(mLua), top);
        }
    }
}
