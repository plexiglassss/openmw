"""Exercise the shipped equipment handler with mocked OpenMW services."""
import ctypes as c
import pathlib
import sys

path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else
    'custom/ui-features/InventoryExtenderIntegration/Data_Files/scripts/InventoryExtender/ui/windows/inventory.lua')
source = path.read_text()
start = source.index('    local function canPickupItem(')
end = source.index('    local function createDraggedActivationRow(', start)
handler = source[start:end].replace('    local onRowUse = function(row, rowWidget, fromKBMKeybind, forceBarterAction)\n        return false\n    end',
    '    local onRowUse = function() tradeCalls = tradeCalls + 1; return false end')
setup = '''
local mode, tradeCalls, uses, updates, sounds, vetoCalls
local equipment, bound, veto
local ctx, self, omwself, I, helpers, ambient, core
local function reset(m)
    mode, tradeCalls, uses, updates, sounds, vetoCalls = m, 0, 0, 0, 0, 0
    bound, veto, equipment = false, false, {}
    omwself = {type = {}}
    omwself.type.hasEquipped = function(_, item)
        for _, v in pairs(equipment) do if v == item then return true end end
        return false
    end
    omwself.type.getEquipment = function() return equipment end
    omwself.type.setEquipment = function(_, e) equipment = e end
    omwself.sendEvent = function() updates = updates + 1 end
    ctx = {handlers = {onRowPickup = {{handler = function()
        vetoCalls = vetoCalls + 1; if veto then return false end
    end}}}, barterState = {selling = {}, buying = {}}}
    self = {type = 'Inventory', target = omwself}
    I = {UI = {getMode = function() return mode end}}
    helpers = {isBoundItem = function() return bound end, getItemSound = function() return 'down' end}
    ambient = {playSound = function() sounds = sounds + 1 end}
    core = {sendGlobalEvent = function(name, args)
        assert(name == 'IE_UseItem' and args.actor == omwself)
        assert(args.source == omwself)
        uses = uses + 1
    end}
end
'''
checks = '''
for _, m in ipairs({'Interface', 'Barter', 'Container', 'Companion'}) do
    reset(m)
    local item = {id = 'weapon', parentContainer = omwself}
    local row = {item = item}
    assert(onRowPickup(row, nil, 'Inventory', true, true) ~= false)
    assert(uses == 1 and tradeCalls == 0 and vetoCalls == 1, m .. ': equip')
    equipment[1] = item
    equipment[2] = {id = 'other'}
    onRowPickup(row, nil, 'Inventory', true, true)
    assert(equipment[1] == nil and equipment[2] ~= nil and updates == 1 and sounds == 1, m .. ': unequip')
    equipment[1], bound = item, true
    assert(onRowPickup(row, nil, 'Inventory', true, true) == false)
    assert(equipment[1] == item and updates == 1, m .. ': bound')
    bound, veto = false, true
    assert(onRowPickup(row, nil, 'Inventory', true, true) == false)
    assert(equipment[1] == item and updates == 1, m .. ': provider veto')
end
for _, kind in ipairs({'merchant', 'bartered', 'selling', 'buying'}) do
    reset('Barter')
    local item = {id = 'weapon', parentContainer = omwself}
    local row = {item = item}
    if kind == 'merchant' then item.parentContainer = {} end
    if kind == 'bartered' then row.isBartered = true end
    if kind == 'selling' then ctx.barterState.selling[item.id] = {count = 1} end
    if kind == 'buying' then ctx.barterState.buying[item.id] = {count = 1} end
    equipment[1] = item
    assert(onRowPickup(row, nil, 'Inventory', true, true) == false, kind)
    assert(uses == 0 and tradeCalls == 0 and updates == 0 and equipment[1] == item, kind)
end
reset('Barter')
local item = {id = 'weapon', parentContainer = omwself}
ctx.barterState.selling[item.id] = {count = 0}
assert(onRowPickup({item = item}, nil, 'Inventory', true, true) ~= false)
assert(uses == 1 and tradeCalls == 0, 'cancelled sale')
onRowPickup({item = item}, nil, 'Inventory', true)
assert(tradeCalls == 1 and uses == 1, 'ordinary barter row unchanged')
print('PASS: paper-doll equip/unequip in four modes; bound items, provider veto, trade ownership/reservations, cancelled sale, ordinary barter routing')
'''
lua = c.CDLL('liblua5.4.so.0')
lua.luaL_newstate.restype = c.c_void_p
lua.luaL_openlibs.argtypes = [c.c_void_p]
lua.luaL_loadstring.argtypes = [c.c_void_p, c.c_char_p]
lua.lua_pcallk.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_int, c.c_longlong, c.c_void_p]
lua.lua_tolstring.argtypes = [c.c_void_p, c.c_int, c.c_void_p]
lua.lua_tolstring.restype = c.c_char_p
lua.lua_close.argtypes = [c.c_void_p]
state = lua.luaL_newstate()
lua.luaL_openlibs(state)
status = lua.luaL_loadstring(state, (setup + handler + checks).encode())
if not status:
    status = lua.lua_pcallk(state, 0, 0, 0, 0, None)
if status:
    raise RuntimeError(lua.lua_tolstring(state, -1, None).decode())
lua.lua_close(state)

# Both paper-doll paths must opt into equipment handling, never a trade action.
assert 'onRowPickup(createDraggedActivationRow(draggedItem), nil, activation.windowType, true, true)' in source
assert "getCount = function() return item.count end }, nil, 'Inventory', true, true)" in source
