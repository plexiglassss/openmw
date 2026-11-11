-- Rank independent groups with the same comparator and Lua runtime as the main menu.
-- The owner retains the records and sends the existing menu/request ID when the order is ready.


return function(core)
    local submit = core.loadWorker and core.loadWorker("scripts.quest_guider_lite.rankQuestPositions")
    local pending = {}
    local function ordinary(groups)
        for _, group in ipairs(groups) do
            table.sort(group, function(a,b) return (a.distanceToPlayer or math.huge) < (b.distanceToPlayer or math.huge) end)
        end
    end
    local function cancel(player, menu)
        for i = #pending, 1, -1 do
            if pending[i].player == player and pending[i].menu == menu then
                pending[i].job:cancel()
                table.remove(pending, i)
            end
        end
    end
    local function sort(groups, player, menu, publish)
        local size = 0
        for _, group in ipairs(groups) do size = size + #group end
        if submit and size >= 128 and size + #groups <= 16384 then
            local input = {}
            for _, group in ipairs(groups) do
                input[#input+1] = #group
                for _, row in ipairs(group) do input[#input+1] = row.distanceToPlayer or math.huge end
            end
            local job = submit(input)
            if job then
                pending[#pending+1] = {job=job, groups=groups, player=player, menu=menu, publish=publish}
                return
            end
        end
        ordinary(groups)
        publish()
    end
    local function update()
        while pending[1] do
            local p = pending[1]
            local status, result = p.job:poll()
            if status == 'pending' then return end
            table.remove(pending, 1)
            if p.player:isValid() and status ~= 'cancelled' then
                if status == 'ready' then
                    local cursor = 1
                    for _, group in ipairs(p.groups) do
                        local rows = {}
                        for i=1,#group do rows[i] = group[result[cursor]]; cursor = cursor + 1 end
                        for i=1,#rows do group[i] = rows[i] end
                    end
                else
                    ordinary(p.groups)
                end
                p.publish()
            end
        end
    end
    local function clear()
        for _, p in ipairs(pending) do p.job:cancel() end
        pending = {}
    end
    return {sort=sort, update=update, cancel=cancel, clear=clear}
end
