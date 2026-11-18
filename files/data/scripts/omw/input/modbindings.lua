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
local filter, pageNumber, status = '', 1, ''
local pageSize = 30
local element, filterElement
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
local function localized(group, key)
    return tostring(core.l10n(group.l10n)(key or group.key))
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
                    local entry = {
                        id=(global and 'g:' or 'p:')..groupKey..':'..settingKey,
                        group=groupKey, setting=settingKey, global=global, value=value, arg=arg,
                        name=localized(group, setting.name), owner=localized(group, group.name),
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
                elseif setting.binding or setting.renderer:lower():find('key') or setting.renderer:lower():find('bind') then
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
            for _, other in ipairs(bucket) do
                if other.owner.id ~= binding.owner.id then
                    local severity = model.conflict(binding, other)
                    if severity > 0 then
                        binding.severity = math.max(binding.severity, severity)
                        binding.conflicts[#binding.conflicts+1] = other.owner.owner..' / '..other.owner.name
                    end
                end
            end
        end
    end
    table.sort(entries, function(a,b)
        local an, bn = a.owner..' / '..a.name, b.owner..' / '..b.name
        return an == bn and a.id < b.id or an < bn
    end)
    return entries, unsupported
end
local function text(value, color)
    return {template=I.MWUI.templates.textParagraph, props={text=value, size=util.vector2(570,0), textColor=color}}
end
local function button(label, action, color)
    return {
        template=I.MWUI.templates.box,
        content=ui.content{{template=I.MWUI.templates.padding, content=ui.content{
            {template=I.MWUI.templates.textNormal, props={text=label,textColor=color}}
        }}},
        events={mouseClick=async:callback(function() if not recording then action() end end)},
    }
end
local function label(binding)
    if binding.device == 'keyboard' then return input.getKeyName(binding.button) end
    return binding.device..' '..tostring(binding.button)
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
local function render()
    local entries, unsupported = collect()
    local content = {
        text('Mod Keybindings'),
        text('Automatic for registered bindings and installed adapters. Red: overlapping declared contexts. Amber: possible overlap; context is unknown. Base keys are edited in Mouse and Keyboard / Controller.'),
        text('Hardcoded keys, unregistered settings and arbitrary custom formats cannot be discovered reliably.'),
        filterElement,
    }
    if status ~= '' then content[#content+1] = text(status) end
    local matches = {}
    for _, entry in ipairs(entries) do
        if (entry.owner..' '..entry.name):lower():find(filter:lower(),1,true) then matches[#matches+1]=entry end
    end
    local pages = math.max(1,math.ceil(#matches/pageSize))
    pageNumber=math.min(pageNumber,pages)
    content[#content+1]=text(string.format('%d bindings · page %d / %d',#matches,pageNumber,pages))
    content[#content+1]={type=ui.TYPE.Flex,props={horizontal=true},content=ui.content{
        button('Previous',function() pageNumber=math.max(1,pageNumber-1); dirty=true end),
        button('Next',function() pageNumber=math.min(pages,pageNumber+1); dirty=true end),
        button('Refresh',invalidate),
    }}
    for i=(pageNumber-1)*pageSize+1, math.min(#matches,pageNumber*pageSize) do
        local entry=matches[i]
        content[#content+1]=text(entry.owner..' / '..entry.name)
        if entry.error then content[#content+1]=text(entry.error) end
        for index,binding in ipairs(entry.bindings) do
            local color=binding.severity==2 and util.color.rgb(1,0.32,0.27)
                or binding.severity==1 and util.color.rgb(1,0.7,0.25) or nil
            local row={button(label(binding),function() record(entry,index) end,color),
                button('Clear',function() write(entry,index,nil) end)}
            content[#content+1]={type=ui.TYPE.Flex,props={horizontal=true},content=ui.content(row)}
            if binding.severity>0 then
                content[#content+1]=text((binding.severity==2 and 'Conflict: ' or 'Possible conflict: ')
                    ..table.concat(binding.conflicts, '; '),color)
            end
        end
        if #entry.bindings==0 or entry.adapter.multiple then
            content[#content+1]=button('Add binding',function() record(entry,#entry.bindings+1) end)
        end
        content[#content+1]={template=I.MWUI.templates.interval}
    end
    local formats={}
    for name in pairs(unsupported) do formats[#formats+1]=name end
    table.sort(formats)
    if #formats>0 then content[#content+1]=text('No adapter for: '..table.concat(formats,', ')) end
    local layout={type=ui.TYPE.Flex,props={position=util.vector2(8,8)},content=ui.content(content)}
    if element then
        element.layout=layout
        element:update()
    else
        element=ui.create(layout,{deferUntilAttached=true})
        page.element=element
    end
    dirty=false
end
filterElement=ui.create({template=I.MWUI.templates.textEditLine, props={text=filter,size=util.vector2(570,28)},
    events={textChanged=async:callback(function(value)
        filter=value; filterElement.layout.props.text=value; pageNumber=1; dirty=true
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
        if dirty then render() end
    end},
}
