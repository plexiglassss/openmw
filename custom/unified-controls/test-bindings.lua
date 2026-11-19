local root=arg[1]
package.path=root..'/files/data/?.lua;'..package.path
local model=require('scripts.omw.input.modbindingmodel')
local function binding(contexts,modifiers,button,device)
    return {contexts=contexts,modifiers=modifiers,button=button or 4,device=device or 'keyboard'}
end
assert(model.conflict(binding({'barter'},'any'),binding({'barter'},'ctrl'))==2)
assert(model.conflict(binding({'gameplay'},'any'),binding({'barter'},'any'))==0)
assert(model.conflict(binding(nil,'any'),binding({'barter'},'any'))==1)
assert(model.conflict(binding({'barter'},'none'),binding({'barter'},'ctrl'))==0)
assert(model.conflict(binding({'barter'},'any'),binding({'barter'},'any',5))==0)
assert(model.conflict(binding({'barter'},'any'),binding({'barter'},'any',4,'controller'))==0)
local function copy(t)
    if type(t)~='table' then return t end
    local out={};for k,v in pairs(t) do out[k]=copy(v) end;return out
end
local running=false
local sections={}
local function section(key)
    if sections[key] then return sections[key] end
    local s={values={},listeners={}}
    function s:get(k) return self.values[k] end
    function s:getCopy(k) return copy(self.values[k]) end
    function s:asTable() assert(key:sub(1,2)~='g:' or running, 'global storage inactive'); return copy(self.values) end
    function s:set(k,v)
        self.values[k]=copy(v)
        for _,callback in ipairs(self.listeners) do callback(key:sub(3),k) end
    end
    function s:subscribe(fn) self.listeners[#self.listeners+1]=fn end
    sections[key]=s;return s
end
local storage={playerSection=function(k) return section('p:'..k) end,globalSection=function(k)return section('g:'..k)end}
local pages={Test={key='Test',l10n='page',name='TestPageName'}, G={key='G',l10n='page',name='Global Mod'}}
local pageRevision=0
local I={Settings={getPageInfo=function(id)return copy(pages[id])end,getPageRevision=function()return pageRevision end},MWUI={templates=setmetatable({},{__index=function(_,name)return {testTemplate=name}end})}}
local async={callback=function(_,f)return f end}
local shown,capturing,captured,registered=true,false,nil,nil
local input={CONTROLLER_BUTTON={A=0,B=1},actions={},triggers={TestTrigger={l10n='action',name='ActionName',description='ActionDescription'}},KEY={Escape=41},getKeyName=function(n)return 'Key '..n end,
    _beginBindingCapture=function() capturing=true end,
    _cancelBindingCapture=function()capturing=false end,
    _takeCapturedBinding=function()local v=captured;captured=nil;return v end,
    _getBaseBindings=function()return {{action=1,name='Activate',device='keyboard',button=4}}end}
local contentWidth,navigationWidth,updates,resets=700,220,0,0
local ui={TYPE={Flex='flex',TextEdit='edit'},content=function(t)return t end,
    create=function(layout) return {layout=layout,update=function()updates=updates+1 end} end,
    registerSettingsPage=function(page)registered=page end,_controlsPageVisible=function()return shown end,
    _controlsPageWidth=function(nav)return nav and navigationWidth or contentWidth end,
    _resetControlsPageScroll=function()resets=resets+1 end}
local util={vector2=function(x,y)return{x=x,y=y}end,color={rgb=function(r,g,b)return{r=r,g=g,b=b}end}}
package.preload['openmw.menu']=function()return {STATE={Running='running',NoGame='none'},getState=function()return running and 'running' or 'none' end}end
local events={}
local translations={page={TestPageName='Actual Test Mod'},action={ActionName='Toggle test feature',ActionDescription='Toggles a feature during barter.'},test={SettingDescription='Description from the setting.'}}
local core={l10n=function(context)return function(k)return (translations[context] or {})[k] or k end end,
    sendGlobalEvent=function(name,data) events[#events+1]={name=name,data=data} end}
for name,value in pairs{storage=storage,interfaces=I,async=async,input=input,ui=ui,util=util,core=core} do
    package.preload['openmw.'..name]=function()return value end
end
local common=require('scripts.omw.settings.common')
common.registerGroup{key='SettingsTest',page='Test',l10n='test',name='Test Mod',permanentStorage=true,settings={
    {key='action',renderer='inputBinding',name='Do thing',default='test-id',argument={key='TestTrigger',type='trigger'},binding={contexts={'barter'}}},
}}
assert(storage.playerSection('OmwSettingGroups'):get('SettingsTest').settings.action.binding.contexts[1]=='barter')
storage.playerSection('OMWInputBindings'):set('test-id',{device='keyboard',button=4,type='trigger',key='TestTrigger'})
local manager=require('scripts.omw.input.modbindings')
I.ModKeybindings=manager.interface
assert(registered.controls and registered.element and registered.navigationElement)
manager.engineHandlers.onFrame()
local function strings(layout,out)
    out=out or {}
    if layout.layout then layout=layout.layout end
    if layout.props and layout.props.text then out[#out+1]=layout.props.text end
    for _,child in ipairs(layout.content or {}) do strings(child,out) end
    return out
end
local function findButton(layout,label)
    if layout.layout then layout=layout.layout end
    if layout.events and layout.events.mouseClick then
        for _,s in ipairs(strings(layout)) do if s==label then return layout.events.mouseClick end end
    end
    for _,child in ipairs(layout.content or {}) do local found=findButton(child,label);if found then return found end end
end
assert(not table.concat(strings(registered.element),'\n'):find('Base controls / Activate',1,true))
findButton(registered.element,'Possible conflict (1) +')()
manager.engineHandlers.onFrame()
local initialText=table.concat(strings(registered.element),'\n')
assert(initialText:find('Base controls / Activate',1,true))
assert(initialText:find('Actual Test Mod',1,true))
assert(initialText:find('Toggle test feature',1,true))
assert(initialText:find('Toggles a feature during barter.',1,true))
findButton(registered.element,'Key 4')()
assert(capturing)
captured={device='keyboard',button=8};manager.engineHandlers.onFrame()
local v=storage.playerSection('OMWInputBindings'):get('test-id')
assert(v.button==8 and v.key=='TestTrigger' and v.type=='trigger')
assert(storage.playerSection('SettingsTest'):get('action')=='test-id')
-- Original settings change propagates back, without rescan/restarting the game.
storage.playerSection('OMWInputBindings'):set('test-id',{device='controller',button=0,type='trigger',key='TestTrigger'})
manager.engineHandlers.onFrame()
assert(findButton(registered.element,'Controller A'))
findButton(registered.element,'Clear')()
manager.engineHandlers.onFrame()
assert(storage.playerSection('OMWInputBindings'):get('test-id')==nil)
assert(findButton(registered.element,'Assign key'))
findButton(registered.element,'Assign key')()
captured={device='keyboard',button=41};manager.engineHandlers.onFrame()
assert(storage.playerSection('OMWInputBindings'):get('test-id')==nil)
-- A new group appears after runtime registration, with the same originals.
common.registerGroup{key='SettingsNew',page='Test',l10n='test',name='New Mod',permanentStorage=true,settings={
    {key='action',renderer='inputBinding',name='New action',default='new-id',argument={key='NewTrigger',type='trigger'}},
}}
manager.engineHandlers.onFrame()
assert(table.concat(strings(registered.element),'\n'):find('New action',1,true))
-- Groups with generic labels still belong to the page, never become mod identities.
local navigation=table.concat(strings(registered.navigationElement),'\n')
assert(navigation:find('Actual Test Mod',1,true) and not navigation:find('New Mod',1,true))
local count=0;for _,s in pairs(sections) do count=count+#s.listeners end
local stableUpdates=updates
for i=1,50 do manager.engineHandlers.onFrame() end
assert(updates==stableUpdates, 'Idle page must not rebuild')
local count2=0;for _,s in pairs(sections) do count2=count2+#s.listeners end
assert(count==count2)
-- Hiding the page cancels a pending recorder and does no repeated rendering.
findButton(registered.element,'Assign key')();shown=false;manager.engineHandlers.onFrame();assert(not capturing)
-- Global discovery is deferred until running; reload resubscribes after callback cleanup.
local group={key='SettingsGlobal',page='G',l10n='g',name='Global Mod',settings={key={key='key',renderer='inputBinding',name='Global key',argument={key='G',type='trigger'}}}}
storage.globalSection('OmwSettingGroups'):set('SettingsGlobal',group)
storage.globalSection('SettingsGlobal'):set('key','global-id')
storage.globalSection('SettingsGlobalArguments'):set('key',{key='G',type='trigger'})
running=true;shown=true;manager.engineHandlers.onStateChanged();manager.engineHandlers.onFrame()
assert(table.concat(strings(registered.element),'\n'):find('Global Mod',1,true))
assert(table.concat(strings(registered.element),'\n'):find('Global key',1,true))
-- Register a one-value custom format to verify the global event writeback path.
I.ModKeybindings.registerAdapter('numericKey',{devices={keyboard=true},modifiers='any',
    read=function(v)return v and {{device='keyboard',button=v}} or {}end,
    write=function(_,_,_,b,set)set(b and b.button or nil)end})
group.settings.key.renderer='numericKey';storage.globalSection('OmwSettingGroups'):set('SettingsGlobal',group)
storage.globalSection('SettingsGlobal'):set('key',19);manager.engineHandlers.onFrame()
findButton(registered.element,'Key 19')();captured={device='keyboard',button=20};manager.engineHandlers.onFrame()
assert(events[#events].data.groupKey=='SettingsGlobal' and events[#events].data.value==20)
assert(storage.globalSection('SettingsGlobal'):get('key')==19) -- no illegal menu write
running=false;for k,s in pairs(sections)do if k:sub(1,2)=='g:'then s.listeners={}end end
manager.engineHandlers.onStateChanged();running=true;manager.engineHandlers.onStateChanged();manager.engineHandlers.onFrame()
assert(#storage.globalSection('OmwSettingGroups').listeners==1)
assert(#storage.globalSection('SettingsGlobal').listeners==1)
-- Broken stored values/adapters remain isolated to their row.
storage.playerSection('OMWInputBindings'):set('test-id',{device='keyboard',button='bad'})
manager.engineHandlers.onFrame()
assert(table.concat(strings(registered.element),'\n'):find('Invalid stored binding',1,true))
assert(not pcall(I.ModKeybindings.registerAdapter,'bad',{read=function()end,write=function()end}))
-- Late page registration changes the visible owner without changing settings.
pages.Test.name='Renamed Test Mod';pageRevision=pageRevision+1;manager.engineHandlers.onFrame()
assert(table.concat(strings(registered.navigationElement),'\n'):find('Renamed Test Mod',1,true))
findButton(registered.navigationElement,'Renamed Test Mod')();manager.engineHandlers.onFrame()
assert(not table.concat(strings(registered.element),'\n'):find('Global key',1,true))
assert(resets>0)
-- Search includes descriptions, and keeps the text editor as the same Element.
local function findEditor(node)
    if node.layout and node.layout.events and node.layout.events.textChanged then return node end
    if node.layout then node=node.layout end
    for _,child in ipairs(node.content or {}) do local result=findEditor(child);if result then return result end end
end
local editor=findEditor(registered.navigationElement)
editor.layout.events.textChanged('Toggles a feature');manager.engineHandlers.onFrame()
assert(findEditor(registered.navigationElement)==editor)
assert(table.concat(strings(registered.element),'\n'):find('Toggle test feature',1,true))
assert(not table.concat(strings(registered.element),'\n'):find('New action',1,true))
editor.layout.events.textChanged('');manager.engineHandlers.onFrame()
-- Native viewport resizing changes wrapping widths without mutating bindings.
contentWidth=340;navigationWidth=150;manager.engineHandlers.onFrame()
assert(registered.element.layout.props.size.x==340)
assert(editor.layout.props.size.x==142)
-- Explicit setting descriptions use the group's namespace; canonical actions use theirs.
local m=model.metadata({page='X',l10n='test',name='General'},
    {key='k',name='Generic setting',description='SettingDescription'},
    {type='trigger',key='TestTrigger'},{name='TestPageName',l10n='page'},
    {trigger=input.triggers},function(context,key)return core.l10n(context)(key)end)
assert(m.owner=='Actual Test Mod' and m.name=='Toggle test feature' and m.description=='Description from the setting.')
assert(m.section=='General' and m.modId=='X')
-- Distinct pages with identical titles must remain distinct selectable mods.
pages.G.name='Renamed Test Mod';pageRevision=pageRevision+1;manager.engineHandlers.onFrame()
local duplicateCount=0
for _,value in ipairs(strings(registered.navigationElement)) do if value=='Renamed Test Mod' then duplicateCount=duplicateCount+1 end end
assert(duplicateCount==2)
-- Conflict-only view can be cleared and descriptions absent from metadata are not invented.
findButton(registered.navigationElement,'Show conflicts only')();manager.engineHandlers.onFrame()
assert(table.concat(strings(registered.element),'\n'):find('No matching shortcuts',1,true))
findButton(registered.navigationElement,'Show all shortcuts')();manager.engineHandlers.onFrame()
assert(table.concat(strings(registered.element),'\n'):find('No description provided by this mod.',1,true))
-- Mixed known/unknown overlaps retain separate severity and readable owners.
local conflicted=storage.playerSection('OmwSettingGroups'):getCopy('SettingsNew')
conflicted.settings.action.binding={contexts={'barter'}}
storage.playerSection('OmwSettingGroups'):set('SettingsNew',conflicted)
storage.playerSection('OMWInputBindings'):set('test-id',{device='keyboard',button=4})
storage.playerSection('OMWInputBindings'):set('new-id',{device='keyboard',button=4})
manager.engineHandlers.onFrame()
findButton(registered.element,'Conflicts / possible (2) +')();manager.engineHandlers.onFrame()
local mixedText=table.concat(strings(registered.element),'\n')
assert(mixedText:find('Conflict: Renamed Test Mod / New action',1,true))
assert(mixedText:find('Possible conflict: Base controls / Activate',1,true))
-- Native registry lookup must never receive a missing/non-string key.
local strict=setmetatable({},{__index=function(_,key) assert(type(key)=='string');return nil end})
for _,argument in ipairs{true,12,'bad',{type='trigger'},{type='trigger',key=42}} do
    assert(model.metadata({page='X',l10n='test'}, {key='k'},argument,nil,{trigger=strict},function(_,key)return key end).name=='k')
end
-- The native MyGUI fixture consumes the actual layouts emitted by this script.
if modBindingsLayoutProbe then
    translations.action.ActionDescription='Opens the detailed equipment comparison for the selected inventory item, including every registered modifier and the currently equipped alternative. This intentionally long description must remain readable when the settings window is narrow.'
    findButton(registered.navigationElement,'Refresh')()
    for _,width in ipairs{340,700,1000} do
        contentWidth=width;manager.engineHandlers.onFrame()
        modBindingsLayoutProbe(registered.element.layout,registered.navigationElement.layout,width,navigationWidth)
    end
end
-- Reset uses registered defaults, keeps unrelated settings and honours its
-- confirmed scope even when a search hides some of that mod's shortcuts.
pages.G.name='Global Mod'; pageRevision=pageRevision+1
group.settings.key.default=31;storage.globalSection('OmwSettingGroups'):set('SettingsGlobal',group)
storage.playerSection('SettingsTest'):set('unrelated',false)
manager.engineHandlers.onFrame()
findButton(registered.navigationElement,'Renamed Test Mod')();manager.engineHandlers.onFrame()
local beforeResetEvents=#events
findButton(registered.element,'Reset controls')();manager.engineHandlers.onFrame()
assert(storage.playerSection('OMWInputBindings'):get('test-id').button==4)
findButton(registered.element,'Cancel')();manager.engineHandlers.onFrame()
assert(storage.playerSection('OMWInputBindings'):get('test-id').button==4)
findButton(registered.element,'Reset controls')();manager.engineHandlers.onFrame()
findButton(registered.element,'Restore defaults')();manager.engineHandlers.onFrame()
assert(storage.playerSection('OMWInputBindings'):get('test-id')==nil)
assert(storage.playerSection('OMWInputBindings'):get('new-id')==nil)
assert(storage.playerSection('SettingsTest'):get('action')=='test-id')
assert(storage.playerSection('SettingsTest'):get('unrelated')==false)
assert(#events==beforeResetEvents,'Selected mod reset must not alter a different global mod')
-- The search filters rows, not the explicitly stated all-mod reset scope.
editor.layout.events.textChanged('New action');manager.engineHandlers.onFrame()
findButton(registered.element,'Reset controls')();manager.engineHandlers.onFrame()
assert(table.concat(strings(registered.element),'\n'):find('for all listed mods?',1,true))
findButton(registered.element,'Restore defaults')();manager.engineHandlers.onFrame()
assert(events[#events].data.groupKey=='SettingsGlobal' and events[#events].data.value==31)
assert(storage.globalSection('SettingsGlobal'):get('key')==19,'Global writes must use the event path')
editor.layout.events.textChanged('');manager.engineHandlers.onFrame()
-- Optional adapters can restore defaults held outside the setting value.
local adapterResets=0
I.ModKeybindings.registerAdapter('numericKey',{devices={keyboard=true},read=function(v)
    return v and {{device='keyboard',button=v}} or {}
end,write=function(_,_,_,b,set)set(b and b.button)end,
reset=function(value,arg,default,set)
    assert(value==19 and default==31);adapterResets=adapterResets+1;set(default)
end})
manager.engineHandlers.onFrame()
findButton(registered.navigationElement,'Global Mod')();manager.engineHandlers.onFrame()
findButton(registered.element,'Reset controls')();manager.engineHandlers.onFrame()
findButton(registered.element,'Restore defaults')();manager.engineHandlers.onFrame()
assert(adapterResets==1)
-- Closing the panel discards an unconfirmed reset and keeps idle frames cheap.
findButton(registered.element,'Reset controls')();manager.engineHandlers.onFrame()
shown=false;manager.engineHandlers.onFrame();shown=true;manager.engineHandlers.onFrame()
assert(not findButton(registered.element,'Restore defaults'))
local idleUpdates=updates
for i=1,50 do manager.engineHandlers.onFrame() end
assert(updates==idleUpdates)
assert(not pcall(I.ModKeybindings.registerAdapter,'invalidReset',{read=function()end,
    write=function()end,devices={},reset=true}))
-- Default nil is meaningful, failures remain isolated, and confirmation freezes
-- identities while fresh registrations/default values can still change.
pages.Reset={key='Reset',name='Reset edge cases',l10n='test'}
I.ModKeybindings.registerAdapter('resetValue',{devices={keyboard=true},
    read=function(v)return v and {{device='keyboard',button=v}} or {}end,
    write=function(_,_,_,b,set)set(b and b.button)end})
I.ModKeybindings.registerAdapter('resetError',{devices={keyboard=true},
    read=function()return {}end,write=function()end,reset=function()error('Reset fixture failure')end})
common.registerGroup{key='SettingsReset',page='Reset',l10n='test',name='Reset edge cases',permanentStorage=true,settings={
    {key='clear',name='Clear default',renderer='resetValue'},
    {key='fresh',name='Changed default',renderer='resetValue',default=10},
    {key='missing',name='Removed setting',renderer='resetValue',default=1},
    {key='bad',name='Missing default ID',renderer='inputBinding',argument={key='B',type='trigger'}},
    {key='throws',name='Failing adapter',renderer='resetError'},
}}
local resetStore=storage.playerSection('SettingsReset')
resetStore:set('clear',12);resetStore:set('fresh',13);resetStore:set('bad','bad-id')
storage.playerSection('OMWInputBindings'):set('bad-id',{device='keyboard',button=9})
manager.engineHandlers.onFrame()
findButton(registered.navigationElement,'Reset edge cases')();manager.engineHandlers.onFrame()
findButton(registered.element,'Reset controls')();manager.engineHandlers.onFrame()
local changed=storage.playerSection('OmwSettingGroups'):getCopy('SettingsReset')
changed.settings.fresh.default=15
changed.settings.missing=nil
changed.settings.added={key='added',name='Added later',renderer='resetValue',default=1}
storage.playerSection('OmwSettingGroups'):set('SettingsReset',changed);resetStore:set('added',29)
manager.engineHandlers.onFrame()
findButton(registered.element,'Restore defaults')();manager.engineHandlers.onFrame()
assert(resetStore:get('clear')==nil and resetStore:get('fresh')==15 and resetStore:get('added')==29)
assert(resetStore:get('bad')=='bad-id' and storage.playerSection('OMWInputBindings'):get('bad-id').button==9)
assert(table.concat(strings(registered.element),'\n'):find('3 could not be reset',1,true))
-- A drifted ID is cleaned up, but a shared mapping outside this mod is protected.
changed.settings.bad.default='default-id'
storage.playerSection('OmwSettingGroups'):set('SettingsReset',changed)
storage.playerSection('OMWInputBindings'):set('default-id',{device='keyboard',button=10})
manager.engineHandlers.onFrame()
findButton(registered.element,'Reset controls')();manager.engineHandlers.onFrame()
findButton(registered.element,'Restore defaults')();manager.engineHandlers.onFrame()
assert(resetStore:get('bad')=='default-id')
assert(storage.playerSection('OMWInputBindings'):get('bad-id')==nil)
assert(storage.playerSection('OMWInputBindings'):get('default-id')==nil)
resetStore:set('bad','test-id')
storage.playerSection('OMWInputBindings'):set('test-id',{device='keyboard',button=6})
manager.engineHandlers.onFrame()
findButton(registered.element,'Reset controls')();manager.engineHandlers.onFrame()
findButton(registered.element,'Restore defaults')();manager.engineHandlers.onFrame()
assert(resetStore:get('bad')=='test-id' and storage.playerSection('OMWInputBindings'):get('test-id').button==6)
print('Binding discovery, synchronization, capture, reset scopes/defaults, global events and idle subscriptions: PASS')
