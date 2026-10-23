local root = 'custom/ui-features/InventoryExtenderIntegration/Data_Files/'
package.path = root .. '?.lua;' .. package.path
local events, messages, equipped = {}, {}, {}
local player = {type = {}, position = 0, cell = {isExterior = true}}
player.type.hasEquipped = function(_, item) return equipped[item.id] == true end
player.sendEvent = function() end
local config = {tweaks = {}, misc = {}}
local ctx = {favoriteItems = {}, windowArgs = {}}
local ui = {
    layers = {indexOf = function() return 1 end},
    showMessage = function(msg) table.insert(messages, msg) end,
    TYPE = {}, ALIGNMENT = {}, texture = function(x) return x end,
    content = function(x) return x end,
    create = function(x) return {layout=x, update=function() end} end,
}
local core = {
    l10n = function() return function(x) return x end end,
    sendGlobalEvent = function(name, data) table.insert(events, {name=name, data=data}) end,
}
local util = {vector2=function() return {} end, vector3=function() return 0 end,
    clamp=function(n,a,b) return math.max(a,math.min(n,b)) end,
    color={rgb=function() return {} end}, transform={rotateZ=function() return {} end}}
local helpers = {getItemSound=function() end, doesItemFit=function() return true end,
    getCountString=tostring}
local types = {Container={objectIsInstance=function() return false end}}
local modules = {
    ['openmw.self']=player, ['openmw.ui']=ui, ['openmw.core']=core,
    ['openmw.types']=types, ['openmw.util']=util,
    ['openmw.interfaces']={UI={getMode=function() return 'Interface' end}, MWUI={templates={textHeader={}}}},
    ['openmw.ambient']={}, ['openmw.input']={}, ['openmw.nearby']={},
    ['openmw.async']={}, ['openmw.camera']={getYaw=function() return 0 end},
    ['openmw_aux.ui']={deepDestroy=function() end},
    ['scripts.InventoryExtender.config.player']=config,
    ['scripts.InventoryExtender.util.helpers']=helpers,
    ['scripts.InventoryExtender.util.constants']={},
    ['scripts.InventoryExtender.ui.templates.base']={},
    ['scripts.InventoryExtender.ui.templates.magic']={},
}
for name,value in pairs(modules) do package.loaded[name]=value end
local protect = require('scripts.InventoryExtender.util.itemProtection')
local drag = require('scripts.InventoryExtender.ui.dragAndDrop')
drag.ctx=ctx
local function item(id, record, count)
    return {id=id, recordId=record or id, count=count or 1, parentContainer=player,
        type={record=function() return {icon='test'} end}}
end
local a, b, c = item('a'), item('b'), item('c')
ctx.favoriteItems.a = true
equipped.b = true
assert(protect.reason(ctx,a,player,{}) == 'UI_Msg_FavoriteItem')
assert(protect.reason(ctx,b,player,{}) == 'UI_Msg_EquippedProtected')
assert(protect.reason(ctx,a,player,player) == nil)
assert(protect.reason(ctx,a,{},player) == nil)
config.tweaks.b_ProtectFavorites=false
assert(protect.reason(ctx,a,player,{}) == nil)
assert(protect.reason(ctx,b,player,{}) ~= nil)
config.tweaks.b_ProtectEquipped=false
assert(protect.reason(ctx,b,player,{}) == nil)
config.tweaks={}
assert(drag:transferInto(a,{},player,1) == false and #events == 0)
assert(drag:transferInto(b,{},player,1) == false and #events == 0)
assert(drag:transferInto(c,{},player,1) == true and #events == 1)
assert(drag:transferInto(a,player,player,1) == true)
events={}
drag:moveAll(player,{}, {a,b,c})
assert(#events==1 and events[1].name=='IE_MoveAll')
assert(#events[1].data.items==1 and events[1].data.items[1]==c)
events={}
assert(drag:dropAt(a,1,0)==false and #events==0)
assert(drag:dropAt(b,1,0)==false and #events==0)
assert(drag:dropAt(c,1,0)==true and events[1].name=='IE_Teleport')
-- Cursor identity can change after a stack split; protection must survive it.
drag:startDrag(a,player,1)
local split = item('split','a')
drag:setDraggingObject(split)
assert(ctx.favoriteItems.split==true)
events={}
assert(drag:stopDrag({})==false and drag.draggingObject==split and #events==0)
drag:stopDrag(player)
assert(drag.draggingObject==nil and drag.dragProtection==nil)
-- Dragging can unequip an item before the destination is selected.
drag:startDrag(b,player,1)
equipped.b=nil
drag:setDraggingObject(b)
events={}
assert(drag:dropAt(b,1,0)==false and #events==0)
assert(drag:transferInto(b,{})==false and #events==0)
config.tweaks.b_ProtectEquipped=false
assert(drag:transferInto(b,{})==true)

local recent = require('scripts.InventoryExtender.util.recentItems')
recent.load(nil)
local stack=item('s','potion',5)
recent.observe({stack})
assert(not recent.isRecent(stack), 'old inventory must not start NEW')
stack.count=7;recent.observe({stack});assert(recent.isRecent(stack))
recent.clear();assert(not recent.isRecent(stack))
recent.observe({item('part1','potion',3), item('part2','potion',4)})
assert(not recent.isRecent(stack), 'split is not acquisition')
recent.observe({item('merged','potion',7)})
assert(not recent.isRecent(stack), 'merge is not acquisition')
local weapon=item('w','sword')
recent.observe({stack,weapon});assert(recent.isRecent(weapon))
local saved=recent.save();recent.load(nil);recent.load(saved)
recent.observe({stack,weapon});assert(recent.isRecent(weapon))
recent.observe({stack});assert(not recent.isRecent(weapon))
recent.observe({stack,weapon});assert(recent.isRecent(weapon))
print('PASS: protection toggles, transfer/drop/bulk routes, own-inventory return, split cursor identity, unequipped cursor protection; recent baseline, gains, splits, merges, clear, save/load and reacquisition')
