#ifndef OPENMW_LUAUI_SCRIPTSETTINGS
#define OPENMW_LUAUI_SCRIPTSETTINGS

#include <memory>
#include <string>

namespace LuaUi
{
    class LuaAdapter;
    struct Element;
    struct ScriptSettingsPage
    {
        std::string mName;
        std::string mSearchHints;
        std::shared_ptr<Element> mElement;
        bool mControls = false;
    };
    size_t scriptSettingsPageCount();
    ScriptSettingsPage scriptSettingsPageAt(size_t index);
    void ensureScriptSettingsPage(size_t index);
    void attachPageAt(size_t index, LuaAdapter* adapter);
    void attachControlsPage(LuaAdapter* adapter);
    void setControlsPageVisible(bool visible);
    bool controlsPageVisible();
}

#endif // !OPENMW_LUAUI_SCRIPTSETTINGS
