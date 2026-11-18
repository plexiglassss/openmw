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
-- Page, group and action strings may each belong to a different localization context.
local function metadata(group, setting, argument, page, inputTypes, translate)
    local info
    if type(argument) == 'table' and type(argument.type) == 'string' and type(argument.key) == 'string' then
        local registry = inputTypes[argument.type]
        info = registry and registry[argument.key]
    end
    local function localized(source, key)
        if not source or not key or key == '' then return nil end
        return translate(source.l10n, key)
    end
    return {
        modId = group.page,
        owner = localized(page, page and page.name) or ('Unregistered page: ' .. group.page),
        section = localized(group, group.name),
        name = localized(info, info and info.name) or localized(group, setting.name) or setting.key,
        description = localized(group, setting.description) or localized(info, info and info.description),
    }
end

return { conflict = conflict, metadata = metadata }
