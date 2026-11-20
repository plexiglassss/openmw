local commonData = require("scripts.advanced_world_map.common")
local tableLib = require("scripts.advanced_world_map.utils.table")
local cellHelper = require("scripts.advanced_world_map.helpers.cell")


local this = {}


---@param trRecords table<string, any>
---@param trCells table<any, any[]>
---@param entrances table<string, advancedWorldMap.dynamicDataHandler.entranceData[]>
function this.buildTransportData(trRecords, trCells, entrances, checkpoint)
    checkpoint = checkpoint or function() end
    local transportNpcs = {}
    local nodes = {}
    local exitNodes = {}
    local transportClass = {
        ["caravaner"] = 1,
        ["shipmaster"] = 2,
        ["t_mw_riverstriderservice"] = 2,
        ["guild guide"] = 3,
        ["gondolier"] = 4,
    }
    local transport = {nodes = nodes, actors = transportNpcs, data = {}}
    for _, id in pairs(transportClass) do
        checkpoint()
        transport.data[id] = {}
    end
    transport.data[-1] = {}

    local function getNodeId(pos, tp)
        local unknownTypeNode
        local unknownTypeNodeId
        local unknownTypeNodeDist = math.huge
        local node
        local nodeId
        local dist = math.huge
        for i, nodeDt in pairs(nodes) do
            checkpoint()
            local d = commonData.distance2D(nodeDt.p, pos)

            if nodeDt.tp == tp or (tp == -1 and nodeDt.tp ~= -1) then
                if d < dist then
                    dist = d
                    node = nodeDt
                    nodeId = i
                end
            end

            if nodeDt.tp == -1 and tp ~= -1 then
                if d < unknownTypeNodeDist then
                    unknownTypeNodeDist = d
                    unknownTypeNode = nodeDt
                    unknownTypeNodeId = i
                end
            end
        end

        if node and dist < 3072 then
            return nodeId, node
        end

        if unknownTypeNode and unknownTypeNodeDist < 3072 then
            unknownTypeNode.tp = tp
            return unknownTypeNodeId, unknownTypeNode
        end

        local ind = #nodes + 1
        nodes[ind] = {tp = tp, p = pos, ls = {}}
        return ind, nodes[ind]
    end

    local function processRec (rec)
        if not rec.travelDestinations then
            return
        end

        local data = {tp = transportClass[rec.class or ""] or -1, ns = {}}

        local function processDest (destDt)
            if not destDt.cellId then return end
            if destDt.cellId:find(commonData.exteriorCellLabel) then
                local pos = destDt.position
                local nodeId = getNodeId(pos, data.tp)

                data.ns[nodeId] = true
            else
                local cachedExitNode = exitNodes[destDt.cellId]
                if cachedExitNode then
                    data.ns[cachedExitNode] = true
                else
                    local exits = cellHelper.findExitPoss(destDt.cellId, entrances, nil, nil, nil, checkpoint)
                    if not exits or not exits[1] then return end

                    local exit = exits[1]
                    local nodeId = getNodeId(exit, data.tp)
                    data.ns[nodeId] = true

                    exitNodes[destDt.cellId] = nodeId
                end
            end
        end

        for _, destDt in pairs(rec.travelDestinations) do

            checkpoint()
            processDest(destDt)
        end

        if next(data.ns) then
            data.ns = tableLib.keys(data.ns)
            transportNpcs[rec.id] = data
        end
    end


    for _, rec in pairs(trRecords) do


        checkpoint()
        processRec(rec)
    end


    local function processCell (cell, actors)
        if not cell.id then return end

        local processActor = function (actor)
            local transporterData = transportNpcs[actor.recordId]
            if not transporterData then return end

            local actorNodeId
            if cell.isExterior then
                actorNodeId = getNodeId(actor.position, transporterData.tp)
            else
                local cachedExitNode = exitNodes[cell.id]
                if cachedExitNode then
                    actorNodeId = cachedExitNode
                else
                    local exits = cellHelper.findExitPoss(cell.id, entrances, nil, nil, nil, checkpoint)
                    if not exits or not exits[1] then return end

                    local exit = exits[1]
                    local nodeId = getNodeId(exit, transporterData.tp)
                    actorNodeId = nodeId

                    exitNodes[cell.id] = nodeId
                end
            end

            if actorNodeId then
                local actorNode = nodes[actorNodeId]
                if actorNode then
                    actorNode.ars = actorNode.ars or {}
                    table.insert(actorNode.ars, actor.recordId)

                    for _, nodeId in pairs(transporterData.ns) do

                        checkpoint()
                        if nodeId ~= actorNodeId then
                            local nDt = nodes[nodeId]
                            if nDt then
                                if nDt.tp == -1 then
                                    nDt.tp = actorNode.tp
                                end

                                actorNode.ls[nodeId] = true
                            end
                        end
                    end

                end
            end
        end

        for _, actor in pairs(actors) do

            checkpoint()
            processActor(actor)
        end
    end


    for cell, actors in pairs(trCells) do


        checkpoint()
        processCell(cell, actors)
    end

    for i, nodeDt in pairs(nodes) do

        checkpoint()
        nodeDt.ls = tableLib.keys(nodeDt.ls)
        table.insert(transport.data[nodeDt.tp], i)
    end

    return transport
end


return this