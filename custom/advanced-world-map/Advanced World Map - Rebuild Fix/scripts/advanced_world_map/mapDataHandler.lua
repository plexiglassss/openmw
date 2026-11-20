local core = require("openmw.core")
local storage = require("openmw.storage")
local types = require("openmw.types")
local util = require("openmw.util")

local pDoor = require("scripts.advanced_world_map.helpers.protectedDoor")

local log = require("scripts.advanced_world_map.utils.log")

local stringLib = require("scripts.advanced_world_map.utils.string")
local tableLib = require("scripts.advanced_world_map.utils.table")
local commonData = require("scripts.advanced_world_map.common")

local this = {}

this.version = 11

---@type table<string, advancedWorldMap.dynamicDataHandler.cellData> by cell name
this.cellNameData = nil
---@type table<string, advancedWorldMap.dynamicDataHandler.cellData> by region name
this.regionNameData = nil
---@type table<string, advancedWorldMap.dynamicDataHandler.entranceData[]> by cell id
this.entrances = nil
---@type table<string, string> by cell id
this.cellNameById = nil
---@type table<string, boolean> by cell id
this.validCellsWithoutName = {}
---@type {max : {x : integer, y : integer}, min : {x : integer, y : integer}}
this.grid = nil
---@type {[1] : number, [2] : number, [3] : number, [4] : number}[]
this.worldMapTileRectangles = {}
---@type advancedWorldMap.dynamicDataHandler.transport
this.transport = nil

---@type table<string, string>
this.worldNameById = {}

this.cellCount = 0
this.contentFileCount = 0

local initialized = false
local rebuildJob = require("scripts.advanced_world_map.rebuildJob")
local activeBuild, completedBuild
local activeToken, pendingRequest, lastResultToken
local activeOrder = 0
local requestSerial = 0
local offers, offerOrder = {}, {}
local dataKeys = { "cellNameData", "regionNameData", "entrances", "cellNameById",
    "validCellsWithoutName", "grid", "worldMapTileRectangles", "transport", "cellCount", "contentFileCount" }

local function publish(data)
    for _, key in ipairs(dataKeys) do this[key] = data[key] end
    this.worldNameById = data.worldNameById or {}
    initialized = true
end

function this.isCurrentRequest(token)
    return token ~= nil and token == activeToken
end

function this.cancelBuild()
    if activeBuild then activeBuild.job.cancel() end
    activeBuild, completedBuild = nil, nil
end

function this.resetSession()
    this.cancelBuild()
    activeToken, pendingRequest, lastResultToken, initialized = nil, nil, nil, false
    activeOrder = 0
    offers, offerOrder = {}, {}
    for _, key in ipairs(dataKeys) do this[key] = nil end
    this.cellCount, this.contentFileCount = 0, 0
    this.worldNameById = {}
end



local forbiddenCellPrefixes = { -- lowercase
    ["solstheim"] = true
}


---@class advancedWorldMap.dynamicDataHandler.cellData
---@field posX number
---@field posY number
---@field name string
---@field id string
---@field count integer

---@class advancedWorldMap.dynamicDataHandler.entranceData
---@field pos any position
---@field cId string cell id
---@field isEx boolean is in exterior cell
---@field isLEx boolean is destination in like exterior cell
---@field dCId string destination cell id
---@field dPos any destination position
---@field isDEx boolean is destination cell exterior
---@field isDLEx boolean is destination cell like exterior
---@field name string destination cell name
---@field fName string destination cell full name
---@field pN string? prefix name (for cells with comma in name)
---@field ppN string? second prefix name
---@field dHash string door hash
---@field pHash string? parent entrance hash (for grouped entrances)
---@field isIsl boolean? is isolated from other entrances
---@field tp integer?

---@class advancedWorldMap.dynamicDataHandler.transport
---@field nodes advancedWorldMap.dynamicDataHandler.transport.node[] list of transport nodes
---@field actors table<string, {tp: integer, ns: integer[]}> by npc record id
---@field data table<integer, integer[]> list of node ids by transport type (1 - caravaner, 2 - shipmaster, 3 - guild guide, 4 - gondolier)

---@class advancedWorldMap.dynamicDataHandler.transport.node
---@field tp integer type (1 - caravaner, 2 - shipmaster, 3 - guild guide, 4 - gondolier)
---@field p Vector2 position
---@field ls integer[] list of node ids that have this node in their list of destinations
---@field ars string[]? list of actor record ids that are on this node


function this.getCellCount()
    local cells = require("openmw.world").cells
    local localStorage = require("scripts.advanced_world_map.storage.localStorage")
    local nonContentCells = localStorage.data[commonData.globalNonContentCellsFieldId] or {}
    local cellCount = 0
    for _, c in pairs(cells) do
        if not c.id or not nonContentCells[c.id] then
            cellCount = cellCount + 1
        end
    end

    return cellCount
end


function this.getClusterBoundingBox(cluster)
    local fpos = cluster[1].pos
    local minX, maxX = fpos.x, fpos.x
    local minY, maxY = fpos.y, fpos.y
    for _, m in pairs(cluster) do
        local pos = m.pos
        if pos.x < minX then minX = pos.x end
        if pos.x > maxX then maxX = pos.x end
        if pos.y < minY then minY = pos.y end
        if pos.y > maxY then maxY = pos.y end
    end
    return {x = util.vector2(minX, minY), y = util.vector2(maxX, maxY), center = util.vector2((minX + maxX) / 2, (minY + maxY) / 2)}
end


local function buildWorldNameByIdData(target)
    local this = target or this
    if not this.cellNameData then return end
    local tb = {}
    for cN, dt in pairs(this.cellNameData) do
        if dt.id then
            tb[dt.id] = dt.name
        end
    end

    this.worldNameById = tb
end


local function isContentFile(name)
    name = name:lower()

    local suffixes = {"esm", "esp", "omwaddon"}
    for _, suf in ipairs(suffixes) do
        if stringLib.isEndsWith(name, suf) then
            return true
        end
    end
    return false
end


local function worldCoverWithRectanglesFast(occupied, checkpoint)
    local res = {}

    local xCoords = {}
    for x in pairs(occupied) do
        checkpoint()
        table.insert(xCoords, x)
    end
    table.sort(xCoords)

    for _, x in ipairs(xCoords) do

        checkpoint()
        local col = occupied[x]
        if col then
            local yCoords = {}
            for y in pairs(col) do
                checkpoint()
                table.insert(yCoords, y)
            end
            table.sort(yCoords)

            local i = 1
            while i <= #yCoords do
                checkpoint()
                local y1 = yCoords[i]
                local y2 = y1

                while i < #yCoords and yCoords[i + 1] == y2 + 1 do

                    checkpoint()
                    i = i + 1
                    y2 = yCoords[i]
                end

                local x2 = x
                local canExpandX = true
                while canExpandX do
                    checkpoint()
                    local nextX = x2 + 1
                    local nextCol = occupied[nextX]
                    if nextCol then

                        for y = y1, y2 do

                            checkpoint()
                            if not nextCol[y] then
                                canExpandX = false
                                break
                            end
                        end

                        if canExpandX then
                            x2 = nextX
                        end
                    else
                        canExpandX = false
                    end
                end

                table.insert(res, {x, y1, x2, y2})

                for xi = x, x2 do

                    checkpoint()
                    local c = occupied[xi]
                    if c then
                        for y = y1, y2 do
                            checkpoint()
                            c[y] = nil
                        end
                        if not next(c) then
                            occupied[xi] = nil
                        end
                    end
                end

                i = i + 1
            end
        end
    end

    return res
end


local function buildData(params, result, checkpoint)
    local this = result
    local world = require("openmw.world")
    local transportModule = require("scripts.advanced_world_map.mapData.transport")
    local cellDoorTypeModule = require("scripts.advanced_world_map.mapData.cellDoorType")
    local localStorage = require("scripts.advanced_world_map.storage.localStorage")

    local nonContentCells = localStorage.data[commonData.globalNonContentCellsFieldId] or {}

    local buildCellWorldMapData = params.buildCellWorldMapData

    local function getRegionName(id)
        if not id then return "" end
        if not core.regions or not core.regions.records then return stringLib.capitalizeFirst(id) end

        local region = core.regions.records[id]
        if not region then return stringLib.capitalizeFirst(id) end

        return region.name or ""
    end

    this.cellNameById = {}
    this.validCellsWithoutName = {}
    this.worldMapTileRectangles = {}

    local minGridX = math.huge
    local minGridY = math.huge
    local maxGridX = -math.huge
    local maxGridY = -math.huge

    local cellNameData = {}
    local regionNameData = {}
    local entrances = {}
    local occupied = {}
    local exCellSecondPrefixes = {}
    this.cellCount = this.cellCount ~= 0 and this.cellCount or this.getCellCount()

    local function processCellInit (cell)
        if not cell.isExterior or not cell.id then return end

        local gridX = cell.gridX
        local gridY = cell.gridY

        if gridX > 1000 or gridX < -1000 or gridY > 1000 or gridY < -1000 then
            return
        end

        if buildCellWorldMapData then
            if core.land then
                local posX = gridX * 8192
                local posY = gridY * 8192
                local aboveWater = 0
                for i = 1024, 8192, 2048 do
                    checkpoint()
                    for j = 1024, 8192, 2048 do
                        checkpoint()
                        if core.land.getHeightAt(util.vector3(posX + i, posY + j, 0), cell) > 0 then
                            aboveWater = aboveWater + 1
                        end
                    end
                end
                if aboveWater > 8 then
                    occupied[gridX] = occupied[gridX] or {}
                    occupied[gridX][gridY] = true
                end
            else
                occupied[gridX] = occupied[gridX] or {}
                occupied[gridX][gridY] = true
            end
        end

        if not cell.name or cell.name == "" then return end

        local name, pName = stringLib.getBeforeAfterComma(cell.name)
        if cell.isExterior and pName then
            local n, p = stringLib.getBeforeAfterComma(cell.displayName or cell.name)
            if p then
                exCellSecondPrefixes[cell.id] = p
            end
        end

        local function getInitCellData()
            return {
                name = stringLib.getBeforeComma(cell.displayName or cell.name),
                id = stringLib.utf8_lower(name),
                count = 0,
                minX = math.huge, maxX = -math.huge,
                minY = math.huge, maxY = -math.huge,
            }
        end

        local cellDt = cellNameData[name]
        if not cellDt then
            cellDt = getInitCellData()
            cellNameData[name] = cellDt
        else
            local cx = cellDt.maxX - cellDt.minX
            local cy = cellDt.maxY - cellDt.minY

            local function findNewName(name, x, y)
                local n = name
                for i = 1, 10 do
                    checkpoint()
                    local dt = cellNameData[n]
                    if not dt or math.abs(dt.maxX - dt.minX - x) < 16 and math.abs(dt.maxY - dt.minY - y) < 16 then
                        return n
                    end
                    n = " "..n.." "
                end
                return n
            end

            -- for cases when the same cell name is used for different locations, we will try to find a new name by adding spaces around the name
            -- this method is used to avoid compatibility issues
            if math.abs(cx - gridX) > 16 or math.abs(cy - gridY) > 16 then
                local nn = findNewName(name, gridX, gridX)
                if not cellNameData[nn] then
                    cellNameData[nn] = getInitCellData()
                end
                cellDt = cellNameData[nn]
            end
        end

        cellDt.minX = math.min(gridX, cellDt.minX)
        cellDt.minY = math.min(gridY, cellDt.minY)
        cellDt.maxX = math.max(gridX, cellDt.maxX)
        cellDt.maxY = math.max(gridY, cellDt.maxY)
        cellDt.count = cellDt.count + 1

        if cell.region then
            local regDt = regionNameData[cell.region]
            if not regDt then
                regDt = {
                    name = getRegionName(cell.region), count = 0,
                    minX = math.huge, maxX = -math.huge,
                    minY = math.huge, maxY = -math.huge,
                }
                regionNameData[cell.region] = regDt
            end

            regDt.minX = math.min(gridX, regDt.minX)
            regDt.minY = math.min(gridY, regDt.minY)
            regDt.maxX = math.max(gridX, regDt.maxX)
            regDt.maxY = math.max(gridY, regDt.maxY)
            regDt.count = regDt.count + 1
        end
    end

    for _, cell in pairs(world.cells) do

        checkpoint()
        processCellInit(cell)
    end


    local function buildRectMapFast()
        this.worldMapTileRectangles = worldCoverWithRectanglesFast(occupied, checkpoint)
    end

    if buildCellWorldMapData and not pcall(buildRectMapFast) then
        log("Error building world map tile rectangles")
        this.worldMapTileRectangles = {}
    end

    local function getCellName(cell)
        local name = cell.displayName or cell.name
        if cell.isExterior then
            if not name or name == "" then
                name = getRegionName(cell.region)
            end
        end
        return name
    end

    local cellNameLines = {}
    local cellNames = {}
    for _, dt in pairs(cellNameData) do
        checkpoint()
        if dt.count >= 1 then

            local posX = (dt.minX + (dt.maxX - dt.minX) / 2) * 8192 + 4096
            local posY = (dt.minY + (dt.maxY - dt.minY) / 2) * 8192 + 4096

            local cellDt = {
                id = dt.id,
                name = dt.name,
                count = dt.count,
                posX = posX,
                posY = posY,
            }
            cellNames[dt.name] = cellDt

            local hash = math.floor(posY / 6144)
            for i = -1, 1 do
                checkpoint()
                local h = hash + i
                cellNameLines[h] = cellNameLines[h] or {}
                table.insert(cellNameLines[h], cellDt)
            end

        end
    end

    ---@type advancedWorldMap.dynamicDataHandler.cellType.actorData
    local cellTypeModuleData = {
        guards = {},
        pubOwners = {},
        traderNPC = {},
        npcFaction = {},
    }
    ---@type table<string, integer|boolean>
    local cellDoorTypes = {}

    local transportRecords = {}
    local transportCellData = {}
    for _, rec in pairs(types.NPC.records) do
        checkpoint()
        if rec.travelDestinations and #rec.travelDestinations > 0 then
            transportRecords[rec.id] = rec
        end

        cellDoorTypeModule.fillActorRecordData(cellTypeModuleData, rec)
    end

    ---@type table<string, advancedWorldMap.dynamicDataHandler.entranceData[]>
    local entrancesWithCellName = {}
    local populationMap = {}
    local cellsWithObjects = {}

    local function beginCell(cell)
        local cellId = cell.id
        if not cellId or this.cellNameById[cellId] or this.validCellsWithoutName[cellId] then return nil end

        local isQuasiExterior = cell:hasTag("QuasiExterior")

        local cellName = getCellName(cell)
        this.cellNameById[cellId] = cellName
        if not cellName then
            this.validCellsWithoutName[cellId] = true
        end

        local processDoor = function (context)
            local door, dest, destPos = context.door, context.dest, context.destPos
            local isDestQuasiExterior = context.quasi
            local doorType = context.doorType
            if context.needsDestination then doorType = cellDoorTypes[dest.id] end
            if not dest.isExterior and not isDestQuasiExterior then
                if context.needsFallback and type(doorType) ~= "number" then
                    doorType = cellDoorTypeModule.getDoorType(door, dest) or true
                    cellDoorTypes[dest.id] = doorType
                end
                if type(doorType) ~= "number" then doorType = nil end
            end

            local exTypeCellName
            local name = getCellName(dest)
            local prefixName
            local fullName = name
            if name:find(",") or name:find("，") then
                if not dest.isExterior then
                    local nm, cellNameMark = stringLib.getAfterBeforeComma(name)
                    if cellNames[cellNameMark] then
                        exTypeCellName = cellNameMark
                        name = nm
                    end

                    if cell.isExterior then
                        prefixName = cellNameMark

                        local cellIdMark = stringLib.getBeforeComma(dest.id)
                        if forbiddenCellPrefixes[cellIdMark] then
                            fullName = name
                        end
                    end
                else
                    name = stringLib.getAfterComma(name)
                end
            end

            local doorHash = commonData.doorHash(door, dest.id or "")

            entrances[cellId] = entrances[cellId] or {}
            ---@type advancedWorldMap.dynamicDataHandler.entranceData
            local data = {
                pos = door.position,
                cId = cellId,
                isEx = cell.isExterior,
                dCId = dest.id or "",
                dPos = destPos,
                isDEx = dest.isExterior,
                name = name,
                fName = fullName,
                pN = prefixName,
                dHash = doorHash,
                isDLEx = dest.isExterior or isDestQuasiExterior,
                isLEx = cell.isExterior or isQuasiExterior,
                tp = doorType
            }
            entrances[cellId][doorHash] = data

            if cell.isExterior then
                local x = math.floor(data.pos.x / 1024)
                local y = math.floor(data.pos.y / 1024)
                local id = x * 262144 + y
                populationMap[id] = populationMap[id] or {x = x, y = y, m = {}}
                table.insert(populationMap[id].m, data)
            end

            if exTypeCellName and cell.isExterior then
                entrancesWithCellName[exTypeCellName] = entrancesWithCellName[exTypeCellName] or {}
                table.insert(entrancesWithCellName[exTypeCellName], data)

                if prefixName then
                    local sPrefixes = {}
                    for x = cell.gridX - 1, cell.gridX + 1 do
                        checkpoint()
                        for y = cell.gridY - 1, cell.gridY + 1 do
                            checkpoint()
                            local pp = exCellSecondPrefixes[commonData.exteriorCellIdFormat:format(x, y)]
                            if pp and not sPrefixes[pp] then
                                sPrefixes[pp] = x == cell.gridX and y == cell.gridY
                            end
                        end
                    end
                    for sPrefix, isCurrentCell in pairs(sPrefixes) do
                        checkpoint()
                        if isCurrentCell then
                            data.ppN = sPrefix
                        end
                        if data.name:sub(1, #sPrefix) == sPrefix then
                            data.ppN = sPrefix
                            data.name = stringLib.trimStart(data.name:sub(#sPrefix + 1))
                        end
                    end
                end
            end
        end

        local hasObjects = false

        local npcs = cell:getAll(types.NPC)
        for _, npc in pairs(npcs) do
            checkpoint()
            hasObjects = true
            if transportRecords[npc.recordId] then
                transportCellData[cell] = transportCellData[cell] or {}
                table.insert(transportCellData[cell], npc)
            end
        end

        if not cell.isExterior and not isQuasiExterior then
            cellDoorTypes[cellId] = cellDoorTypeModule.getDestDoorType(cellId) or
                cellDoorTypeModule.getCellType(cellId, npcs, cellTypeModuleData) or false
        end

        local nextDoor, state, key = pairs(cell:getAll(types.Door))
        return { cell = cell, id = cellId, nextDoor = nextDoor, state = state, key = key,
            hasObjects = hasObjects, processDoor = processDoor }
    end

    local function inspectDoor(door)
        if not types.Door.isTeleport(door) then return end
        local dest, destPos = pDoor.destCell(door), pDoor.destPosition(door)
        if not dest or not destPos then return end
        local quasi = dest:hasTag("QuasiExterior")
        local doorType
        if not dest.isExterior and not quasi then
            doorType = cellDoorTypeModule.getHighPriorityDoorType(door, dest.id) or cellDoorTypes[dest.id]
        end
        return {door = door, dest = dest, destPos = destPos, quasi = quasi, doorType = doorType,
            needsFallback = not doorType,
            needsDestination = not dest.isExterior and not quasi and not doorType and type(doorType) ~= "boolean"}
    end

    -- Heap-owned frames preserve v11's depth-first order. Resume the SAME door
    -- after its child cell has finished; cycles still use the original visited maps.
    local function processCell(cell)
        local first = beginCell(cell)
        if not first then return cellsWithObjects[cell.id] end
        local stack = {first}
        while #stack > 0 do
            checkpoint()
            local frame = stack[#stack]
            if frame.pending then
                frame.processDoor(frame.pending)
                frame.pending = nil
            else
                local key, door = frame.nextDoor(frame.state, frame.key)
                frame.key = key
                if key == nil then
                    cellsWithObjects[frame.id] = frame.hasObjects
                    stack[#stack] = nil
                else
                    frame.hasObjects = true
                    local context = inspectDoor(door)
                    if context then
                        local child = context.needsDestination and beginCell(context.dest)
                        if child then
                            frame.pending = context
                            stack[#stack + 1] = child
                        else
                            frame.processDoor(context)
                        end
                    end
                end
            end
        end
        return cellsWithObjects[cell.id]
    end


    for _, cell in pairs(world.cells) do


        checkpoint()
        local hasObjects = processCell(cell)

        if cell.isExterior and (hasObjects or not nonContentCells[cell.id]) then
            local gridX = cell.gridX
            local gridY = cell.gridY

            minGridX = math.min(minGridX, gridX)
            maxGridX = math.max(maxGridX, gridX)
            minGridY = math.min(minGridY, gridY)
            maxGridY = math.max(maxGridY, gridY)
        end
    end

    for _, list in pairs(entrancesWithCellName) do

        checkpoint()
        if #list <= 2 then
            for _, dt in pairs(list) do
                checkpoint()
                dt.name = dt.fName
            end
        end
    end


    local mergeDist = 1024 * 1024
    local nameGroups = {}
    for _, lst in pairs(entrances) do
        checkpoint()
        for _, dt in pairs(lst) do
            checkpoint()
            nameGroups[dt.name] = nameGroups[dt.name] or {}
            table.insert(nameGroups[dt.name], dt)
        end
    end

    for _, entries in pairs(nameGroups) do

        checkpoint()
        local used = {}
        local entryCount = #entries
        for i = 1, entryCount do
            checkpoint()
            if not used[i] then
                used[i] = true

                local entry = entries[i]
                local cluster = { entry }
                local expanded = true
                for k = 1, 5 do
                    checkpoint()
                    expanded = false
                    for j = 1, entryCount do
                        checkpoint()
                        if not used[j] then
                            local ej = entries[j]
                            if ej.isEx and entry.isEx or ej.cId == entry.cId then
                                for _, cm in pairs(cluster) do
                                    checkpoint()
                                    local dx = cm.pos.x - ej.pos.x
                                    local dy = cm.pos.y - ej.pos.y
                                    if dx * dx + dy * dy <= mergeDist then
                                        used[j] = true
                                        table.insert(cluster, ej)
                                        expanded = true
                                        break
                                    end
                                end
                            end
                        end
                    end

                    if not expanded then break end
                end

                local cx, cy = 0, 0
                for _, e in pairs(cluster) do
                    checkpoint()
                    cx = cx + e.pos.x
                    cy = cy + e.pos.y
                end
                local clusterSize = #cluster
                cx = cx / clusterSize
                cy = cy / clusterSize

                local bestEntry = cluster[1]
                local bestDist = math.huge
                for _, e in pairs(cluster) do
                    checkpoint()
                    local dx = e.pos.x - cx
                    local dy = e.pos.y - cy
                    local dsq = dx * dx + dy * dy
                    if dsq < bestDist then
                        bestDist = dsq
                        bestEntry = e
                    end
                end

                local repDt = bestEntry

                for _, e in pairs(cluster) do

                    checkpoint()
                    if e ~= repDt then
                        e.pHash = repDt.dHash
                    end
                end
            end
        end
    end

    for _, data in pairs(populationMap) do

        checkpoint()
        local x = data.x
        local y = data.y
        for _, dt in pairs(data.m) do
            checkpoint()
            local maxDist = dt.fName == dt.name and 2048 or 6144
            local steps = math.ceil(maxDist / 1024)

            local isolated = true
            local function check()
                local cnt = 0
                for i = x - steps, x + steps do
                    checkpoint()
                    for j = y - steps, y + steps do
                        checkpoint()
                        local popData = populationMap[i * 262144 + j]
                        if popData then
                            for _, d in pairs(popData.m) do
                                checkpoint()
                                local idDist = commonData.distance2D(d.pos, dt.pos) < maxDist
                                if d ~= dt and idDist then
                                    if (d.name ~= dt.name and d.pN and dt.name ~= d.pN) then
                                        isolated = false
                                        return
                                    elseif cnt < 1 and not d.pN and not dt.pN or d.name == dt.name then
                                        cnt = cnt + 1
                                    else
                                        isolated = false
                                        return
                                    end
                                end
                            end
                        end
                    end
                end
            end
            check()

            dt.isIsl = isolated or nil
        end
    end


    local function processLines(lines, xPosMul, heightMul)
        local xLim = xPosMul * 20
        local xOverlapMul = 0
        local maxShift = heightMul

        local function applyShift(el, mx, my)
            local nx = el.posX + mx
            local ny = el.posY + my
            if nx - el.origX > maxShift then nx = el.origX + maxShift end
            if nx - el.origX < -maxShift then nx = el.origX - maxShift end
            if ny - el.origY > maxShift then ny = el.origY + maxShift end
            if ny - el.origY < -maxShift then ny = el.origY - maxShift end
            el.posX = nx
            el.posY = ny
        end

        for _, lineElems in pairs(lines) do

            checkpoint()
            local elemCount = #lineElems
            if elemCount > 1 then
                for _, el in pairs(lineElems) do
                    checkpoint()
                    if el.origX == nil then
                        el.origX = el.posX
                        el.origY = el.posY
                        el.len = math.max(6, stringLib.length(el.name))
                    end
                end

                table.sort(lineElems, function(a, b) return a.posX < b.posX end)

                for _ = 1, 8 do

                    checkpoint()
                    local moved = false
                    for i = 1, elemCount - 1 do
                        checkpoint()
                        local el1 = lineElems[i]
                        local xLim2 = el1.posX + xLim
                        for j = i + 1, elemCount do
                            checkpoint()
                            local el2 = lineElems[j]
                            if el2.posX > xLim2 then break end

                            local xPosDiff = (el1.len * 0.5 + el2.len * 0.5) * xPosMul
                            local heightDiff = heightMul * (1 + 0.05 * (el1.count + el2.count - 2))
                            local xDiff = math.abs(el2.posX - el1.posX)
                            local yDiff = math.abs(el2.posY - el1.posY)

                            if xDiff < xPosDiff and yDiff < heightDiff then
                                local overlapX = xPosDiff - xDiff
                                local overlapY = heightDiff - yDiff

                                local dx = overlapX * xOverlapMul * 0.5
                                local dy = overlapY * (1 - xOverlapMul) * 0.5

                                local sx = (el2.posX >= el1.posX) and 1 or -1
                                local sy = (el2.posY >= el1.posY) and 1 or -1

                                applyShift(el1, -sx * dx, -sy * dy)
                                applyShift(el2, sx * dx, sy * dy)
                                moved = true
                            end
                        end
                    end
                    if not moved then break end
                    table.sort(lineElems, function(a, b) return a.posX < b.posX end)
                end
            end
        end
    end
    processLines(cellNameLines, 4096, 4096)

    this.cellNameData = cellNames


    local regNameLines = {}
    local regNames = {}
    for _, dt in pairs(regionNameData) do
        checkpoint()
        if dt.count >= 1 then

            local posX = (dt.minX + (dt.maxX - dt.minX) / 2) * 8192 + 4096
            local posY = (dt.minY + (dt.maxY - dt.minY) / 2) * 8192 + 4096

            local cellDt = {
                name = dt.name,
                count = dt.count,
                posX = posX,
                posY = posY,
            }
            regNames[dt.name] = cellDt

            local hash = math.floor(posY / 8192)
            for i = -1, 1 do
                checkpoint()
                local h = hash + i
                regNameLines[h] = regNameLines[h] or {}
                table.insert(regNameLines[h], cellDt)
            end

        end
    end

    processLines(regNameLines, 8192, 8192)

    this.regionNameData = regNames


    for cellId, list in pairs(entrances) do


        checkpoint()
        entrances[cellId] = tableLib.values(list)
    end
    this.entrances = entrances

    this.grid = {min = {x = minGridX, y = minGridY}, max = {x = maxGridX, y = maxGridY}}

    this.transport = transportModule.buildTransportData(transportRecords, transportCellData, entrances, checkpoint)
end


local function acceptOffer(token)
    local order = token and offers[token]
    if not order or order <= activeOrder then return false end
    activeOrder = order
    -- A safe-init menu timer may return an older request first. Keep newer
    -- offers available, while making older/duplicate starts permanently inert.
    for candidate, issued in pairs(offers) do
        if issued <= order then offers[candidate] = nil end
    end
    this.cancelBuild()
    activeToken = token
    return true
end

function this.globalBuildData(playerRef, options)
    options = options or {}
    if not acceptOffer(options._rebuildToken) then return false end
    local result = {cellCount = 0, getCellCount = this.getCellCount, worldNameById = {}}
    activeBuild = {token = activeToken, player = playerRef, options = options, start = core.getRealTime()}
    activeBuild.job = rebuildJob.new(function(checkpoint)
        buildData(options, result, checkpoint)
        result.contentFileCount = #core.contentFiles.list
        buildWorldNameByIdData(result)
        result.getCellCount = nil
        return result
    end, core.getRealTime, 0.002, 128)
    return true
end

-- OpenMW calls global onUpdate even when paused (duration zero).
-- No continuation events or coroutine state are serialized into saved games.
function this.advanceBuild()
    local build = activeBuild
    if not build then return end
    if not this.isCurrentRequest(build.token) or not build.player:isValid() then this.cancelBuild(); return end
    local status, result, err = build.job.step()
    if status == "pending" then return end
    activeBuild = nil
    if status ~= "complete" then
        log("Map rebuild failed; previous complete data retained: " .. tostring(err))
        build.player:sendEvent("AdvWMap:showMessage", "World map rebuilding failed. Previous map data has been kept.")
        return
    end
    local payload = {}
    for _, key in ipairs(dataKeys) do payload[key] = result[key] end
    payload.options, payload.plId, payload._rebuildToken = build.options, build.player.id, build.token
    local ok, failure = pcall(function() build.player:sendEvent("AdvWMap:updateMapData", payload) end)
    if not ok then
        log("Map rebuild could not be delivered: " .. tostring(failure))
        return
    end
    completedBuild = {token = build.token, result = result}
    log(string.format("Map data prepared in %.2f s across %d slices (largest %.2f ms)",
        core.getRealTime() - build.start, build.job.slices, build.job.maxSlice * 1000))
end

function this.commitBuild(token)
    if not this.isCurrentRequest(token) or not completedBuild or completedBuild.token ~= token then return false end
    publish(completedBuild.result)
    completedBuild = nil
    return true
end

-- Only the player can originate a fresh request. Unsaved pending nonces
-- make old init/results inert after load and during superseding requests.
function this.requestInit(playerRef, options)
    requestSerial = requestSerial + 1
    pendingRequest = string.format("%.17g/%d", core.getRealTime(), requestSerial)
    activeToken, lastResultToken = nil, nil
    local request = {}
    for key, value in pairs(options or {}) do request[key] = value end
    request._requestToken, request._rebuildToken = pendingRequest, nil
    core.sendGlobalEvent("AdvWMap:initMapData", {plRef = playerRef, options = request})
end

function this.globalInit(playerRef, options)
    if not playerRef or not options or not options._requestToken then return end
    requestSerial = requestSerial + 1
    local token = string.format("%.17g/%d", core.getRealTime(), requestSerial)
    local request = {}
    for key, value in pairs(options) do request[key] = value end
    request._rebuildToken = token
    offers[token] = requestSerial
    offerOrder[#offerOrder + 1] = token
    if #offerOrder > 128 then offers[table.remove(offerOrder, 1)] = nil end
    -- Offering an init reply cannot cancel a valid build. The current player
    -- must accept it and return the token before global state changes.
    playerRef:sendEvent("AdvWMap:initMapData", {cellCount = this.getCellCount(), options = request})
end


function this.playerInit(playerRef, cellCount, options)
    options = options or {}
    if not pendingRequest or options._requestToken ~= pendingRequest or not options._rebuildToken then return false end
    pendingRequest = nil
    activeToken = options._rebuildToken
    local stor = storage.playerSection(commonData.mapDataStorageName)

    local shouldRebuild
    if not options.skipRebuild then
        shouldRebuild = options.force or stor:get("version") ~= this.version or stor:get("cellCount") ~= cellCount or
            stor:get("contentFileCount") ~= #core.contentFiles.list
    else
        shouldRebuild = false
    end

    if shouldRebuild then
        local dataSettingStorage = storage.playerSection(commonData.configDataSectionName)
        options.buildCellWorldMapData = dataSettingStorage:get("data.initializer") == commonData.dataInitializerTypes[6]

        if not commonData.isSaveBloatFixed() and require("scripts.advanced_world_map.config.config").data.data.safeInit then
            types.Player.sendMenuEvent(playerRef, "AdvWMap:startDataRebuilding", {plId = playerRef.id, options = options})
        else
            core.sendGlobalEvent("AdvWMap:rebuildMapData", {plId = playerRef.id, options = options})
        end
        return false
    else
        local data = stor:asTable()

        this.cellNameData = data.cellNameData or {}
        this.regionNameData = data.regionNameData or {}
        this.entrances = data.entrances or {}
        this.cellNameById = data.cellNameById or {}
        this.validCellsWithoutName = data.validCellsWithoutName or {}
        this.grid = data.grid or {min = {x = 0, y = 0}, max = {x = 0, y = 0}}
        this.worldMapTileRectangles = data.worldMapTileRectangles or {}
        this.transport = data.transport or {}
        this.cellCount = data.cellCount or 0
        this.contentFileCount = data.contentFileCount or 0

        core.sendGlobalEvent("AdvWMap:updateMapData", {
            _rebuildToken = activeToken,
            cellNameData = this.cellNameData,
            regionNameData = this.regionNameData,
            entrances = this.entrances,
            cellNameById = this.cellNameById,
            grid = this.grid,
            worldMapTileRectangles = this.worldMapTileRectangles,
            transport = this.transport,
            validCellsWithoutName = this.validCellsWithoutName,
        })

        if options then
            playerRef:sendEvent("AdvWMap:processMapDataOptions", options)
        end

        buildWorldNameByIdData()

        initialized = true

        return true
    end
end



function this.updateData(playerRef, data)
    if not this.isCurrentRequest(data._rebuildToken) or data._rebuildToken == lastResultToken then return false end
    local candidate = {}
    for _, key in ipairs(dataKeys) do candidate[key] = data[key] end
    buildWorldNameByIdData(candidate)
    local stor = storage.playerSection(commonData.mapDataStorageName)
    local old = stor:asTable()
    local values = {}
    for _, key in ipairs(dataKeys) do values[key] = candidate[key] end
    values.contentFileCount, values.version, values.apiVersion = #core.contentFiles.list, this.version, core.API_REVISION
    local keys = {}
    for key in pairs(values) do keys[#keys + 1] = key end
    local ok, failure = pcall(function()
        for _, key in ipairs(keys) do stor:set(key, values[key]) end
    end)
    if not ok then
        local restored, restoreError = pcall(function()
            for _, key in ipairs(keys) do stor:set(key, old[key]) end
        end)
        log("Map storage update failed: " .. tostring(failure)
            .. (restored and "; previous storage restored" or "; rollback failed: " .. tostring(restoreError)))
        return false
    end
    lastResultToken = data._rebuildToken
    publish(candidate)
    core.sendGlobalEvent("AdvWMap:rebuildCommitted", {token = data._rebuildToken})
    log("Map data updated and saved to storage")
    if not commonData.isSaveBloatFixed() and require("scripts.advanced_world_map.config.config").data.data.safeInit then
        types.Player.sendMenuEvent(playerRef, "AdvWMap:finishDataRebuilding", {plId = data.plId, options = data.options})
    else
        core.sendGlobalEvent("AdvWMap:processMapDataOptions", {plId = data.plId, options = data.options})
    end
    return true
end


function this.loadMapData(data)
    if not acceptOffer(data._rebuildToken) then return false end
    this.cellNameData = data.cellNameData or {}
    this.regionNameData = data.regionNameData or {}
    this.entrances = data.entrances or {}
    this.cellNameById = data.cellNameById or {}
    this.validCellsWithoutName = data.validCellsWithoutName or {}
    this.grid = data.grid or {min = {x = 0, y = 0}, max = {x = 0, y = 0}}
    this.worldMapTileRectangles = data.worldMapTileRectangles or {}
    this.transport = data.transport or {}

    buildWorldNameByIdData()

    initialized = true
end


function this.isInitialized()
    return initialized
end


return this