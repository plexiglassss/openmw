-- UI discovery only. Uses the existing global queue and its shared soft budget;
-- quest state, actor scripts and gameplay events keep their existing execution path.
local M = {}

function M.new(queue, world, types, protectedDoor, questLib, dataHandler)
    local giverTypes = {types.NPC, types.Creature, types.Book, types.Miscellaneous, types.Weapon, types.Activator}

    local function collect(player)
        local objectIds, visitedDepth = {}, {}
        local function processCell(cell, depth)
            if not cell or not cell.id or (visitedDepth[cell.id] or 0) >= depth then return end
            visitedDepth[cell.id] = depth
            queue.checkpoint('nearby quest cell')
            for _, objectType in ipairs(giverTypes) do
                queue.checkpoint('nearby quest object list')
                for _, object in pairs(cell:getAll(objectType)) do
                    queue.checkpoint('nearby quest object')
                    if object:isValid() then
                        objectIds[object.recordId] = true
                        local record = object.type.record(object)
                        if record and record.mwscript and record.mwscript ~= '' then
                            objectIds[record.mwscript] = true
                        end
                    end
                end
            end
            if depth <= 1 then return end
            queue.checkpoint('nearby quest door list')
            for _, door in pairs(cell:getAll(types.Door)) do
                queue.checkpoint('nearby quest door')
                if door:isValid() and types.Door.isTeleport(door) then
                    processCell(protectedDoor.destCell(door), depth - 1)
                end
            end
        end

        local cell = player.cell
        if cell.isExterior then
            for x = -1, 1 do
                for y = -1, 1 do
                    queue.checkpoint('nearby quest exterior')
                    processCell(world.getExteriorCell(cell.gridX + x, cell.gridY + y), 2)
                end
            end
        else
            processCell(cell, 2)
        end
        local dialogueIds = {}
        for objectId in pairs(objectIds) do
            queue.checkpoint('nearby quest lookup')
            local data = questLib.getObjectData(objectId)
            if data and data.starts then
                for _, dialogueId in pairs(data.starts) do
                    queue.checkpoint('nearby quest result')
                    dialogueIds[dialogueId] = true
                end
            end
        end
        local result = {}
        for dialogueId in pairs(dialogueIds) do
            queue.checkpoint('nearby quest result list')
            result[#result + 1] = dialogueId
        end
        return result
    end

    return {
        request = function(data)
            local player = data.player
            if not player or not player:isValid() or not player.cell then return false end
            local origin = player.cell.id
            local group = 'nearby-list:'..tostring(player.id)
            local key = group..':'..tostring(data.menuId or '')
            return queue.add(key, function()
                local dialogueIds = collect(player)
                queue.checkpoint('publish nearby quests')
                local result = {}
                for name, value in pairs(data) do result[name] = value end
                result.diaIds = dialogueIds
                player:sendEvent('QGL:questsNearby', result)
            end, function()
                if not player:isValid() or not player.cell or player.cell.id ~= origin then return false end
                if not dataHandler.isReady() then return 'wait' end
                return true, origin
            end, group, {replace = true, priority = true, label = 'nearby quest list'})
        end,
    }
end

return M
