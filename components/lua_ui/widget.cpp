#include "widget.hpp"
#include "components/lua/utilpackage.hpp"
#include "components/lua_ui/util.hpp"
#include "element.hpp"
#include <components/misc/combinedperformance.hpp>
#include <components/widgets/windowsnap.hpp>

#include <SDL_events.h>
#include <components/sdlutil/sdlmappings.hpp>
#include <cstdlib>
#include <ranges>

namespace
{
    // Arbitrary large number caps to prevent performance issues
    constexpr int sExtCalcSizeWidthCap = 10000000;
    constexpr int sExtCalcSizeHeightCap = 10000000;
    constexpr int sExtCalcPositionLeftCap = 10000000;
    constexpr int sExtCalcPositionTopCap = 10000000;

    // MyGUI/Lua UI actions run on the main thread. Thread-local state also
    // keeps independent native test contexts isolated.
    thread_local std::uint64_t sLayoutEpoch = 0;
    thread_local unsigned sLayoutDepth = 0;
    thread_local bool sLayoutReuseAllowed = false;
    thread_local bool sCollectLayoutStatistics = false;
    thread_local LuaUi::LayoutUpdateStatistics sLayoutStatistics;
}

namespace LuaUi
{
    bool useNativeLayoutReuse()
    {
        static const bool enabled = [] {
            const char* legacy = std::getenv("OPENMW_LUA_UI_LEGACY_UPDATES");
            if (!legacy)
                legacy = std::getenv("OPENMW_LUA_UI_LEGACY_LAYOUT");
            return Misc::useCombinedPerformance() && (legacy == nullptr || std::string_view(legacy) != "1");
        }();
        return enabled;
    }

    bool layoutReuseInProgress()
    {
        return sLayoutDepth != 0 && sLayoutReuseAllowed;
    }

    void invalidateLayoutReuse()
    {
        sLayoutReuseAllowed = false;
    }

    LayoutUpdateScope::LayoutUpdateScope(WidgetExtension* root)
    {
        if (sLayoutDepth++ == 0)
        {
            ++sLayoutEpoch;
            sLayoutReuseAllowed = useNativeLayoutReuse();
        }
        if (root && sLayoutReuseAllowed)
        {
            while (root->getParent())
                root = root->getParent();
            root->preserveRetainedExternalReads();
        }
    }

    LayoutUpdateScope::~LayoutUpdateScope()
    {
        --sLayoutDepth;
    }

    void collectLayoutUpdateStatistics(bool enabled)
    {
        sCollectLayoutStatistics = enabled;
        sLayoutStatistics = {};
    }

    LayoutUpdateStatistics takeLayoutUpdateStatistics()
    {
        const auto result = sLayoutStatistics;
        sLayoutStatistics = {};
        return result;
    }

    void WidgetExtension::preserveRetainedExternalReads() const
    {
        if (!layoutReuseInProgress())
            return;
        if (!allowsLayoutReuse())
        {
            invalidateLayoutReuse();
            return;
        }
        preserveDynamicLayoutReads(mExternal);
        for (const WidgetExtension* child : mTemplateChildren)
            child->preserveRetainedExternalReads();
        for (const WidgetExtension* child : mChildren)
            child->preserveRetainedExternalReads();
    }

    void WidgetExtension::invalidateLayout()
    {
        if (!layoutReuseInProgress())
            return;
        for (WidgetExtension* current = this; current; current = current->mParent)
        {
            current->mLayoutDirty = true;
            ++current->mLayoutRevision;
        }
    }

    bool WidgetExtension::reuseLayout()
    {
        if (!allowsLayoutReuse())
        {
            invalidateLayoutReuse();
            return false;
        }
        if (!layoutReuseInProgress() || mLayoutDirty || mLayoutEpoch != sLayoutEpoch)
            return false;
        MyGUI::Widget* parent = mWidget->getParent();
        const bool parentVisible = parent == nullptr || parent->getInheritedVisible();
        if (mLayoutNativeParent != parent || mLayoutParentVisible != parentVisible || mLayoutParentSize != parentSize()
            || mLayoutCoord != mWidget->getCoord() || mLayoutForcePosition != mForcePosition
            || mLayoutForceSize != mForceSize || (mForcePosition && mLayoutForcedCoord.point() != mForcedCoord.point())
            || (mForceSize && mLayoutForcedCoord.size() != mForcedCoord.size()))
            return false;
        if (sCollectLayoutStatistics)
            ++sLayoutStatistics.reused;
        return true;
    }

    void WidgetExtension::invalidateLayoutMeasurement()
    {
        if (!layoutReuseInProgress())
            return;
        for (WidgetExtension* current = this; current; current = current->mParent)
        {
            current->mLayoutDirty = true;
            ++current->mLayoutRevision;
            ++current->mLayoutMeasurementRevision;
        }
    }

    void WidgetExtension::rememberLayout(std::uint64_t revision)
    {
        if (sCollectLayoutStatistics)
            ++sLayoutStatistics.calculated;
        if (!layoutReuseInProgress())
            return;
        mLayoutEpoch = sLayoutEpoch;
        mLayoutParentSize = parentSize();
        mLayoutCoord = mWidget->getCoord();
        mLayoutForcedCoord = mForcedCoord;
        mLayoutForcePosition = mForcePosition;
        mLayoutForceSize = mForceSize;
        mLayoutNativeParent = mWidget->getParent();
        mLayoutParentVisible = mLayoutNativeParent == nullptr || mLayoutNativeParent->getInheritedVisible();
        // A child can change its measured size while its parent is applying
        // constraints (notably wrapped text). Keep the next pass when that
        // dependency changed during this traversal; it is not redundant.
        mLayoutDirty = revision != mLayoutRevision || calculateCoord() != mLayoutCoord;
        if (mLayoutDirty && mParent)
            mParent->invalidateLayout();
    }

    WidgetExtension::WidgetExtension()
        : mForcePosition(false)
        , mForceSize(false)
        , mPropagateEvents(true)
        , mVisible(true)
        , mLua(nullptr)
        , mWidget(nullptr)
        , mSlot(this)
        , mLayout(sol::nil)
        , mProperties(sol::nil)
        , mTemplateProperties(sol::nil)
        , mExternal(sol::nil)
        , mParent(nullptr)
        , mTemplateChild(false)
        , mElementRoot(false)
        , mContentWidget(nullptr)
    {
    }

    void WidgetExtension::initialize(lua_State* lua, MyGUI::Widget* self, bool isRoot)
    {
        mLua = lua;
        mWidget = self;
        mElementRoot = isRoot;
        initialize();
        updateTemplate();
    }

    void WidgetExtension::initialize()
    {
        // \todo might be more efficient to only register these if there are Lua callbacks
        registerEvents(mWidget);

        mContentWidget = mWidget->createWidget<MyGUI::Widget>("", MyGUI::IntCoord(), MyGUI::Align::Default);
        mContentWidget->setNeedMouseFocus(false);
        mContentWidget->setNeedKeyFocus(false);
        mContentWidget->setInheritsPick(true);
    }

    void WidgetExtension::deinitialize()
    {
        if (mWindowSnapTarget)
        {
            Gui::unregisterSnapWindow(mWidget);
            mWindowSnapTarget = false;
        }
        clearCallbacks();
        clearEvents(mWidget);

        for (WidgetExtension* w : mChildren)
            w->deinitialize();
        for (WidgetExtension* w : mTemplateChildren)
            w->deinitialize();
    }

    void WidgetExtension::registerEvents(MyGUI::Widget* w)
    {
        w->eventKeyButtonPressed += MyGUI::newDelegate(this, &WidgetExtension::keyPress);
        w->eventKeyButtonReleased += MyGUI::newDelegate(this, &WidgetExtension::keyRelease);
        w->eventMouseButtonClick += MyGUI::newDelegate(this, &WidgetExtension::mouseClick);
        w->eventMouseButtonDoubleClick += MyGUI::newDelegate(this, &WidgetExtension::mouseDoubleClick);
        w->eventMouseButtonPressed += MyGUI::newDelegate(this, &WidgetExtension::mousePress);
        w->eventMouseButtonReleased += MyGUI::newDelegate(this, &WidgetExtension::mouseRelease);
        w->eventMouseMove += MyGUI::newDelegate(this, &WidgetExtension::mouseMove);
        w->eventMouseDrag += MyGUI::newDelegate(this, &WidgetExtension::mouseDrag);

        w->eventMouseSetFocus += MyGUI::newDelegate(this, &WidgetExtension::focusGain);
        w->eventMouseLostFocus += MyGUI::newDelegate(this, &WidgetExtension::focusLoss);
        w->eventKeySetFocus += MyGUI::newDelegate(this, &WidgetExtension::focusGain);
        w->eventKeyLostFocus += MyGUI::newDelegate(this, &WidgetExtension::focusLoss);
    }
    void WidgetExtension::clearEvents(MyGUI::Widget* w)
    {
        w->eventKeyButtonPressed.clear();
        w->eventKeyButtonReleased.clear();
        w->eventMouseButtonClick.clear();
        w->eventMouseButtonDoubleClick.clear();
        w->eventMouseButtonPressed.clear();
        w->eventMouseButtonReleased.clear();
        w->eventMouseMove.clear();
        w->eventMouseDrag.clear();

        w->eventMouseSetFocus.clear();
        w->eventMouseLostFocus.clear();
        w->eventKeySetFocus.clear();
        w->eventKeyLostFocus.clear();
    }

    void WidgetExtension::reset()
    {
        // detach all children from the slot widget, in case it gets destroyed
        for (auto& w : mChildren)
            w->widget()->detachFromWidget();
    }

    void WidgetExtension::updateVisible()
    {
        // workaround for MyGUI bug
        // parent visibility doesn't affect added children
        MyGUI::Widget* parent = widget()->getParent();
        bool inheritedVisible = mVisible && (parent == nullptr || parent->getInheritedVisible());
        widget()->setVisible(inheritedVisible);
    }

    void WidgetExtension::attach(WidgetExtension* ext)
    {
        if (ext->mParent != this)
        {
            // A retained Element can arrive from another root whose live
            // external fields were not part of this action's initial scan.
            ext->preserveRetainedExternalReads();
            if (ext->mParent)
            {
                // Immediate relayout can also visit the previous hierarchy's
                // remaining children, outside the destination's initial scan.
                WidgetExtension* previousRoot = ext->mParent;
                while (previousRoot->mParent)
                    previousRoot = previousRoot->mParent;
                previousRoot->preserveRetainedExternalReads();
                auto children = ext->mParent->children();
                std::erase(children, ext);
                ext->mParent->setChildren(children);
            }
            ext->detachFromParent();
        }
        ext->mParent = this;
        ext->mTemplateChild = false;
        ext->widget()->attachToWidget(mSlot->contentWidget());
    }

    void WidgetExtension::attachTemplate(WidgetExtension* ext)
    {
        if (ext->mParent != this)
            ext->preserveRetainedExternalReads();
        ext->mParent = this;
        ext->mTemplateChild = true;
        ext->widget()->attachToWidget(widget());
    }

    void WidgetExtension::detachFromParent()
    {
        invalidateLayout();
        mParent = nullptr;
        widget()->detachFromWidget();
        widget()->detachFromLayer();
    }

    WidgetExtension* WidgetExtension::findDeep(std::string_view flagName)
    {
        for (WidgetExtension* w : mChildren)
        {
            WidgetExtension* result = w->findDeep(flagName);
            if (result != nullptr)
                return result;
        }
        if (externalValue(flagName, false))
            return this;
        return nullptr;
    }

    void WidgetExtension::findAll(std::string_view flagName, std::vector<WidgetExtension*>& result)
    {
        if (externalValue(flagName, false))
            result.push_back(this);
        for (WidgetExtension* w : mChildren)
            w->findAll(flagName, result);
    }

    WidgetExtension* WidgetExtension::findDeepInTemplates(std::string_view flagName)
    {
        for (WidgetExtension* w : mTemplateChildren)
        {
            WidgetExtension* result = w->findDeep(flagName);
            if (result != nullptr)
                return result;
        }
        return nullptr;
    }

    std::vector<WidgetExtension*> WidgetExtension::findAllInTemplates(std::string_view flagName)
    {
        std::vector<WidgetExtension*> result;
        for (WidgetExtension* w : mTemplateChildren)
            w->findAll(flagName, result);
        return result;
    }

    sol::object WidgetExtension::keyEvent(LuaUtil::LuaView& view, MyGUI::KeyCode code) const
    {
        auto keySym = SDL_Keysym();
        keySym.sym = SDLUtil::myGuiKeyToSdl(code);
        keySym.scancode = SDL_GetScancodeFromKey(keySym.sym);
        keySym.mod = static_cast<Uint16>(SDL_GetModState());
        return sol::make_object(view.sol(), keySym);
    }

    sol::object WidgetExtension::mouseEvent(
        LuaUtil::LuaView& view, int left, int top, MyGUI::MouseButton button = MyGUI::MouseButton::None) const
    {
        osg::Vec2f position(static_cast<float>(left), static_cast<float>(top));
        MyGUI::IntPoint absolutePosition = mWidget->getAbsolutePosition();
        osg::Vec2f offset = position
            - osg::Vec2f(static_cast<float>(absolutePosition.left), static_cast<float>(absolutePosition.top));
        sol::table table = view.newTable();
        int sdlButton = SDLUtil::myGuiMouseButtonToSdl(button);
        table["position"] = position;
        table["offset"] = offset;
        if (sdlButton != 0) // nil if no button was pressed
            table["button"] = sdlButton;
        return table;
    }

    void WidgetExtension::setChildren(const std::vector<WidgetExtension*>& children)
    {
        invalidateLayout();
        mChildren.resize(children.size());
        for (size_t i = 0; i < children.size(); ++i)
        {
            mChildren[i] = children[i];
            attach(mChildren[i]);
        }
        updateChildren();
    }

    void WidgetExtension::setTemplateChildren(const std::vector<WidgetExtension*>& children)
    {
        invalidateLayout();
        mTemplateChildren.resize(children.size());
        for (size_t i = 0; i < children.size(); ++i)
        {
            mTemplateChildren[i] = children[i];
            attachTemplate(mTemplateChildren[i]);
        }
        updateTemplate();

        // Template children attach directly to this widget, while normal content is
        // kept in mContentWidget to apply padding and clipping. Reattach the wrapper
        // after templates so normal content remains the top-most sibling.
        mContentWidget->detachFromWidget();
        mContentWidget->attachToWidget(widget());
    }

    void WidgetExtension::updateTemplate()
    {
        WidgetExtension* slot = findDeepInTemplates("slot");
        if (slot == nullptr)
            mSlot = this;
        else
            mSlot = slot->mSlot;
    }

    void WidgetExtension::setCallback(const std::string& name, const LuaUtil::Callback& callback)
    {
        mCallbacks[name] = callback;
    }

    void WidgetExtension::clearCallbacks()
    {
        mCallbacks.clear();
    }

    MyGUI::IntCoord WidgetExtension::forcedCoord()
    {
        return mForcedCoord;
    }

    void WidgetExtension::forceCoord(const MyGUI::IntCoord& offset)
    {
        if (mParent && (!mForcePosition || !mForceSize || mForcedCoord != offset))
            mParent->invalidateLayout();
        mForcePosition = true;
        mForceSize = true;
        mForcedCoord = offset;
    }

    void WidgetExtension::forcePosition(const MyGUI::IntPoint& pos)
    {
        if (mParent && (!mForcePosition || mForcedCoord.point() != pos))
            mParent->invalidateLayout();
        mForcePosition = true;
        mForcedCoord = pos;
    }

    void WidgetExtension::forceSize(const MyGUI::IntSize& size)
    {
        if (mParent && (!mForceSize || mForcedCoord.size() != size))
            mParent->invalidateLayout();
        mForceSize = true;
        mForcedCoord = size;
    }

    void WidgetExtension::clearForced()
    {
        // Flex temporarily clears its constraints while measuring a child,
        // then restores them before applying geometry. Compare the final
        // constraint values in reuseLayout instead of invalidating this child
        // merely because that temporary measurement happened.
        if (mParent && (mForcePosition || mForceSize))
            mParent->invalidateLayout();
        mForcePosition = false;
        mForceSize = false;
    }

    void WidgetExtension::updateCoord()
    {
        if (reuseLayout())
            return;
        const std::uint64_t revision = mLayoutRevision;
        updateVisible();

        MyGUI::IntCoord oldCoord = mWidget->getCoord();
        MyGUI::IntCoord newCoord = calculateCoord();

        if (oldCoord != newCoord)
        {
            mWidget->setCoord(newCoord);
            if (mParent)
                mParent->invalidateLayoutMeasurement();
        }

        if (mContentWidget)
            mContentWidget->setCoord(MyGUI::IntCoord(getContentOffset(), getContentSize()));

        updateChildrenCoord();
        rememberLayout(revision);
    }

    void WidgetExtension::setProperties(const sol::main_object& props)
    {
        preserveDynamicLayoutReads(props);
        invalidateLayout();
        mProperties = props;
        updateProperties();
        // Derived setters can perform an immediate layout before later
        // properties are applied (LuaText::setCaption is one example). That
        // intermediate result must not hide a subsequent font/size change.
        invalidateLayout();
    }

    void WidgetExtension::updateProperties()
    {
        const bool snapTarget = propertyValue("windowSnapTarget", false);
        if (snapTarget != mWindowSnapTarget)
        {
            if (snapTarget)
                Gui::registerSnapWindow(mWidget);
            else
                Gui::unregisterSnapWindow(mWidget);
            mWindowSnapTarget = snapTarget;
        }
        mPropagateEvents = propertyValue("propagateEvents", true);
        mAbsoluteCoord = propertyValue("position", MyGUI::IntPoint());
        mAbsoluteCoord = propertyValue("size", MyGUI::IntSize());
        mRelativeCoord = propertyValue("relativePosition", MyGUI::FloatPoint());
        mRelativeCoord = propertyValue("relativeSize", MyGUI::FloatSize());
        mAnchor = propertyValue("anchor", MyGUI::FloatSize());
        mVisible = propertyValue("visible", true);
        mWidget->setVisible(mVisible);
        mWidget->setNeedMouseFocus(!propertyValue("ignorePointerEvents", false));
        mWidget->setPointer(propertyValue("pointer", std::string("arrow")));
        mWidget->setAlpha(propertyValue("alpha", 1.f));
        mWidget->setInheritsAlpha(propertyValue("inheritAlpha", true));
        parsePadding();
    }

    void WidgetExtension::parsePadding()
    {
        mPadding = Padding();
        const LuaUtil::Vec4 value = propertyValue("padding", LuaUtil::Vec4());
        mPadding = Padding{ static_cast<int>(value.x()), static_cast<int>(value.y()), static_cast<int>(value.z()),
            static_cast<int>(value.w()) };
    }

    void WidgetExtension::updateChildrenCoord()
    {
        for (WidgetExtension* w : mTemplateChildren)
            w->updateCoord();
        for (WidgetExtension* w : mChildren)
            w->updateCoord();
    }

    MyGUI::IntSize WidgetExtension::parentSize() const
    {
        if (!mParent)
            return widget()->getParentSize(); // size of the layer
        if (mTemplateChild)
            return mParent->templateScalingSize();
        else
            return mParent->childScalingSize();
    }

    MyGUI::IntPoint WidgetExtension::getContentOffset() const
    {
        return MyGUI::IntPoint(mPadding.mLeft, mPadding.mTop);
    }

    MyGUI::IntSize WidgetExtension::getContentSize() const
    {
        MyGUI::IntSize fullSize = calculateSize();
        return MyGUI::IntSize(std::max(0, fullSize.width - (mPadding.mLeft + mPadding.mRight)),
            std::max(0, fullSize.height - (mPadding.mTop + mPadding.mBottom)));
    }

    MyGUI::IntSize WidgetExtension::calculateSize() const
    {
        MyGUI::IntSize newSize;
        if (mForceSize)
            newSize = mForcedCoord.size();
        else
        {
            MyGUI::IntSize pSize = parentSize();
            newSize = mAbsoluteCoord.size();
            newSize.width += static_cast<int>(mRelativeCoord.width * pSize.width);
            newSize.height += static_cast<int>(mRelativeCoord.height * pSize.height);
        }
        newSize.width = std::clamp(newSize.width, -sExtCalcSizeWidthCap, sExtCalcSizeWidthCap);
        newSize.height = std::clamp(newSize.height, -sExtCalcSizeHeightCap, sExtCalcSizeHeightCap);
        return newSize;
    }

    MyGUI::IntPoint WidgetExtension::calculatePosition(const MyGUI::IntSize& size) const
    {
        MyGUI::IntPoint newPosition;
        if (mForcePosition)
            newPosition = mForcedCoord.point();
        else
        {
            MyGUI::IntSize pSize = parentSize();
            newPosition = mAbsoluteCoord.point();
            newPosition.left += static_cast<int>(mRelativeCoord.left * pSize.width - mAnchor.width * size.width);
            newPosition.top += static_cast<int>(mRelativeCoord.top * pSize.height - mAnchor.height * size.height);
        }
        newPosition.left = std::clamp(newPosition.left, -sExtCalcPositionLeftCap, sExtCalcPositionLeftCap);
        newPosition.top = std::clamp(newPosition.top, -sExtCalcPositionTopCap, sExtCalcPositionTopCap);
        return newPosition;
    }

    MyGUI::IntCoord WidgetExtension::calculateCoord() const
    {
        MyGUI::IntCoord newCoord;
        newCoord = calculateSize();
        newCoord = calculatePosition(newCoord.size());
        return newCoord;
    }

    MyGUI::Widget* WidgetExtension::contentWidget() const
    {
        return mContentWidget;
    }

    MyGUI::IntSize WidgetExtension::childScalingSize() const
    {
        return mSlot->getContentSize();
    }

    MyGUI::IntSize WidgetExtension::templateScalingSize() const
    {
        return widget()->getSize();
    }

    void WidgetExtension::triggerEvent(std::string_view name, sol::object argument) const
    {
        invalidateLayoutReuse();
        auto it = mCallbacks.find(name);
        if (it != mCallbacks.end())
            it->second.call(argument, mLayout);
    }

    bool WidgetExtension::collectWarnings(Warnings& warnings, int depth, bool generateWarningStrings) const
    {
        auto beginningSize = warnings.size();
        if (collectUnusedWarnings(warnings, generateWarningStrings) && !generateWarningStrings)
            return true;

        if (depth > 0)
        {
            std::ranges::transform(warnings, warnings.begin(),
                [&](const std::string& warning) { return std::string((depth + 1) * 2, ' ') + warning; });
        }

        for (size_t i = 0; i < mChildren.size(); i++)
        {
            Warnings childWarnings;
            if (mChildren[i]->isRoot())
                continue;
            if (!mChildren[i]->collectWarnings(childWarnings, depth + 1, generateWarningStrings))
                continue;
            if (!generateWarningStrings)
                return true;
            warnings.emplace_back(std::string((depth + 1) * 2, ' ') + "in content[" + std::to_string(i) + "]:");
            std::ranges::move(childWarnings, std::back_inserter(warnings));
        }
        if (!warnings.empty())
        {
            warnings.insert(warnings.begin(), "Warnings generated for " + diagnosticName() + ":");
            if (depth > 0)
                warnings.front() = std::string(depth * 2, ' ') + warnings.front();
        }

        return warnings.size() != beginningSize;
    }

    bool WidgetExtension::collectUnusedWarnings(std::vector<std::string>& warnings, bool generateWarningStrings) const
    {
        const auto& usedPropsKeys = allUsedProperties();
        const auto& usedLayoutKeys = LuaUi::Element::allLayoutProperties();
        bool layoutWarn = warnUnused(warnings, mLayout, "layout", usedLayoutKeys, generateWarningStrings);
        if (layoutWarn && !generateWarningStrings)
            // We can skip checking props
            return true;
        bool propsWarn = warnUnused(warnings, mProperties, "props", usedPropsKeys, generateWarningStrings);

        return layoutWarn || propsWarn;
    }

    std::string WidgetExtension::diagnosticName() const
    {
        const std::string& name = mWidget->getName();
        const std::string typeName(mWidget->getTypeName());
        if (name.empty())
            return "unnamed " + typeName;
        return typeName + " named '" + name + "'";
    }

    const std::vector<std::string_view>& WidgetExtension::allUsedProperties() const
    {
        static std::vector<std::string_view> usedProps = {
            "windowSnapTarget",
            "propagateEvents",
            "position",
            "size",
            "relativePosition",
            "relativeSize",
            "anchor",
            "visible",
            "ignorePointerEvents",
            "pointer",
            "alpha",
            "inheritAlpha",
            "padding",
        };
        return usedProps;
    }

    void WidgetExtension::keyPress(MyGUI::Widget*, MyGUI::KeyCode code, MyGUI::Char ch)
    {
        protectedCall([=, this](LuaUtil::LuaView& view) {
            if (code == MyGUI::KeyCode::None)
            {
                propagateEvent("textInput", [&](auto w) {
                    MyGUI::UString uString;
                    uString.push_back(static_cast<MyGUI::UString::unicode_char>(ch));
                    return sol::make_object(view.sol(), uString.asUTF8());
                });
            }
            else
                propagateEvent("keyPress", [&](auto w) { return w->keyEvent(view, code); });
        });
    }

    void WidgetExtension::keyRelease(MyGUI::Widget*, MyGUI::KeyCode code)
    {
        protectedCall([=, this](LuaUtil::LuaView& view) {
            propagateEvent("keyRelease", [&](auto w) { return w->keyEvent(view, code); });
        });
    }

    void WidgetExtension::mouseMove(MyGUI::Widget*, int left, int top)
    {
        protectedCall([=, this](LuaUtil::LuaView& view) {
            propagateEvent("mouseMove", [&](auto w) { return w->mouseEvent(view, left, top); });
        });
    }

    void WidgetExtension::mouseDrag(MyGUI::Widget*, int left, int top, MyGUI::MouseButton button)
    {
        protectedCall([=, this](LuaUtil::LuaView& view) {
            propagateEvent("mouseMove", [&](auto w) { return w->mouseEvent(view, left, top, button); });
        });
    }

    void WidgetExtension::mouseClick(MyGUI::Widget* /*widget*/)
    {
        propagateEvent("mouseClick", [](auto) { return sol::nil; });
    }

    void WidgetExtension::mouseDoubleClick(MyGUI::Widget* /*widget*/)
    {
        propagateEvent("mouseDoubleClick", [](auto) { return sol::nil; });
    }

    void WidgetExtension::mousePress(MyGUI::Widget*, int left, int top, MyGUI::MouseButton button)
    {
        protectedCall([=, this](LuaUtil::LuaView& view) {
            propagateEvent("mousePress", [&](auto w) { return w->mouseEvent(view, left, top, button); });
        });
    }

    void WidgetExtension::mouseRelease(MyGUI::Widget*, int left, int top, MyGUI::MouseButton button)
    {
        protectedCall([=, this](LuaUtil::LuaView& view) {
            propagateEvent("mouseRelease", [&](auto w) { return w->mouseEvent(view, left, top, button); });
        });
    }

    void WidgetExtension::mouseWheel(MyGUI::Widget*, float left, float top, float x, float y)
    {
        protectedCall([=, this](LuaUtil::LuaView& view) {
            propagateEvent("mouseWheel", [&](auto w) {
                sol::table event = view.newTable();
                event["position"] = osg::Vec2f(left, top);
                event["delta"] = osg::Vec2f(x, y);
                return event;
            });
        });
    }

    void WidgetExtension::focusGain(MyGUI::Widget*, MyGUI::Widget*)
    {
        propagateEvent("focusGain", [](auto) { return sol::nil; });
    }

    void WidgetExtension::focusLoss(MyGUI::Widget*, MyGUI::Widget*)
    {
        propagateEvent("focusLoss", [](auto) { return sol::nil; });
    }
}
