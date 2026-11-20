local tableLib = require("scripts.advanced_world_map.utils.table")

local maxDepth = 10

local this = {}


---@param dataTable table<string, advancedWorldMap.dynamicDataHandler.entranceData[]>
---@return any[]?
function this.findExitPoss(cellId, dataTable, checked, res, depth, checkpoint)
    checkpoint = checkpoint or function() end
    if not dataTable or not cellId then return end
    if not checked then checked = {} end
    if not res then res = {} end
    if not depth then depth = 1 end

    local doors = dataTable[cellId]
    if not doors then return end

    if (checked[cellId] and checked[cellId] < depth) or depth > maxDepth then
        return
    end
    checked[cellId] = math.min(checked[cellId] or depth, depth)

    local processDoor = function (door)
        if checked[door.dCId] and checked[door.dCId] < depth + 1 then return end

        if door.isDEx then
            res[door] = depth
        else
            this.findExitPoss(door.dCId, dataTable, checked, res, depth + 1, checkpoint)
        end
    end

    for _, door in pairs(doors) do

        checkpoint()
        processDoor(door)
    end

    if depth == 1 then
        local minDepth = math.huge
        for _, d in pairs(res) do
            checkpoint()
            if d < minDepth then minDepth = d end
        end

        if minDepth == math.huge then return nil end

        local r = {}
        for door, d in pairs(res) do
            checkpoint()
            if d == minDepth then
                table.insert(r, door.dPos)
            end
        end

        return r
    end

    return nil
end


return this