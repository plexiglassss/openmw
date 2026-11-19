-- Real Lua templates, native widgets, adapters, hit testing and event callbacks.
-- Only the game/settings services are substitutes. clickLabel/wheelLabel inject
-- input at screen coordinates, never invoke a layout callback directly.
package.path=sourceRoot..'/files/data/?.lua;'..package.path
local sections={}
local function copy(value)
    if type(value)~='table' then return value end
    local result={}; for key,item in pairs(value) do result[key]=copy(item) end; return result
end
local function section(name)
    if not sections[name] then
        local s={values={},listeners={}}
        function s:get(key) return copy(self.values[key]) end
        s.getCopy=s.get
        function s:set(key,value)
            self.values[key]=copy(value)
            for _,callback in ipairs(self.listeners) do callback(name,key) end
        end
        function s:subscribe(callback) self.listeners[#self.listeners+1]=callback end
        function s:asTable() return copy(self.values) end
        sections[name]=s
    end
    return sections[name]
end
local pages={}
local I={MWUI={templates={}},Settings={getPageRevision=function()return 0 end,
    getPageInfo=function(id)return pages[id]end}}
local input={KEY={Escape=41},CONTROLLER_BUTTON={},actions={},triggers={},capturing=false,
    getKeyName=function(key)return 'Key '..key end,_getBaseBindings=function()return {} end}
input._beginBindingCapture=function()input.capturing=true end
input._cancelBindingCapture=function()input.capturing=false end
input._takeCapturedBinding=function()
    local value=input.captured; input.captured=nil
    if value then input.capturing=false end
    return value
end
local ui={TYPE={Flex='LuaFlex',Text='LuaText',TextEdit='LuaTextEdit',Container='LuaContainer',
    Image='LuaImage',Widget='LuaWidget'},content=content,create=element,registerSettingsPage=registerPage,
    _getDefaultFontSize=function()return 16 end,_getMenuTransparency=function()return .8 end,
    texture=function(options)return texture(options.path)end,_controlsPageVisible=function()return true end,
    _controlsPageWidth=function(nav)return nav and 220 or 700 end,
    _resetControlsPageScroll=function()scrollTop(false)end}
local modules={util=nativeUtil,ui=ui,input=input,interfaces=I,
    async={callback=function(_,fn)return nativeCallback(fn)end},
    core={getGMST=function()return '223,201,159' end,l10n=function()return function(key)return key end end},
    menu={STATE={Running=1},getState=function()return 0 end},
    storage={playerSection=section,globalSection=function(name)return section('global:'..name)end}}
for name,value in pairs(modules) do package.loaded['openmw.'..name]=value end
for _,part in ipairs{'text','textEdit','space','borders'} do
    require('scripts.omw.mwui.'..part)(I.MWUI.templates)
end
local common=require('scripts.omw.settings.common')
for mod=1,20 do
    local id=string.format('Mod%02d',mod)
    pages[id]={key=id,name=id,l10n='test'}
    local settings={}
    for key=1,6 do
        local binding=id..'Key'..key
        settings[#settings+1]={key=binding,name='Action '..key,renderer='inputBinding',default=binding,
            description='A wrapped action description that remains readable and clickable after scrolling.',
            argument={key=binding,type='trigger'}}
        section('OMWInputBindings'):set(binding,{device='keyboard',button=10+key,type='trigger',key=binding})
    end
    common.registerGroup{key='Settings'..id,page=id,l10n='test',name=id,
        permanentStorage=true,settings=settings}
end
local manager=require('scripts.omw.input.modbindings')
attachPage()
local function frame()manager.engineHandlers.onFrame();flush()end
frame()
clickLabel(true,'');typeText('Mod04');frame()
assert(hasLabel(false,'Mod04') and not hasLabel(false,'Mod01'),'Search must accept real text input')
backspace(5);frame()
clickLabel(true,'Mod02');frame()
assert(hasLabel(false,'Mod02') and not hasLabel(false,'Mod01'), 'Clicking a mod label must select that mod')
clickLabel(false,'Key 11')
assert(input.capturing,'Clicking a key label must begin binding capture')
input.captured={device='keyboard',button=28};frame()
assert(section('OMWInputBindings'):get('Mod02Key1').button==28)
assert(hasLabel(false,'Key 28'))
wheelLabel(false,'Action 1',-1)
assert(offset(false)==-50 and offset(true)==0,'Right panel must scroll once independently')
wheelLabel(true,'Mod02',-1)
assert(offset(true)==-50 and offset(false)==-50,'Left panel must scroll once independently')
scrollTop(true);scrollTop(false)
scrollBottom(true)
clickLabel(true,'Mod20');frame()
assert(hasLabel(false,'Mod20') and not hasLabel(false,'Mod02'),'A scrolled mod must remain selectable')
scrollBottom(false)
clickLabel(false,'Key 16')
assert(input.capturing,'A scrolled key must remain clickable')
input.captured={device='keyboard',button=41};frame()
assert(section('OMWInputBindings'):get('Mod20Key6').button==16,'Escape must preserve the old binding')
scrollTop(false)
clickLabel(false,'Reset controls');frame()
clickLabel(false,'Cancel');frame()
assert(section('OMWInputBindings'):get('Mod20Key1').button==11)
clickLabel(false,'Reset controls');frame()
clickLabel(false,'Restore defaults');frame()
for key=1,6 do assert(section('OMWInputBindings'):get('Mod20Key'..key)==nil) end
assert(section('OMWInputBindings'):get('Mod02Key1').button==28)
