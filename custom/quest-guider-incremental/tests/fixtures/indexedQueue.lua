-- QGL Indexed Data 1.5: priority tracking, sharing the existing 2 ms soft budget.
-- Individual native calls and YAML decodes remain non-preemptible.
local core = require('openmw.core')
local this = {}
local BUDGET, MAX_CHECKPOINTS, MAX_COMPLETED = 0.002, 256, 64
local lanes, keys, revisions, revision, active, deadline, checkpoints
local counters, announced, lastReport, reportedPending
local function lane() return {jobs={}, first=1, last=0} end
function this.clear()
    lanes, keys, revisions, revision = {lane(), lane()}, {}, {}, 0
    active, announced, reportedPending = nil, false, false
    lastReport = core.getRealTime()
    counters = {completed=0, yielded=0, cancelled=0, restarted=0, errors=0,
        updates=0, maxUpdate=0, added=0, peakPending=0, maxWait=0,
        maxLatency=0, maxSpan=0, maxValidate=0, overruns=0}
end
this.clear()
local function pending()
    return lanes[1].last-lanes[1].first+1 + lanes[2].last-lanes[2].first+1
end
function this.cancel(key)
    local job=keys[key]
    if job then job.cancelled=true; keys[key]=nil end
end
function this.cancelGroup(group)
    for key,job in pairs(keys) do
        if job.group==group then job.cancelled=true; keys[key]=nil end
    end
end
-- Existing callers coalesce activations; tracking replaces superseded requests.
function this.add(key, callback, validate, group, options)
    options=options or {}
    if keys[key] then
        if not options.replace then return false end
        this.cancel(key)
    end
    local q=lanes[options.priority and 1 or 2]
    local job={key=key, callback=callback, validate=validate, group=group,
        enqueuedAt=core.getRealTime(), trace=options.trace, label=options.label,
        work=0, maxSlice=0, slices=0, phases={}, onFinish=options.onFinish}
    q.last=q.last+1; q.jobs[q.last]=job; keys[key]=job
    counters.added=counters.added+1
    counters.peakPending=math.max(counters.peakPending,pending())
    return true
end
function this.invalidate(group)
    if group then revisions[group]=(revisions[group] or 0)+1
    else revision=revision+1 end
end
local function noteSpan(job,now)
    local elapsed=now-job.lastCheckpoint
    if elapsed>counters.maxSpan then
        counters.maxSpan,counters.spanKey,counters.spanLabel=elapsed,job.key,job.label
    end
    job.lastCheckpoint=now
end
function this.checkpoint(label)
    if not active or coroutine.running()~=active.thread then return end
    local now=core.getRealTime()
    noteSpan(active,now); active.label=label; checkpoints=checkpoints+1
    if checkpoints>=MAX_CHECKPOINTS or now>=deadline then coroutine.yield() end
end
local function workTime(job) return job.work+core.getRealTime()-job.resumedAt end
function this.clock()
    if active and coroutine.running()==active.thread then return workTime(active) end
    return core.getRealTime()
end
-- Inclusive function timings exclude suspension between frames. Fixed labels
-- avoid allocating one counter for every item/reference. Nil returns survive.
function this.measure(label,fn,...)
    local job=active
    if not job or not job.trace or coroutine.running()~=job.thread then return fn(...) end
    this.checkpoint(label)
    local started=workTime(job)
    local function finish(...)
        local elapsed=math.max(0,workTime(job)-started)
        local s=job.phases[label] or {calls=0,total=0,max=0}
        s.calls,s.total,s.max=s.calls+1,s.total+elapsed,math.max(s.max,elapsed)
        job.phases[label]=s
        this.checkpoint(label..' done')
        return ...
    end
    return finish(fn(...))
end
function this.wrap(label,fn) return function(...) return this.measure(label,fn,...) end end
local function reportJob(job,status)
    if not job.trace then return end
    print(string.format('[QGL Tracking 1.5] %s status=%s slices=%d work=%.3fms maxSlice=%.3fms latency=%.3fs',
        job.key,status,job.slices,job.work*1000,job.maxSlice*1000,core.getRealTime()-job.enqueuedAt))
    local ordered={}
    for label,s in pairs(job.phases) do ordered[#ordered+1]={label=label,stats=s} end
    table.sort(ordered,function(a,b) return a.stats.total>b.stats.total end)
    for _,p in ipairs(ordered) do
        local s=p.stats
        print(string.format('[QGL Tracking 1.5] phase=%s calls=%d work=%.3fms max=%.3fms',
            p.label,s.calls,s.total*1000,s.max*1000))
    end
end
local function discard(q,status)
    local job=q.jobs[q.first]
    if keys[job.key]==job then keys[job.key]=nil end
    reportJob(job,status)
    if job.onFinish then
        local ok,err=pcall(job.onFinish,status)
        if not ok then
            counters.errors=counters.errors+1
            print('[QGL Indexed] Completion failed ('..job.key..'): '..tostring(err))
        end
    end
    q.jobs[q.first]=nil; q.first=q.first+1
    if q.first>q.last then q.jobs,q.first,q.last={},1,0 end
end
local function nextLane()
    if lanes[1].first<=lanes[1].last then return lanes[1] end
    if lanes[2].first<=lanes[2].last then return lanes[2] end
end
function this.update()
    if not nextLane() then return end
    if not announced then
        print('[QGL Indexed] Queue 1.5 active: priority tracking; shared 2 ms soft budget; wait/span reporting.')
        announced=true; this.report('start'); reportedPending=true
    end
    local started=core.getRealTime()
    deadline,checkpoints=started+BUDGET,0
    local completed=0
    while nextLane() and completed<MAX_COMPLETED do
        local q=nextLane()
        local job=q.jobs[q.first]
        local ok,usable,context=true,not job.cancelled,nil
        local validateStarted=core.getRealTime()
        if usable and job.validate then ok,usable,context=pcall(job.validate) end
        counters.maxValidate=math.max(counters.maxValidate,core.getRealTime()-validateStarted)
        if not ok then
            counters.errors=counters.errors+1
            print('[QGL Indexed] Validation failed ('..job.key..'): '..tostring(usable))
            discard(q,'error'); completed=completed+1
        elseif usable=='wait' then
            break -- data initialization is pending; retain the request
        elseif not usable then
            counters.cancelled=counters.cancelled+1
            discard(q,'cancelled'); completed=completed+1
        else
            local groupRevision=revisions[job.group] or 0
            if job.thread and (job.context~=context or job.revision~=revision or job.groupRevision~=groupRevision) then
                job.thread=nil; counters.restarted=counters.restarted+1
            end
            if not job.thread then
                job.thread=coroutine.create(job.callback)
                job.context,job.revision,job.groupRevision=context,revision,groupRevision
            end
            if not job.startedAt then
                job.startedAt=core.getRealTime()
                counters.maxWait=math.max(counters.maxWait,job.startedAt-job.enqueuedAt)
            end
            active=job
            job.resumedAt=core.getRealTime(); job.lastCheckpoint=job.resumedAt
            local success,err=coroutine.resume(job.thread)
            local finished=core.getRealTime()
            noteSpan(job,finished)
            local elapsed=finished-job.resumedAt
            job.work,job.maxSlice,job.slices=job.work+elapsed,math.max(job.maxSlice,elapsed),job.slices+1
            active=nil
            if not success then
                counters.errors=counters.errors+1
                print('[QGL Indexed] Job failed ('..job.key..', '..tostring(job.label)..'): '..tostring(err))
                discard(q,'error'); completed=completed+1
            elseif coroutine.status(job.thread)=='dead' then
                counters.completed=counters.completed+1
                counters.maxLatency=math.max(counters.maxLatency,finished-job.enqueuedAt)
                discard(q,'complete'); completed=completed+1
            else
                counters.yielded=counters.yielded+1
                break
            end
        end
        if core.getRealTime()>=deadline or checkpoints>=MAX_CHECKPOINTS then break end
    end
    counters.updates=counters.updates+1
    local finished=core.getRealTime()
    local elapsed=finished-started
    counters.maxUpdate=math.max(counters.maxUpdate,elapsed)
    if elapsed>BUDGET+0.0001 then counters.overruns=counters.overruns+1 end
    if not nextLane() then
        if reportedPending or finished-lastReport>=15 then
            this.report('drained'); lastReport=finished; reportedPending=false
        end
    elseif finished-lastReport>=15 then
        this.report('periodic'); lastReport=finished; reportedPending=true
    end
end
function this.getStats()
    local result={}
    for k,v in pairs(counters) do result[k]=v end
    result.pending=pending()
    local q=nextLane()
    local job=q and q.jobs[q.first]
    result.oldest=job and math.max(0,core.getRealTime()-job.enqueuedAt) or 0
    result.headKey,result.headLabel=job and job.key,job and job.label
    result.budget=BUDGET
    return result
end
function this.report(reason)
    local s=this.getStats()
    print(string.format('[QGL Indexed] Queue 1.5 (%s): completed=%d pending=%d yields=%d restarts=%d cancelled=%d errors=%d maxUpdate=%.3fms peak=%d oldest=%.3fs maxWait=%.3fs maxLatency=%.3fs overruns=%d maxSpan=%.3fms maxValidate=%.3fms span=%s key=%s head=%s headPhase=%s',
        reason or 'save',s.completed,s.pending,s.yielded,s.restarted,s.cancelled,s.errors,s.maxUpdate*1000,
        s.peakPending,s.oldest,s.maxWait,s.maxLatency,s.overruns,s.maxSpan*1000,s.maxValidate*1000,
        tostring(s.spanLabel),tostring(s.spanKey),tostring(s.headKey),tostring(s.headLabel)))
end
return this
