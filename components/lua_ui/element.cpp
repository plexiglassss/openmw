#include "element.hpp"

#include <MyGUI_Gui.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <unordered_map>

#include <components/misc/combinedperformance.hpp>
#include "components/settings/values.hpp"
#include "content.hpp"
#include "util.hpp"
#include "widget.hpp"

namespace LuaUi
{
    namespace
    {
        namespace LayoutKeys
        {
            constexpr std::string_view type = "type";
            constexpr std::string_view name = "name";
            constexpr std::string_view layer = "layer";
            constexpr std::string_view templateLayout = "template";
            constexpr std::string_view props = "props";
            constexpr std::string_view events = "events";
            constexpr std::string_view content = "content";
            constexpr std::string_view external = "external";
            constexpr std::string_view userData = "userData";

            const std::vector<std::string_view> allKeys
                = { type, name, layer, templateLayout, props, events, content, external, userData };
        }

        const std::string defaultWidgetType = "LuaWidget";

        constexpr uint64_t maxDepth = 250;

        // Inspect only fields that the native layout reader uses. Raw reads
        // cannot invoke a mod's getter during this check. A dynamic getter
        // may inspect another widget while its parent is being updated.
        bool plainAttachmentLayout(lua_State* lua, WidgetExtension* owner, unsigned depth, size_t& remaining)
        {
            if (depth >= maxDepth || remaining == 0 || !lua_checkstack(lua, 8))
                return false;
            --remaining;
            const Detail::RestoreStack restore(lua);
            const int layout = lua_gettop(lua);
            if (!lua_istable(lua, layout) || lua_getmetatable(lua, layout))
                return false;

            for (const char* field : { "props", "external", "events" })
            {
                lua_pushstring(lua, field);
                lua_rawget(lua, layout);
                if (!lua_isnil(lua, -1) && (!lua_istable(lua, -1) || lua_getmetatable(lua, -1)))
                    return false;
                lua_pop(lua, 1);
            }
            lua_pushliteral(lua, "template");
            lua_rawget(lua, layout);
            if (!lua_isnil(lua, -1) && !plainAttachmentLayout(lua, owner, depth + 1, remaining))
                return false;
            lua_pop(lua, 1);

            lua_pushliteral(lua, "content");
            lua_rawget(lua, layout);
            if (lua_isnil(lua, -1))
                return true;
            const int content = lua_gettop(lua);
            if (!lua_istable(lua, content) || !lua_getmetatable(lua, content) || lua_getmetatable(lua, -1))
                return false;
            lua_pushliteral(lua, "__Content");
            lua_rawget(lua, -2);
            if (!lua_isboolean(lua, -1) || !lua_toboolean(lua, -1))
                return false;
            lua_pop(lua, 1);
            lua_pushliteral(lua, "__len");
            lua_rawget(lua, -2);
            if (!lua_isnil(lua, -1))
                return false;
            lua_pop(lua, 2);

            const size_t count = lua_rawlen(lua, content);
            for (size_t i = 0; i < count; ++i)
            {
                lua_rawgeti(lua, content, static_cast<int>(i + 1));
                if (lua_istable(lua, -1))
                {
                    if (!plainAttachmentLayout(lua, owner, depth + 1, remaining))
                        return false;
                }
                else
                {
                    if (!lua_isuserdata(lua, -1) || !sol::stack::check<Element*>(lua, -1))
                        return false;
                    Element* child = sol::stack::get<Element*>(lua, -1);
                    if (!child || child->mState != Element::Created || child->mRoot == nullptr)
                        return false;
                    // Only the existing tree was checked for live external
                    // properties and native text editors at the start of this action.
                    auto* root = child->mRoot;
                    while (root->getParent())
                        root = root->getParent();
                    if (root != owner)
                        return false;
                }
                lua_pop(lua, 1);
            }
            return true;
        }

        bool canRetainAttachments(const sol::table& layout, WidgetExtension* owner)
        {
            if (!Misc::useTownFrameUpdates() || !layoutReuseInProgress())
                return false;
            lua_State* lua = layout.lua_state();
            const Detail::RestoreStack restore(lua);
            layout.push(lua);
            size_t remaining = 8192;
            while (owner->getParent())
                owner = owner->getParent();
            return plainAttachmentLayout(lua, owner, 0, remaining);
        }

        std::string widgetType(const sol::table& layout)
        {
            preserveDynamicLayoutReads(layout);
            sol::object typeField = LuaUtil::getFieldOrNil(layout, LayoutKeys::type);
            std::string type = LuaUtil::getValueOrDefault(typeField, defaultWidgetType);
            sol::object templateTypeField = [&] {
                sol::object templateLayout = LuaUtil::getFieldOrNil(layout, LayoutKeys::templateLayout);
                preserveDynamicLayoutReads(templateLayout);
                return LuaUtil::getFieldOrNil(templateLayout, LayoutKeys::type);
            }();
            if (templateTypeField != sol::nil)
            {
                std::string templateType = LuaUtil::getValueOrDefault(templateTypeField, defaultWidgetType);
                if (typeField != sol::nil && templateType != type)
                    throw std::logic_error(std::string("Template layout type ") + type
                        + std::string(" doesn't match template type ") + templateType);
                type = std::move(templateType);
            }
            return type;
        }

        void destroyWidget(WidgetExtension* ext)
        {
            ext->deinitialize();
            MyGUI::Gui::getInstancePtr()->destroyWidget(ext->widget());
        }

        void detachElements(WidgetExtension* ext)
        {
            auto predicate = [](WidgetExtension* child) {
                if (child->isRoot())
                    return true;
                detachElements(child);
                return false;
            };
            ext->detachChildrenIf(predicate);
            ext->detachTemplateChildrenIf(predicate);
        }

        void destroyChild(WidgetExtension* ext)
        {
            if (!ext->isRoot())
            {
                detachElements(ext);
                destroyWidget(ext);
            }
            else
                ext->detachFromParent();
        }

        void destroyRoot(WidgetExtension* ext)
        {
            detachElements(ext);
            destroyWidget(ext);
        }

        void updateRootCoord(WidgetExtension* ext)
        {
            WidgetExtension* root = ext;
            while (root->getParent())
                root = root->getParent();
            root->updateCoord();
        }

        WidgetExtension* pluckElementRoot(const sol::object& child, uint64_t depth)
        {
            std::shared_ptr<Element> element = child.as<std::shared_ptr<Element>>();
            if (element->mState == Element::Destroyed || element->mState == Element::Destroy)
                throw std::logic_error("Using a destroyed element as a layout child");
            // child Element was created in the same frame and its action hasn't been processed yet
            if (element->mState == Element::New)
                element->create(depth + 1);
            WidgetExtension* root = element->mRoot;
            assert(root);
            return root;
        }

        WidgetExtension* createWidget(const sol::table& layout, bool isRoot, uint64_t depth);
        void updateWidget(WidgetExtension* ext, const sol::table& layout, uint64_t depth, bool retain);

        std::vector<WidgetExtension*> updateContent(
            const std::vector<WidgetExtension*>& children, const sol::object& contentObj, uint64_t depth, bool retain)
        {
            ++depth;
            std::vector<WidgetExtension*> result;
            if (contentObj == sol::nil)
            {
                for (WidgetExtension* w : children)
                    destroyChild(w);
                return result;
            }
            ContentView content(LuaUtil::cast<sol::table>(contentObj));
            result.resize(content.size());
            size_t minSize = std::min(children.size(), content.size());
            std::vector<WidgetExtension*> toDestroy;
            for (size_t i = 0; i < minSize; i++)
            {
                WidgetExtension* ext = children[i];
                sol::object child = content.at(i);
                if (child.is<Element>())
                {
                    WidgetExtension* root = pluckElementRoot(child, depth);
                    if (ext != root)
                        toDestroy.emplace_back(ext);
                    result[i] = root;
                }
                else
                {
                    sol::table newLayout = child.as<sol::table>();
                    if (ext->widget()->getTypeName() == widgetType(newLayout))
                    {
                        updateWidget(ext, newLayout, depth, retain);
                    }
                    else
                    {
                        toDestroy.emplace_back(ext);
                        ext = createWidget(newLayout, false, depth);
                    }
                    result[i] = ext;
                }
            }
            for (size_t i = minSize; i < content.size(); i++)
            {
                sol::object child = content.at(i);
                if (child.is<Element>())
                    result[i] = pluckElementRoot(child, depth);
                else
                    result[i] = createWidget(child.as<sol::table>(), false, depth);
            }
            // Don't destroy anything until element creation has had a chance to throw
            for (size_t i = minSize; i < children.size(); i++)
                destroyChild(children[i]);
            for (WidgetExtension* ext : toDestroy)
                destroyChild(ext);
            return result;
        }

        void setTemplate(WidgetExtension* ext, const sol::object& templateLayout, uint64_t depth, bool retain)
        {
            ++depth;
            preserveDynamicLayoutReads(templateLayout);
            sol::object props = LuaUtil::getFieldOrNil(templateLayout, LayoutKeys::props);
            ext->setTemplateProperties(props);
            sol::object content = LuaUtil::getFieldOrNil(templateLayout, LayoutKeys::content);
            ext->setTemplateChildren(updateContent(ext->templateChildren(), content, depth, retain), retain);
        }

        void setEventCallbacks(WidgetExtension* ext, const sol::object& eventsObj)
        {
            ext->clearCallbacks();
            if (eventsObj == sol::nil)
                return;
            if (!eventsObj.is<sol::table>())
                throw std::logic_error("The \"events\" layout field must be a table of callbacks");
            auto events = eventsObj.as<sol::table>();
            events.for_each([ext](const sol::object& name, const sol::object& callback) {
                if (name.is<std::string>() && LuaUtil::Callback::isLuaCallback(callback))
                    ext->setCallback(name.as<std::string>(), LuaUtil::Callback::fromLua(callback));
                else if (!name.is<std::string>())
                    Log(Debug::Warning) << "UI event key must be a string";
                else
                    Log(Debug::Warning) << "UI event handler for key \"" << name.as<std::string>()
                                        << "\" must be an openmw.async.callback";
            });
        }

        WidgetExtension* createWidget(const sol::table& layout, bool isRoot, uint64_t depth)
        {
            static auto widgetTypeMap = widgetTypeToName();
            std::string type = widgetType(layout);
            if (widgetTypeMap.find(type) == widgetTypeMap.end())
                throw std::logic_error(std::string("Invalid widget type ") += type);

            std::string name = layout.get_or(LayoutKeys::name, std::string());
            MyGUI::Widget* widget
                = MyGUI::Gui::getInstancePtr()->createWidgetT(type, {}, {}, MyGUI::Align::Default, {}, name);

            WidgetExtension* ext = dynamic_cast<WidgetExtension*>(widget);
            if (!ext)
                throw std::runtime_error("Invalid widget!");
            ext->initialize(layout.lua_state(), widget, isRoot);

            updateWidget(ext, layout, depth, false);
            return ext;
        }

        void updateWidget(WidgetExtension* ext, const sol::table& layout, uint64_t depth, bool retain)
        {
            if (depth >= maxDepth)
                throw std::runtime_error("Maximum layout depth exceeded, probably caused by a circular reference");
            const bool keptAttached = ext->prepareChildrenForUpdate(retain);
            ext->setLayout(layout);
            ext->setExternal(layout.get<sol::object>(LayoutKeys::external));
            setTemplate(ext, layout.get<sol::object>(LayoutKeys::templateLayout), depth, retain);
            ext->setProperties(layout.get<sol::object>(LayoutKeys::props));
            setEventCallbacks(ext, layout.get<sol::object>(LayoutKeys::events));
            ext->setChildren(updateContent(ext->children(), layout.get<sol::object>(LayoutKeys::content), depth, retain),
                keptAttached);
            ext->updateCoord();
        }

        struct PropertyUpdate
        {
            WidgetExtension* widget;
            bool propertiesChanged;
        };

        // Do not cross a child Element boundary. Its own update action owns its
        // property tables. The same inline table can appear in multiple places.
        bool collectPropertyUpdates(WidgetExtension* widget, std::unordered_map<const void*, bool>& requested,
            std::vector<PropertyUpdate>& updates)
        {
            auto match = requested.find(widget->getLayout().pointer());
            const bool changed = match != requested.end();
            if (changed)
                match->second = true;
            bool updateBranch = changed;
            for (const auto& children : { &widget->templateChildren(), &widget->children() })
                for (auto* child : *children)
                    if (!child->isRoot() && collectPropertyUpdates(child, requested, updates))
                        updateBranch = true;
            if (updateBranch)
                updates.push_back({ widget, changed });
            return updateBranch;
        }

        bool updateChangedProperties(WidgetExtension* root, const sol::table& current,
            const std::vector<sol::main_table>& changedLayouts)
        {
            if (current.pointer() != root->getLayout().pointer() || !layoutReuseInProgress())
                return false;
            std::unordered_map<const void*, bool> requested;
            for (const auto& layout : changedLayouts)
                requested.emplace(layout.pointer(), false);
            std::vector<PropertyUpdate> updates;
            collectPropertyUpdates(root, requested, updates);
            if (std::any_of(requested.begin(), requested.end(), [](const auto& entry) { return !entry.second; }))
                return false;
            for (const auto& [widget, changed] : updates)
            {
                // A native LuaWindow can supply the caption for a template child.
                // Keep that ownership and any live property getters on the full path.
                if (widget->widget()->getTypeName() == "LuaWindow" || (changed && !widget->hasPlainPropertyInputs()))
                    return false;
            }
            // Validate membership first. Recompute intrinsic sizes from leaves
            // to root so a changed label can resize all enclosing flexes/containers.
            // Element::update performs the final root layout, including other
            // branches affected by a sibling changing size.
            for (const auto& [widget, changed] : updates)
            {
                if (changed)
                    widget->setProperties(widget->getLayout().get<sol::object>(LayoutKeys::props));
                if (widget != root)
                    widget->updateCoord();
            }
            // A single trace marker lets a gameplay recording confirm that the
            // optional API actually reached the narrow path on this installation.
            static const bool capturing = std::getenv("OPENMW_OSG_STATS_FILE") != nullptr;
            static bool reported = false;
            if (capturing && !reported)
            {
                reported = true;
                Log(Debug::Info) << "Lua UI sparse property refresh active: changedLayouts=" << requested.size()
                                 << " affectedLayoutNodes=" << updates.size();
            }
            return true;
        }

        std::string setLayer(WidgetExtension* ext, const sol::table& layout)
        {
            MyGUI::ILayer* layerNode = ext->widget()->getLayer();
            std::string_view currentLayer;
            if (layerNode)
                currentLayer = layerNode->getName();
            std::string newLayer = layout.get_or(LayoutKeys::layer, std::string());
            if (!newLayer.empty() && !MyGUI::LayerManager::getInstance().isExist(newLayer))
                throw std::logic_error(std::string("Layer ") + newLayer + " doesn't exist");
            else if (newLayer != currentLayer)
            {
                MyGUI::LayerManager::getInstance().attachToLayerNode(newLayer, ext->widget());
            }
            return newLayer;
        }
    }

    std::map<Element*, std::shared_ptr<Element>> Element::sMenuElements;
    std::map<Element*, std::shared_ptr<Element>> Element::sGameElements;

    Element::Element(sol::table layout, sol::optional<sol::table> options)
        : mRoot(nullptr)
        , mLayout(std::move(layout))
        , mLayer()
        , mState(Element::New)
    {
        if (options.has_value())
        {
            mNoWarnUnused = options->get_or("noWarnUnused", false);
        }
    }

    std::shared_ptr<Element> Element::make(sol::table layout, bool menu, sol::optional<sol::table> options)
    {
        std::shared_ptr<Element> ptr(new Element(std::move(layout), std::move(options)));
        auto& container = menu ? sMenuElements : sGameElements;
        container[ptr.get()] = ptr;
        return ptr;
    }

    void Element::erase(Element* element)
    {
        element->destroy();
        sMenuElements.erase(element);
        sGameElements.erase(element);
    }

    const std::vector<std::string_view>& Element::allLayoutProperties()
    {
        return LayoutKeys::allKeys;
    }

    void Element::create(uint64_t depth)
    {
        if (mState == New)
        {
            LayoutUpdateScope layoutUpdate(mRoot);
            assert(!mRoot);
            mRoot = createWidget(layout(), true, depth);
            mLayer = setLayer(mRoot, layout());
            updateRootCoord(mRoot);
            mState = Created;
            checkWarnings();
        }
    }

    void Element::requestUpdate(const sol::table* changedLayouts, const std::function<void()>& enqueue)
    {
        if (mState != Created && mState != Update)
            return;
        const bool newRequest = mState == Created;
        std::vector<sol::main_table> incoming;
        if (changedLayouts)
        {
            lua_State* lua = changedLayouts->lua_state();
            const Detail::RestoreStack restore(lua);
            changedLayouts->push(lua);
            if (!lua_istable(lua, -1))
                throw std::logic_error("updateProperties expects an array of layout tables");
            const size_t size = lua_rawlen(lua, -1);
            if (size == 0)
                return;
            incoming.reserve(size);
            for (size_t i = 1; i <= size; ++i)
            {
                auto layout = changedLayouts->raw_get<sol::object>(i);
                if (layout.get_type() != sol::type::table)
                    throw std::logic_error("updateProperties expects an array of layout tables");
                incoming.emplace_back(layout.as<sol::table>());
            }
        }
        // addAction can reject reentrant requests. Only commit the state after
        // scheduling succeeds, and never promote a queued full refresh to partial.
        if (newRequest)
            enqueue();
        if (!changedLayouts || mRetryFullUpdate || !Misc::useSparseUiUpdates())
            mChangedLayouts.clear();
        else if (newRequest)
            mChangedLayouts.swap(incoming);
        else if (!mChangedLayouts.empty())
            mChangedLayouts.insert(mChangedLayouts.end(), std::make_move_iterator(incoming.begin()),
                std::make_move_iterator(incoming.end()));
        mState = Update;
    }

    void Element::update()
    {
        if (mState != Update)
            return;
        try
        {
            LayoutUpdateScope layoutUpdate(mRoot);
            assert(mRoot);
            // Consume the request once, including if updating the layout throws.
            std::vector<sol::main_table> changedLayouts;
            changedLayouts.swap(mChangedLayouts);
            if (mRoot->widget()->getTypeName() != widgetType(layout()))
            {
                // A root element can either be attached to another UI element or directly to a layer. Keep the
                // parent information before destroying the old widget: mRoot is no longer valid afterwards.
                WidgetExtension* oldRoot = mRoot;
                WidgetExtension* parent = oldRoot->getParent();
                if (parent == nullptr)
                {
                    destroyRoot(oldRoot);
                    try
                    {
                        mRoot = createWidget(layout(), true, 0);
                    }
                    catch (...)
                    {
                        mRoot = nullptr;
                        mState = New;
                        throw;
                    }
                }
                else
                {
                    auto children = parent->children();
                    auto it = std::find(children.begin(), children.end(), oldRoot);
                    assert(it != children.end());

                    destroyRoot(oldRoot);
                    try
                    {
                        mRoot = createWidget(layout(), true, 0);
                        *it = mRoot;
                    }
                    catch (...)
                    {
                        // Remove the old root from its parent's children even if we couldn't replace it.
                        children.erase(it);
                        parent->setChildren(children);
                        mRoot = nullptr;
                        mState = New;
                        throw;
                    }
                    parent->setChildren(children);
                }
            }
            else
            {
                const sol::table current = layout();
                if (changedLayouts.empty() || !updateChangedProperties(mRoot, current, changedLayouts))
                    updateWidget(mRoot, current, 0, canRetainAttachments(current, mRoot));
            }
            mLayer = setLayer(mRoot, layout());
            updateRootCoord(mRoot);
            mState = Created;
            mRetryFullUpdate = false;
            checkWarnings();
        }
        catch (...)
        {
            // A surviving root may already contain earlier writes. Once its
            // input is repaired the next request must reapply the whole layout.
            // A failed root replacement has already selected the New state.
            if (mRoot && mState == Update)
            {
                mRetryFullUpdate = true;
                mState = Created;
            }
            throw;
        }
    }

    void Element::destroy()
    {
        if (mState != Destroyed)
        {
            if (mRoot != nullptr)
            {
                // If someone decided to destroy an element used as another element's content, we need to detach it
                // first so the parent doesn't end up holding a stale pointer
                if (WidgetExtension* parent = mRoot->getParent())
                    parent->detachChildrenIf([&](WidgetExtension* child) { return child == mRoot; });
                destroyRoot(mRoot);
                mRoot = nullptr;
            }
            mChangedLayouts.clear();
            mLayout.reset();
        }
        mState = Destroyed;
    }

    void Element::checkWarnings()
    {
        if (mNoWarnUnused)
            // Currently unused warnings are our only warnings so we can just early out here.
            return;

        if (Settings::lua().mLuaDebug)
        {
            assert(mRoot);
            WidgetExtension::Warnings warnings;
            mRoot->collectWarnings(warnings, 0, true);
            for (const auto& warning : warnings)
                Log(Debug::Warning) << warning;
        }
        else if (!mWarnedOnce)
        {
            mWarnedOnce = true;
            WidgetExtension::Warnings dummy;
            if (mRoot->collectWarnings(dummy, 0, false))
            {
                Log(Debug::Warning) << "Warning generated while parsing layouts of a(n) " + mRoot->diagnosticName() + ". Set 'lua "
                                       "debug=true' in the [Lua] section of settings.cfg to enable detailed warnings.";
            }
        }
    }
}
