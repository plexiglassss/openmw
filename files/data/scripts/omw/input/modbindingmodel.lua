-- Pure conflict logic; adapters declare how their actual input handlers match keys.
local function overlap(a, b)
    if not a or not b then return nil end
    for _, x in ipairs(a) do
        for _, y in ipairs(b) do
            if x == y or x == '*' or y == '*' then return true end
        end
    end
    return false
end
local function conflict(a, b)
    if not a.button or not b.button or a.device ~= b.device or a.button ~= b.button then return 0 end
    local contexts = overlap(a.contexts, b.contexts)
    if contexts == false then return 0 end
    local am, bm = a.modifiers, b.modifiers
    if am and bm and am ~= 'any' and bm ~= 'any' and am ~= bm then return 0 end
    if contexts == nil or not am or not bm then return 1 end
    return 2
end
return { conflict = conflict }
