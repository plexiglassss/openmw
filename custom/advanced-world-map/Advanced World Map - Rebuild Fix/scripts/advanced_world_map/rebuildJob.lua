-- Cooperative work owned by the global script, never stored in a save.
-- Native queries and sorting are indivisible, so the time budget is soft.
local M = {}

function M.new(build, clock, seconds, operations)
    local job = { status = 'pending', slices = 0, maxSlice = 0 }
    local deadline, remaining
    local function checkpoint()
        remaining = remaining - 1
        if remaining <= 0 or clock() >= deadline then
            coroutine.yield()
        end
    end
    local thread = coroutine.create(function() return build(checkpoint) end)

    function job.step()
        if job.status ~= 'pending' then return job.status, job.result, job.error end
        local start = clock()
        deadline, remaining = start + seconds, operations
        local ok, result = coroutine.resume(thread)
        job.slices = job.slices + 1
        job.maxSlice = math.max(job.maxSlice, clock() - start)
        if not ok then
            job.status, job.error, thread = 'failed', tostring(result), nil
        elseif coroutine.status(thread) == 'dead' then
            job.status, job.result, thread = 'complete', result, nil
        end
        return job.status, job.result, job.error
    end

    function job.cancel()
        job.status, job.result, thread = 'cancelled', nil, nil
    end

    return job
end

return M
