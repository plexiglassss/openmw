#ifndef OPENMW_LUAUI_WIDGET
#define OPENMW_LUAUI_WIDGET

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include <MyGUI_Widget.h>
#include <sol/sol.hpp>

#include <components/lua/asyncpackage.hpp>

#include "padding.hpp"
#include "properties.hpp"

namespace LuaUi
{
    class WidgetExtension;

    // A layout cache is valid only while one Element action is being applied.
    // Lua tables may be mutated in-place between actions, including the
    // external fields of retained child Elements. Never reuse a result across
    // that boundary merely because its table or widget identity is unchanged.
    class LayoutUpdateScope
    {
    public:
        explicit LayoutUpdateScope(WidgetExtension* root);
        ~LayoutUpdateScope();
        LayoutUpdateScope(const LayoutUpdateScope&) = delete;
        LayoutUpdateScope& operator=(const LayoutUpdateScope&) = delete;
    };

    bool useNativeLayoutReuse();
    bool layoutReuseInProgress();
    void invalidateLayoutReuse();

    template <class Object>
    void preserveDynamicLayoutReads(const Object& value)
    {
        if (!layoutReuseInProgress() || !value.valid() || value.get_type() != sol::type::table)
            return;
        lua_State* lua = value.lua_state();
        value.push(lua);
        const bool hasMetatable = lua_getmetatable(lua, -1) != 0;
        lua_pop(lua, hasMetatable ? 2 : 1);
        if (hasMetatable)
            invalidateLayoutReuse();
    }

    struct LayoutUpdateStatistics
    {
        std::uint64_t calculated = 0;
        std::uint64_t reused = 0;
    };
    // Disabled during normal play; used by native regression/benchmark tests.
    void collectLayoutUpdateStatistics(bool enabled);
    LayoutUpdateStatistics takeLayoutUpdateStatistics();

    /*
     * extends MyGUI::Widget and its child classes
     * memory ownership is controlled by MyGUI
     * it is important not to call any WidgetExtension methods after destroying the MyGUI::Widget
     */
    class WidgetExtension
    {
    public:
        using Warnings = std::vector<std::string>;
        WidgetExtension();

        virtual ~WidgetExtension() = default;

        // must be called after creating the underlying MyGUI::Widget
        void initialize(lua_State* lua, MyGUI::Widget* self, bool isRoot);
        // must be called after before destroying the underlying MyGUI::Widget
        virtual void deinitialize();

        MyGUI::Widget* widget() const { return mWidget; }

        bool isRoot() const { return mElementRoot; }
        WidgetExtension* getParent() const { return mParent; }
        void detachFromParent();

        void detachChildrenIf(auto&& predicate) { detachChildrenIf(predicate, mChildren); }
        void detachTemplateChildrenIf(auto&& predicate) { detachChildrenIf(predicate, mTemplateChildren); }

        void reset();

        const std::vector<WidgetExtension*>& children() { return mChildren; }
        void setChildren(const std::vector<WidgetExtension*>&);

        const std::vector<WidgetExtension*>& templateChildren() { return mTemplateChildren; }
        void setTemplateChildren(const std::vector<WidgetExtension*>&);

        void setCallback(const std::string&, const LuaUtil::Callback&);
        void clearCallbacks();

        void setProperties(const sol::main_object& props);
        void setTemplateProperties(const sol::main_object& props)
        {
            preserveDynamicLayoutReads(props);
            invalidateLayout();
            mTemplateProperties = props;
        }

        void setExternal(const sol::main_object& external)
        {
            preserveDynamicLayoutReads(external);
            invalidateLayout();
            mExternal = external;
        }

        MyGUI::IntCoord forcedCoord();
        void forceCoord(const MyGUI::IntCoord& offset);
        void forceSize(const MyGUI::IntSize& size);
        void forcePosition(const MyGUI::IntPoint& pos);
        void clearForced();

        virtual void updateCoord();

        const sol::main_table& getLayout() { return mLayout; }
        void setLayout(const sol::table& layout)
        {
            preserveDynamicLayoutReads(layout);
            mLayout = layout;
        }

        template <typename T>
        T externalValue(std::string_view name, const T& defaultValue) const
        {
            return parseExternal(mExternal, name, defaultValue);
        }

        virtual MyGUI::IntSize calculateSize() const;
        virtual MyGUI::IntPoint calculatePosition(const MyGUI::IntSize& size) const;
        MyGUI::IntCoord calculateCoord() const;
        void mouseWheel(MyGUI::Widget*, float, float, float, float);

        virtual bool isTextInput() { return false; }
        virtual bool allowsLayoutReuse() const { return true; }
        bool collectWarnings(Warnings& warnings, int depth, bool generateWarningStrings) const;
        std::string diagnosticName() const;

    protected:
        // Geometry changes invalidate ancestors because containers and flex
        // layouts can derive their own size/arrangement from a descendant.
        void invalidateLayout();
        void invalidateLayoutMeasurement();
        std::uint64_t layoutMeasurementRevision() const { return mLayoutMeasurementRevision; }
        bool reuseLayout();
        void rememberLayout(std::uint64_t revision);

        virtual void initialize();
        void registerEvents(MyGUI::Widget* w);
        void clearEvents(MyGUI::Widget* w);

        sol::object keyEvent(LuaUtil::LuaView& view, MyGUI::KeyCode) const;
        sol::object mouseEvent(LuaUtil::LuaView& view, int left, int top, MyGUI::MouseButton button) const;

        MyGUI::IntSize parentSize() const;
        // Size available for content, i.e., parent size minus padding
        MyGUI::IntSize getContentSize() const;
        // Offset of content area relative to widget position, i.e., where the content area starts
        MyGUI::IntPoint getContentOffset() const;
        MyGUI::Widget* contentWidget() const;
        virtual MyGUI::IntSize childScalingSize() const;
        virtual MyGUI::IntSize templateScalingSize() const;

        template <typename T>
        T propertyValue(std::string_view name, const T& defaultValue)
        {
            return parseProperty(mProperties, mTemplateProperties, name, defaultValue);
        }

        WidgetExtension* findDeepInTemplates(std::string_view flagName);
        std::vector<WidgetExtension*> findAllInTemplates(std::string_view flagName);

        virtual void updateTemplate();
        virtual void updateProperties();
        virtual void updateChildren() {}

        template <class Lambda>
        void protectedCall(Lambda&& f) const
        {
            invalidateLayoutReuse();
            LuaUtil::protectedCall(mLua, std::forward<Lambda>(f));
        }

        void triggerEvent(std::string_view name, sol::object argument) const;
        template <class ArgFactory>
        void propagateEvent(std::string_view name, const ArgFactory& argumentFactory) const
        {
            invalidateLayoutReuse();
            const WidgetExtension* w = this;
            while (w)
            {
                bool shouldPropagate = true;
                auto it = w->mCallbacks.find(name);
                if (it != w->mCallbacks.end())
                {
                    try
                    {
                        sol::object res = it->second.call(argumentFactory(w), w->mLayout);
                        shouldPropagate = res.is<bool>() && res.as<bool>();
                    }
                    catch (const std::exception& e)
                    {
                        Log(Debug::Warning) << name << " event propagation has failed: " << e.what();
                        shouldPropagate = false;
                    }
                }
                if (w->mParent && w->mPropagateEvents && shouldPropagate)
                    w = w->mParent;
                else
                    w = nullptr;
            }
        }

        bool collectUnusedWarnings(std::vector<std::string>& warnings, bool generateWarningStrings) const;

        virtual const std::vector<std::string_view>& allUsedProperties() const;

        bool mForcePosition;
        bool mForceSize;
        // offsets the position and size, used only in C++ widget code
        MyGUI::IntCoord mForcedCoord;
        // position and size in pixels
        MyGUI::IntCoord mAbsoluteCoord;
        // position and size as a ratio of parent size
        MyGUI::FloatCoord mRelativeCoord;
        // negative position offset as a ratio of this widget's size
        // used in combination with relative coord to align the widget, e. g. center it
        MyGUI::FloatSize mAnchor;
        Padding mPadding;

        bool mPropagateEvents;
        bool mWindowSnapTarget = false;
        bool mVisible; // used to implement updateVisible

    private:
        friend class LayoutUpdateScope;
        void preserveRetainedExternalReads() const;
        bool mLayoutDirty = true;
        std::uint64_t mLayoutRevision = 0;
        std::uint64_t mLayoutMeasurementRevision = 0;
        std::uint64_t mLayoutEpoch = 0;
        MyGUI::IntSize mLayoutParentSize;
        MyGUI::IntCoord mLayoutCoord;
        MyGUI::IntCoord mLayoutForcedCoord;
        bool mLayoutForcePosition = false;
        bool mLayoutForceSize = false;
        MyGUI::Widget* mLayoutNativeParent = nullptr;
        bool mLayoutParentVisible = true;

        // use lua_State* instead of sol::state_view because MyGUI requires a default constructor
        lua_State* mLua;
        MyGUI::Widget* mWidget;
        std::vector<WidgetExtension*> mChildren;
        std::vector<WidgetExtension*> mTemplateChildren;
        WidgetExtension* mSlot;
        std::map<std::string, LuaUtil::Callback, std::less<>> mCallbacks;
        sol::main_table mLayout;
        sol::main_object mProperties;
        sol::main_object mTemplateProperties;
        sol::main_object mExternal;
        WidgetExtension* mParent;
        bool mTemplateChild;
        bool mElementRoot;
        MyGUI::Widget* mContentWidget;

        void attach(WidgetExtension* ext);
        void attachTemplate(WidgetExtension* ext);

        WidgetExtension* findDeep(std::string_view name);
        void findAll(std::string_view flagName, std::vector<WidgetExtension*>& result);

        void updateChildrenCoord();
        void parsePadding();

        void keyPress(MyGUI::Widget*, MyGUI::KeyCode, MyGUI::Char);
        void keyRelease(MyGUI::Widget*, MyGUI::KeyCode);
        void mouseMove(MyGUI::Widget*, int, int);
        void mouseDrag(MyGUI::Widget*, int, int, MyGUI::MouseButton);
        void mouseClick(MyGUI::Widget*);
        void mouseDoubleClick(MyGUI::Widget*);
        void mousePress(MyGUI::Widget*, int, int, MyGUI::MouseButton);
        void mouseRelease(MyGUI::Widget*, int, int, MyGUI::MouseButton);
        void focusGain(MyGUI::Widget*, MyGUI::Widget*);
        void focusLoss(MyGUI::Widget*, MyGUI::Widget*);

        void updateVisible();

        void detachChildrenIf(auto&& predicate, std::vector<WidgetExtension*>& children)
        {
            for (auto it = children.begin(); it != children.end();)
            {
                if (predicate(*it))
                {
                    invalidateLayout();
                    (*it)->detachFromParent();
                    it = children.erase(it);
                }
                else
                    ++it;
            }
        }
    };

    class LuaWidget : public MyGUI::Widget, public WidgetExtension
    {
        MYGUI_RTTI_DERIVED(LuaWidget)
    };
}

#endif // !OPENMW_LUAUI_WIDGET
