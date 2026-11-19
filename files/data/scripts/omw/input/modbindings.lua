-- Runtime registrations, never source-code parsing or a fixed installed-mod list.
local ui = require('openmw.ui')
local menu = require('openmw.menu')
local input = require('openmw.input')
local storage = require('openmw.storage')
local core = require('openmw.core')
local util = require('openmw.util')
local async = require('openmw.async')
local I = require('openmw.interfaces')
local common = require('scripts.omw.settings.common')
local model = require('scripts.omw.input.modbindingmodel')
local bindingSection = storage.playerSection('OMWInputBindings')
local adapters, subscriptions = {}, {}
local dirty, visible, recording = true, false, nil
local filter, selectedMod, status = '', nil, ''
local conflictsOnly, showHelp = false, false
local expanded = {}
local element, navigationElement, filterElement
local filterWidth
local contentWidth, navigationWidth, pageRevision = 600, 220, -1
local page = {name = 'Mod Keybindings', controls = true, searchHints = 'keys controls conflicts shortcuts'}
local function invalidate() dirty = true end
local callback = async:callback(invalidate)
local function subscribe(section, key)
    -- Storage subscriptions survive for the menu script lifetime, including reloads of a game.
    if subscriptions[key] then return end
    subscriptions[key] = true
    section:subscribe(callback)
end
local function setValue(global, groupKey, settingKey, value)
    if global then
        core.sendGlobalEvent(common.setGlobalEvent, {groupKey=groupKey, settingKey=settingKey, value=value})
    else
        storage.playerSection(groupKey):set(settingKey, value)
    end
    dirty = true
end
adapters.inputBinding = {
    devices = {keyboard=true, mouse=true, controller=true},
    read = function(id)
        local binding = type(id) == 'string' and bindingSection:getCopy(id)
        return binding and binding.device and {binding} or {}
    end,
    write = function(id, arg, _, binding, set)
        if type(id) ~= 'string' or not arg or not arg.type or not arg.key then
            error('This input binding has no registered action/trigger')
        end
        if binding then binding.type, binding.key = arg.type, arg.key end
        bindingSection:set(id, binding)
        set(id)
    end,
    modifiers = 'any',
}
subscribe(bindingSection, "bindings")
subscribe(common.getSection(false, common.groupSectionKey), 'false:groups')
local function updateGlobalSubscription()
    if menu.getState() == menu.STATE.Running then
        subscribe(common.getSection(true, common.groupSectionKey), 'true:groups')
    else
        for key in pairs(subscriptions) do
            if key:sub(1,5) == 'true:' then subscriptions[key] = nil end
        end
    end
end
updateGlobalSubscription()
local function translate(context, key)
    return tostring(core.l10n(context)(key))
end
local function collect()
    local entries, physical, unsupported = {}, {}, {}
    local contexts = menu.getState() == menu.STATE.Running and {false, true} or {false}
    for _, global in ipairs(contexts) do
        local groups = common.getSection(global, common.groupSectionKey):asTable()
        for groupKey, group in pairs(groups) do
            subscribe(common.getSection(global, groupKey), tostring(global)..":"..groupKey)
            subscribe(common.getArgumentSection(global, groupKey), tostring(global)..":"..groupKey..":args")
            for settingKey, setting in pairs(group.settings or {}) do
                local adapter = adapters[setting.renderer]
                if adapter then
                    local value = common.getSection(global, groupKey):getCopy(settingKey)
                    local arg = common.getArgumentSection(global, groupKey):getCopy(settingKey)
                    local ok, bindings = pcall(adapter.read, value, arg)
                    if ok then
                        if type(bindings) ~= 'table' then
                            ok, bindings = false, 'Binding adapter returned no binding list'
                        else
                            for _, binding in ipairs(bindings) do
                                if type(binding) ~= 'table' or not adapter.devices[binding.device]
                                    or type(binding.button) ~= 'number' or binding.button < 0
                                    or binding.button > 65535 or binding.button ~= math.floor(binding.button) then
                                    ok, bindings = false, 'Invalid stored binding; use the mod settings to repair it'
                                    break
                                end
                            end
                        end
                    end
                    local pageInfo = I.Settings.getPageInfo(group.page)
                    local metadata = model.metadata(group, setting,
                        setting.renderer == 'inputBinding' and arg or nil, pageInfo,
                        {action=input.actions, trigger=input.triggers}, translate)
                    local entry = {
                        id=(global and 'g:' or 'p:')..groupKey..':'..settingKey,
                        group=groupKey, setting=settingKey, global=global, value=value, arg=arg,
                        name=metadata.name, owner=metadata.owner, modId=metadata.modId,
                        section=metadata.section, description=metadata.description,
                        adapter=adapter, bindings=ok and bindings or {}, metadata=setting.binding or {},
                        error=not ok and tostring(bindings) or nil,
                    }
                    entries[#entries+1] = entry
                    for index, binding in ipairs(entry.bindings) do
                        binding.owner, binding.index = entry, index
                        binding.contexts = entry.metadata.contexts or binding.contexts or adapter.contexts
                        if type(binding.contexts) ~= 'table' then binding.contexts = nil end
                        binding.modifiers = entry.metadata.modifiers or binding.modifiers or adapter.modifiers
                        if type(binding.modifiers) ~= 'string' then binding.modifiers = nil end
                        physical[#physical+1] = binding
                    end
                elseif setting.binding or (setting.renderer or ''):lower():find('key') or (setting.renderer or ''):lower():find('bind') then
                    unsupported[setting.renderer] = true
                end
            end
        end
    end
    for _, binding in ipairs(input._getBaseBindings()) do
        binding.owner = {id='base:'..binding.action, name=binding.name, owner='Base controls'}
        binding.modifiers = 'any'
        -- Native actions have differing menu rules. Without declared context, warn rather than
        -- claiming both necessarily fire. Their bindings remain editable in the adjacent tabs.
        physical[#physical+1] = binding
    end
    -- Bucket physical buttons first; unrelated keys never need a pairwise comparison.
    local buckets = {}
    for _, binding in ipairs(physical) do
        local key = binding.device..':'..tostring(binding.button)
        buckets[key] = buckets[key] or {}
        table.insert(buckets[key], binding)
    end
    for _, bucket in pairs(buckets) do
        for _, binding in ipairs(bucket) do
            binding.severity, binding.conflicts = 0, {}
            local seen = {}
            for _, other in ipairs(bucket) do
                if other.owner.id ~= binding.owner.id then
                    local severity = model.conflict(binding, other)
                    if severity > 0 then
                        binding.severity = math.max(binding.severity, severity)
                        local id = other.owner.id
                        if seen[id] then
                            seen[id].severity = math.max(seen[id].severity, severity)
                        else
                            local detail = {name=other.owner.owner..' / '..other.owner.name, severity=severity, id=id}
                            binding.conflicts[#binding.conflicts+1] = detail
                            seen[id] = detail
                        end
                    end
                end
            end
            table.sort(binding.conflicts,function(a,b) return a.name==b.name and a.id<b.id or a.name<b.name end)
        end
    end
    table.sort(entries, function(a,b)
        local an, bn = a.owner..' / '..a.modId..' / '..a.group..' / '..a.name, b.owner..' / '..b.modId..' / '..b.group..' / '..b.name
        return an == bn and a.id < b.id or an < bn
    end)
    return entries, unsupported
end
local muted = util.color.rgb(0.66, 0.64, 0.58)
local heading = util.color.rgb(0.91, 0.88, 0.78)
local red = util.color.rgb(1, 0.32, 0.27)
local amber = util.color.rgb(1, 0.7, 0.25)
local function text(value, width, color)
    return {template=I.MWUI.templates.textParagraph,
        props={text=value, size=util.vector2(width,0), textColor=color}}
end
local function column(content, width, gap)
    return {type=ui.TYPE.Flex, props={size=util.vector2(width,0), gap=gap or 6}, content=ui.content(content)}
end
local function row(content, gap)
    return {type=ui.TYPE.Flex, props={horizontal=true, gap=gap or 8}, content=ui.content(content)}
end
local function space(height) return {props={size=util.vector2(0,height)}} end
local function line(width)
    return {template=I.MWUI.templates.horizontalLine, props={size=util.vector2(width,2), relativeSize=util.vector2(0,0)}}
end
local function button(label, action, width, color)
    return {
        template=I.MWUI.templates.box,
        content=ui.content{{template=I.MWUI.templates.padding, content=ui.content{
            text(label, math.max(1,width-8), color),
        }}},
        events={mouseClick=async:callback(function() if not recording then action() end end)},
    }
end
local controllerNames = {}
for name, label in pairs{
    A='A', B='B', X='X', Y='Y', Back='Back', Guide='Guide', Start='Start',
    LeftStick='Left stick', RightStick='Right stick', LeftShoulder='LB', RightShoulder='RB',
    DPadUp='D-pad up', DPadDown='D-pad down', DPadLeft='D-pad left', DPadRight='D-pad right',
    Misc1='Misc', Paddle1='Paddle 1', Paddle2='Paddle 2', Paddle3='Paddle 3', Paddle4='Paddle 4', Touchpad='Touchpad',
} do
    local code = input.CONTROLLER_BUTTON[name]
    if code then controllerNames[code] = label end
end
local function label(binding)
    if binding.device == 'keyboard' then return input.getKeyName(binding.button) end
    if binding.device == 'mouse' then
        return 'Mouse '..(({[1]='left', [2]='middle', [3]='right'})[binding.button] or tostring(binding.button))
            -- Additional mouse buttons are displayed by their physical index.
    end
    return 'Controller '..(controllerNames[binding.button] or ('button '..tostring(binding.button)))
end
local function changedView()
    ui._resetControlsPageScroll()
    dirty=true
end
local function write(entry, index, binding)
    -- Read current values again: another settings page/global event may have changed them.
    local value = common.getSection(entry.global, entry.group):getCopy(entry.setting)
    local arg = common.getArgumentSection(entry.global, entry.group):getCopy(entry.setting)
    local ok, err = pcall(entry.adapter.write, value, arg, index, binding, function(newValue)
        setValue(entry.global, entry.group, entry.setting, newValue)
    end)
    status = ok and 'Binding updated in the original mod settings.' or tostring(err)
    dirty = true
end
local function record(entry, index)
    recording = {entry=entry, index=index}
    status = 'Press and release a key or button. Escape cancels. Use Clear to unbind.'
    input._beginBindingCapture()
    dirty = true
end
local function hasConflict(entry)
    for _, binding in ipairs(entry.bindings) do
        if binding.severity > 0 then return true end
    end
    return false
end
local function matches(entry)
    return (not conflictsOnly or hasConflict(entry))
        and table.concat({entry.owner, entry.name, entry.section or '', entry.description or ''}, ' ')
            :lower():find(filter:lower(),1,true)
end
local function bindingControls(entry, width)
    local controls = {}
    if entry.error then controls[#controls+1] = text(entry.error, width, red) end
    for index, binding in ipairs(entry.bindings) do
        local color = binding.severity==2 and red or binding.severity==1 and amber or nil
        local key = entry.id..':'..index
        local capturing = recording and recording.entry.id==entry.id and recording.index==index
        local keyButton = button(capturing and 'Press a key...' or label(binding), function() record(entry,index) end,
            math.max(1,width-68), color)
        controls[#controls+1] = row{keyButton, button('Clear',function() write(entry,index,nil) end,60)}
        if binding.severity>0 then
            local warning = binding.severity==2 and 'Conflict' or 'Possible conflict'
            if binding.severity==2 then
                for _,conflict in ipairs(binding.conflicts) do
                    if conflict.severity==1 then warning='Conflicts / possible'; break end
                end
            end
            controls[#controls+1] = button(string.format('%s (%d) %s',warning,#binding.conflicts,
                expanded[key] and '-' or '+'), function() expanded[key]=not expanded[key]; dirty=true end,width,color)
            if expanded[key] then
                for _, conflict in ipairs(binding.conflicts) do
                    controls[#controls+1] = text((conflict.severity==2 and 'Conflict: ' or 'Possible conflict: ')
                        ..conflict.name,width,conflict.severity==2 and red or amber)
                end
            end
        end
    end
    if #entry.bindings==0 or entry.adapter.multiple then
        local adding = recording and recording.entry.id==entry.id and recording.index==#entry.bindings+1
        controls[#controls+1] = button(adding and 'Press a key...' or #entry.bindings==0 and 'Assign key' or 'Add binding',
            function() record(entry,#entry.bindings+1) end,width)
    end
    return column(controls,width,6)
end
local function render()
    local entries, unsupported = collect()
    local mods, order, count = {}, {}, 0
    for _, entry in ipairs(entries) do
        if matches(entry) then
            if not mods[entry.modId] then
                mods[entry.modId] = {name=entry.owner, count=0}
                order[#order+1] = entry.modId
            end
            mods[entry.modId].count = mods[entry.modId].count + 1
            count=count+1
        end
    end
    if selectedMod and not mods[selectedMod] then selectedMod=nil end
    local navWidth = navigationWidth
    if filterWidth~=navWidth then
        filterElement.layout.props.size=util.vector2(math.max(1,navWidth-8),26)
        filterElement:update()
        filterWidth=navWidth
    end
    local navigation = {
        text('Search mods or actions',navWidth,heading),
        {template=I.MWUI.templates.box, content=ui.content{
            {template=I.MWUI.templates.padding,content=ui.content{filterElement}},
        }},
        button(conflictsOnly and 'Show all shortcuts' or 'Show conflicts only',function()
            conflictsOnly=not conflictsOnly; changedView()
        end,navWidth),
        space(6),
    }
    local function modButton(id, name, n)
        -- The stable page key is the identity. Two mods can publish the same title.
        local active=selectedMod==id
        return {
            template=active and I.MWUI.templates.box or I.MWUI.templates.padding,
            content=ui.content{column({text(name,navWidth-8,active and heading or nil),
                text(n..' shortcuts',navWidth-8,muted)},navWidth-8,2)},
            events={mouseClick=async:callback(function()
                if not recording then selectedMod=id; changedView() end
            end)},
        }
    end
    navigation[#navigation+1]=modButton(nil,'All mods',count)
    for _, id in ipairs(order) do
        local mod=mods[id]
        navigation[#navigation+1]=modButton(id,mod.name,mod.count)
    end
    navigation[#navigation+1]=space(6)
    navigation[#navigation+1]=button('Refresh',invalidate,navWidth)
    navigation[#navigation+1]=button(showHelp and 'Hide help' or 'Help & coverage',function()
        showHelp=not showHelp; changedView()
    end,navWidth)

    local width=contentWidth
    local content={text(selectedMod and mods[selectedMod].name or 'Mod Keybindings',width,heading),
        text("Select a key to change it. Changes also apply in the mod's own settings.",width,muted)}
    if status~='' then content[#content+1]=text(status,width,heading) end
    if showHelp then
        content[#content+1]=text('Red: bindings with overlapping declared contexts. Amber: possible overlap where a context is unknown. Select a warning to see the other actions.',width)
        content[#content+1]=text('Base game keys are listed in conflicts; edit them in Mouse/Keyboard or Controller. This page discovers registered bindings and installed adapters automatically. Unregistered or hardcoded shortcuts cannot be listed reliably.',width,muted)
        local formats={}
        for name in pairs(unsupported) do formats[#formats+1]=name end
        table.sort(formats)
        if #formats>0 then content[#content+1]=text('Unsupported formats: '..table.concat(formats,', '),width,muted) end
    end
    content[#content+1]=space(6)
    local lastMod, lastGroup, shownCount=nil,nil,0
    for _, entry in ipairs(entries) do
        if (not selectedMod or selectedMod==entry.modId) and matches(entry) then
            if lastMod~=entry.modId then
                if lastMod then content[#content+1]=space(12) end
                if not selectedMod then content[#content+1]=text(entry.owner,width,heading) end
                lastMod,lastGroup=entry.modId,nil
            end
            if lastGroup~=entry.group then
                if entry.section and entry.section~=entry.owner then
                    content[#content+1]=text(entry.section,width,muted)
                end
                lastGroup=entry.group
            end
            content[#content+1]=line(width)
            -- Wide windows use aligned action / binding columns; narrow windows stack them.
            local stacked=width<510
            local keysWidth=stacked and width or math.min(270,math.floor(width*0.38))
            local descriptionWidth=stacked and width or width-keysWidth-20
            local details=column({text(entry.name,descriptionWidth,heading),
                text(entry.description or 'No description provided by this mod.',descriptionWidth,muted)},descriptionWidth,4)
            local controls=bindingControls(entry,keysWidth)
            content[#content+1]=stacked and column({details,controls},width,8) or row({details,controls},20)
            content[#content+1]=space(8)
            shownCount=shownCount+1
        end
    end
    if shownCount==0 then content[#content+1]=text('No matching shortcuts. Try another search or show all shortcuts.',width,muted) end
    content[#content+1]=line(width)
    content[#content+1]=text(shownCount..' shortcuts shown',width,muted)
    local function update(current, content, w)
        local layout=column(content,w,8)
        layout.props.position=util.vector2(6,6)
        if current then current.layout=layout; current:update(); return current end
        return ui.create(layout,{deferUntilAttached=true})
    end
    element=update(element,content,width)
    navigationElement=update(navigationElement,navigation,navWidth)
    page.element, page.navigationElement=element,navigationElement
    dirty=false
end
filterElement=ui.create({template=I.MWUI.templates.textEditLine, props={text=filter,size=util.vector2(210,26)},
    events={textChanged=async:callback(function(value)
        filter=value; filterElement.layout.props.text=value; selectedMod=nil; changedView()
    end)}},{deferUntilAttached=true})
render()
ui.registerSettingsPage(page)
return {
    interfaceName='ModKeybindings',
    interface={
        version=1,
        registerAdapter=function(renderer, adapter)
            assert(type(renderer)=='string' and type(adapter)=='table' and type(adapter.read)=='function'
                and type(adapter.write)=='function' and type(adapter.devices)=='table', 'Invalid keybinding adapter')
            adapters[renderer]=adapter
            dirty=true
        end,
    },
    engineHandlers={
    onStateChanged=function()
        updateGlobalSubscription()
        if recording then input._cancelBindingCapture(); recording=nil end
        dirty=true
    end,
    onFrame=function()
        local shown=ui._controlsPageVisible()
        if shown and not visible then dirty=true end
        visible=shown
        if not shown then
            if recording then input._cancelBindingCapture(); recording=nil; status=''; dirty=true end
            return
        end
        if recording then
            local binding=input._takeCapturedBinding()
            if binding then
                local target=recording
                recording=nil
                if binding.device=='keyboard' and binding.button==input.KEY.Escape then
                    status='Binding unchanged.'
                elseif not target.entry.adapter.devices[binding.device] then
                    status='This mod does not support that input device.'
                else
                    write(target.entry,target.index,binding)
                end
                dirty=true
            end
        end
        local width, navWidth=ui._controlsPageWidth(false),ui._controlsPageWidth(true)
        local revision=I.Settings.getPageRevision()
        if width~=contentWidth or navWidth~=navigationWidth or revision~=pageRevision then
            contentWidth,navigationWidth,pageRevision=width,navWidth,revision
            dirty=true
        end
        if dirty then render() end
    end},
}
