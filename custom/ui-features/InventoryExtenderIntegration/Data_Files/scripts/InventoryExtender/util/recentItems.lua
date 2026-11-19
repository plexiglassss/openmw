-- Reuse inventory refresh scans. No new per-frame inventory polling or storage.
-- Record totals avoid treating stack splits/merges as acquisitions.
local M = { revision = 0 }
local counts, marked = nil, {}
local hoverId, hoverRecord, hoverSeconds = nil, nil, 0

function M.load(data)
    hoverId, hoverRecord, hoverSeconds = nil, nil, 0
    counts = data and data.counts or nil
    marked = data and data.marked or {}
    M.revision = M.revision + 1
end

function M.save()
    return { counts = counts, marked = marked }
end

function M.observe(items)
    local nextCounts = {}
    for _, item in ipairs(items) do
        nextCounts[item.recordId] = (nextCounts[item.recordId] or 0) + item.count
    end
    local changed = false
    if counts then
        for id, count in pairs(nextCounts) do
            if count > (counts[id] or 0) and not marked[id] then
                marked[id], changed = true, true
            end
        end
        for id in pairs(marked) do
            if not nextCounts[id] then marked[id], changed = nil, true end
        end
    end
    counts = nextCounts
    if changed then M.revision = M.revision + 1 end
end

function M.isRecent(item)
    return marked[item.recordId] == true
end

function M.clear()
    hoverId, hoverRecord, hoverSeconds = nil, nil, 0
    marked = {}
    M.revision = M.revision + 1
end

-- Caller supplies the visible, owned inventory tooltip target. No inventory scan.
-- Timer is transient: focus changes and load/reset never inherit elapsed time.
function M.updateHover(item, dt)
    if not item or not M.isRecent(item) or dt < 0 or dt > 0.25 then
        hoverId, hoverRecord, hoverSeconds = nil, nil, 0
        return false
    end
    if hoverId ~= item.id or hoverRecord ~= item.recordId then
        hoverId, hoverRecord, hoverSeconds = item.id, item.recordId, 0
        return false
    end
    hoverSeconds = hoverSeconds + dt
    if hoverSeconds + 1e-9 < 1 then return false end
    marked[item.recordId] = nil
    hoverId, hoverRecord, hoverSeconds = nil, nil, 0
    M.revision = M.revision + 1
    return true
end

return M
