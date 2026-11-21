local clock = 0
local core = {getRealTime = function() clock = clock + 0.000001; return clock end}
package.preload['openmw.core'] = function() return core end
local queue = assert((loadstring or load)(QUEUE_SOURCE, 'indexedQueue.lua'))()
local module = assert((loadstring or load)(QUERY_SOURCE, 'nearbyQueries.lua'))()
local env = {queue = queue, messages = {}, cells = {}, objects = {}, calls = 0, ready = true, revision = ''}
local types = {}
for _, name in ipairs({'NPC','Creature','Book','Miscellaneous','Weapon','Activator','Door'}) do
    types[name] = {record = function(object) return {mwscript = object.script} end}
end
types.Door.isTeleport = function(object) return object.destination ~= nil end
local function cell(id, x, y)
    local value = {id = id, isExterior = x ~= nil, gridX = x, gridY = y, groups = {}}
    function value:getAll(kind)
        env.calls = env.calls + 1
        local out = {}
        for _, object in ipairs(self.groups[kind] or {}) do
            if object.valid then out[#out + 1] = object end
        end
        return out
    end
    env.cells[id] = value
    return value
end
local function object(owner, kind, id, destination)
    local value = {recordId = id, type = types[kind], valid = true, script = 'script_'..id, destination = destination}
    function value:isValid() return self.valid end
    owner.groups[value.type] = owner.groups[value.type] or {}
    table.insert(owner.groups[value.type], value)
    env.objects[#env.objects + 1] = value
    return value
end
local world = {getExteriorCell = function(x,y) return env.cells[tostring(x)..','..tostring(y)] end}
local protectedDoor = {destCell = function(door) return door.destination end}
local questLib = {getObjectData = function(id)
    return {starts = {id..env.revision, 'shared'..env.revision}}
end}
local dataHandler = {isReady = function() return env.ready end}
env.player = {id = 'player', valid = true}
function env.player:isValid() return self.valid end
function env.player:sendEvent(name, data) env.messages[#env.messages + 1] = {name = name, data = data} end
env.service = module.new(queue, world, types, protectedDoor, questLib, dataHandler)
env.world, env.types, env.protectedDoor, env.questLib = world, types, protectedDoor, questLib

function env.fixture(count, exterior)
    queue.clear(); env.messages = {}; env.cells = {}; env.objects = {}; env.calls = 0
    env.ready, env.revision, env.player.valid = true, '', true
    local a = cell(exterior and '-1,-1' or 'interior', exterior and -1 or nil, exterior and -1 or nil)
    local b = cell('0,0', 0, 0)
    local c = cell('far interior')
    object(a, 'Door', 'a_to_b', b)
    object(b, 'Door', 'b_to_a', a)
    object(b, 'Door', 'b_to_c', c)
    object(c, 'NPC', 'only_reached_when_b_is_root')
    for i = 1, count do
        object(a, 'NPC', 'npc'..i)
        object(a, 'Book', 'book'..i)
        object(b, 'Creature', 'creature'..i)
    end
    env.player.cell = a
    return a,b,c
end
function env.request(menu, request)
    local data = {player = env.player, menuId = menu or 'menu', requestId = request or 'first', extra = 'preserved'}
    env.service.request(data)
    return data
end
function env.finish()
    local count = 0
    while queue.getStats().pending > 0 do
        queue.update(); count = count + 1
        assert(count < 10000, 'queue failed to complete')
    end
    return count
end
function env.baseline(data)
    -- Execute the original production handler with its production type helper.
    local globals = {world = world, types = types, protectedDoor = protectedDoor, questLib = questLib,
        supportedGiverTypeList = {types.NPC, types.Creature, types.Book, types.Miscellaneous, types.Weapon, types.Activator},
        tableLib = {keys = function(values) local out = {}; for key in pairs(values) do out[#out + 1] = key end; return out end}}
    setmetatable(globals, {__index = _G})
    local source = LEGACY_HELPER..'\nreturn '..LEGACY_HANDLER
    local chunk
    if setfenv then chunk = assert(loadstring(source)); setfenv(chunk, globals)
    else chunk = assert(load(source, 'legacy main.lua', 't', globals)) end
    chunk()(data)
end
return env
