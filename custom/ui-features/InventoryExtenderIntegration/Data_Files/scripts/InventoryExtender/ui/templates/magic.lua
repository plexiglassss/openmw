local ui = require('openmw.ui')
local auxUi = require('openmw_aux.ui')
local util = require('openmw.util')
local core = require('openmw.core')
local I = require('openmw.interfaces')
local types = require('openmw.types')
local omwself = require('openmw.self')
local async = require('openmw.async')
local ambient = require('openmw.ambient')

local omwConstants = require('scripts.omw.mwui.constants')

local BASE = require('scripts.InventoryExtender.ui.templates.base')
local helpers = require('scripts.InventoryExtender.util.helpers')
local constants = require('scripts.InventoryExtender.util.constants')

local configPlayer = require('scripts.InventoryExtender.config.player')

local v2 = util.vector2
local l10n = core.l10n('InventoryExtender')

local TOOLTIP_ITEM_ICON_SIZE = 64

local CE_ENCHANT_DISPLAY_MULT = 3

local READABLE_TEXT_WIDTH = 360
local READABLE_TEXT_CHAR_LIMIT = 700
local READABLE_TEXT_LINE_LIMIT = 12

local function trimText(text)
    text = tostring(text or '')
    text = text:gsub('^%s+', ''):gsub('%s+$', '')
    return text
end

local function decodeHtmlEntity(entity)
    local named = {
        amp = '&',
        lt = '<',
        gt = '>',
        quot = '"',
        apos = "'",
        nbsp = ' ',
    }
    if named[entity] then return named[entity] end
    local dec = entity:match('^#(%d+)$')
    if dec then
        local n = tonumber(dec)
        if n and n >= 32 and n <= 126 then
            return string.char(n)
        end
        return ' '
    end
    return ' '
end

local function stripBookMarkup(text)
    text = tostring(text or '')
    text = text:gsub('\r\n', '\n'):gsub('\r', '\n')

    -- Preserve useful breaks from Morrowind book markup, then remove the tags.
    text = text:gsub('<%s*[Bb][Rr]%s*/?%s*>', '\n')
    text = text:gsub('</%s*[Pp]%s*>', '\n')
    text = text:gsub('</%s*[Dd][Ii][Vv]%s*>', '\n')
    text = text:gsub('<%s*[Pp][^>]*>', '\n')
    text = text:gsub('<%s*[Dd][Ii][Vv][^>]*>', '\n')
    text = text:gsub('<[^>]->', '')

    text = text:gsub('&([^;]+);', decodeHtmlEntity)
    text = text:gsub('[ \t]+', ' ')
    text = text:gsub(' *\n *', '\n')
    text = text:gsub('\n\n\n+', '\n\n')
    return trimText(text)
end

local function limitReadableText(text)
    text = stripBookMarkup(text)
    if text == '' then return nil end

    local truncated = false
    if #text > READABLE_TEXT_CHAR_LIMIT then
        text = text:sub(1, READABLE_TEXT_CHAR_LIMIT)
        truncated = true
    end

    local lines = {}
    for line in (text .. '\n'):gmatch('(.-)\n') do
        table.insert(lines, line)
        if #lines >= READABLE_TEXT_LINE_LIMIT then
            truncated = true
            break
        end
    end

    text = table.concat(lines, '\n')
    text = trimText(text)
    if truncated and text ~= '' then
        text = text .. '...'
    end
    return text ~= '' and text or nil
end

local function getReadableItemPreview(item, itemRecord)
    local text
    pcall(function()
        if types.Book and types.Book.objectIsInstance(item) then
            text = itemRecord and itemRecord.text
            if (not text or text == '') and types.Book.records and item.recordId then
                local record = types.Book.records[item.recordId]
                text = record and record.text
            end
        end
    end)
    return limitReadableText(text)
end

local function formatTooltipNumber(value)
    value = tonumber(value) or 0
    local rounded = math.floor(value + 0.5)
    if math.abs(value - rounded) < 0.01 then
        return tostring(rounded)
    end
    local text = string.format('%.1f', value)
    if text:sub(-2) == '.0' then
        return text:sub(1, -3)
    end
    return text
end

local function getEnchantEffectCost(effect)
    if not effect or not effect.effect then return 0 end
    local baseEffect = effect.effect
    local baseCost = baseEffect.baseCost or 0
    if baseCost <= 0 then return 0 end

    local minMagnitude, maxMagnitude = 1, 1
    if baseEffect.hasMagnitude then
        minMagnitude = effect.magnitudeMin or 0
        maxMagnitude = effect.magnitudeMax or minMagnitude
    end

    local duration = baseEffect.hasDuration and (effect.duration or 1) or 1
    if not baseEffect.isAppliedOnce then
        duration = math.max(duration, 1)
    end

    local cost = duration * 0.1 * baseCost
    cost = cost * 0.5 * (minMagnitude + maxMagnitude)
    cost = cost + 0.05 * baseCost * (effect.area or 0)
    if effect.range == core.magic.RANGE.Target then
        cost = cost * 1.5
    end
    cost = cost * core.getGMST('fEffectCostMult')
    return math.max(0, cost)
end

local function getEnchantmentUsedCost(enchantment)
    if not enchantment then return 0 end
    local recordCost = tonumber(enchantment.cost) or 0

    local calculated = 0
    if enchantment.effects then
        for _, effect in ipairs(enchantment.effects) do
            calculated = calculated + getEnchantEffectCost(effect)
        end
    end

    if enchantment.type == core.magic.ENCHANTMENT_TYPE.ConstantEffect then
        if calculated > 0 then
            calculated = calculated * CE_ENCHANT_DISPLAY_MULT
        end
        return math.max(recordCost, calculated)
    end

    if recordCost > 0 then return recordCost end
    return calculated
end

local function getEnchantCapacityColor(used, capacity)
    local free = (capacity or 0) - (used or 0)
    local ratio = (capacity and capacity > 0) and (free / capacity) or 0
    if ratio > 0.6 then
        return util.color.rgb(0.45, 0.74, 0.50)
    elseif ratio > 0.3 then
        return util.color.rgb(0.74, 0.74, 0.45)
    else
        return util.color.rgb(1.0, 0.45, 0.45)
    end
end


local intRe

local Templates = {}

local function initValues()
    intRe = configPlayer.modIntegration.b_InterfaceReimagined

    Templates.HEADER_HEIGHT = 20
    Templates.BORDER_WIDTH_TOTAL = 4 * (intRe and 2 or 4)
    Templates.TEXT_SIZE = BASE.TEXT_SIZE
    Templates.LINE_HEIGHT = Templates.TEXT_SIZE + 2
    Templates.SECTION_DIVIDER_HEIGHT = Templates.LINE_HEIGHT
    Templates.SECTION_INDENT_L = 12
    Templates.SECTION_INDENT_R = 4
    Templates.BOX_OUTER_PADDING = 8
    Templates.BOX_INNER_PADDING = omwConstants.padding
    Templates.BORDER_THICKNESS = omwConstants.border
    Templates.MIN_HEIGHT = 4 + 
        Templates.HEADER_HEIGHT + 
        Templates.BORDER_WIDTH_TOTAL + 
        Templates.BOX_OUTER_PADDING * 2 + 
        Templates.BOX_INNER_PADDING * 2 + 
        Templates.LINE_HEIGHT * 3
    Templates.MIN_INNER_WIDTH = 60
    Templates.MIN_WIDTH = 
        Templates.MIN_INNER_WIDTH + 
        Templates.BORDER_WIDTH_TOTAL + 
        Templates.BOX_OUTER_PADDING * 2
end
initValues()

Templates.active = false
Templates.focusedLabel = nil
Templates.linesToProcess = {}
local lastMousePos = nil

Templates.createTexture = BASE.createTexture

configPlayer.onUpdate(function()
    initValues()
end)

Templates.interactive = function(props, layout, ctx)
    local function absToRel(absPos)
        local layerSize = ui.layers[ui.layers.indexOf('Notification')].size
        return v2(
            absPos.x / layerSize.x,
            absPos.y / layerSize.y
        )
    end

    local function createTooltip()
        if not props.tooltipFn then return nil end

        if ctx.modalElement then
            return nil
        end

        local okTooltip, tooltipLayout = pcall(function()
            return props.tooltipFn()
        end)
        if not okTooltip or not tooltipLayout then
            print('[InventoryExtender] tooltip failed: ' .. tostring(tooltipLayout))
            return nil
        end
        local okCreate, createdTooltip = pcall(function()
            return ui.create(tooltipLayout)
        end)
        if not okCreate or not createdTooltip then
            print('[InventoryExtender] tooltip ui.create failed: ' .. tostring(createdTooltip))
            return nil
        end
        ctx.activeTooltip = createdTooltip
        ctx.activeTooltip.layout.name = props.name
        if lastMousePos then
            ctx.activeTooltip.layout.props.anchor = v2(absToRel(lastMousePos).x, 0)
            ctx.activeTooltip.layout.props.position = v2(lastMousePos.x, lastMousePos.y + 32)
        end
        ctx.activeTooltip:update()
        return ctx.activeTooltip
    end

    local element = layout.layout and layout or ui.create(layout)
    if props.name then
        element.layout.name = props.name
    end

    element.layout.userData = element.layout.userData or {}
    element.layout.userData.interactive = true

    element.layout.events = element.layout.events or {}
    element.layout.events.mousePress = async:callback(function(e, layout)
        if e.button ~= 1 then
            return false
        end
        if props.onClick then
            if props.canClick and not props.canClick() then
                return false
            end
            element.layout.userData.pressed = true
            ambient.playSound('menu click')
            helpers.setInteractiveColor(element.layout)
            element:update()

            if props.parent then
                ctx.updateQueue[props.parent] = true
            end
            return true
        end
        return false
    end)
    element.layout.events.mouseRelease = async:callback(function(e, layout)
        if e.button ~= 1 then
            return false
        end
        if props.onClick then
            if not element.layout.userData.pressed then
                return false
            end
            element.layout.userData.pressed = false
            helpers.setInteractiveColor(element.layout)
            local result = props.onClick()
            element:update()

            if ctx.activeTooltip and ctx.activeTooltip.layout and ctx.activeTooltip.layout.name == props.name then
                auxUi.deepDestroy(ctx.activeTooltip)
                ctx.activeTooltip = createTooltip()
            end

            if props.parent then
                ctx.updateQueue[props.parent] = true
            end
            return result
        end
        return false
    end)
    element.layout.events.focusLoss = async:callback(function()
        ctx.focusedInteractiveDelayed = false
        element.layout.userData.hovering = false
        if props.onFocusLoss then
            props.onFocusLoss(element)
        end
        if props.tooltipFn then
            if ctx.activeTooltip and ctx.activeTooltip.layout then
                ctx.activeTooltip.layout.props.visible = false
                ctx.updateQueue[ctx.activeTooltip] = true
            end
        end

        if props.onClick then
            helpers.setInteractiveColor(element.layout)
            ctx.updateQueue[element] = true

            if props.parent then
                ctx.updateQueue[props.parent] = true
            end
        end
        return true
    end)
    element.layout.events.focusGain = async:callback(function(e, layout)
        ctx.focusedInteractiveDelayed = element
        if props.onFocusGain then
            props.onFocusGain(element)
        end
        if props.onClick then
            helpers.setInteractiveColor(element.layout)
            ctx.updateQueue[element] = true

            if props.parent then
                ctx.updateQueue[props.parent] = true
            end
        end
        return true
    end)
    element.layout.events.mouseMove = async:callback(function(e, layout)
        if props.onMouseMove then
            props.onMouseMove(e, layout, element)
        end
        element.layout.userData.hovering = true
        if props.tooltipFn then
            -- WindowManager uses this per-frame marker to distinguish a
            -- tooltip that is still under the pointer from a stale one left
            -- behind when focus moves to vanilla MWUI.
            ctx.tooltipMouseMoveThisFrame = true
            if not ctx.activeTooltip or not ctx.activeTooltip.layout then
                ctx.activeTooltip = createTooltip()
            elseif ctx.activeTooltip.layout.name ~= props.name then
                auxUi.deepDestroy(ctx.activeTooltip)
                ctx.activeTooltip = createTooltip()
            end
            if ctx.activeTooltip then
                ctx.activeTooltip.layout.props.visible = true
                local distToBottom = ui.layers[ui.layers.indexOf('Notification')].size.y - (e.position.y - e.offset.y)
                if distToBottom < ui.layers[ui.layers.indexOf('Notification')].size.y / 2 then
                    ctx.activeTooltip.layout.props.anchor = v2(absToRel(e.position).x, 1)
                    ctx.activeTooltip.layout.props.position = v2(e.position.x, e.position.y - 32)
                else
                    ctx.activeTooltip.layout.props.anchor = v2(absToRel(e.position).x, 0)
                    ctx.activeTooltip.layout.props.position = v2(e.position.x, e.position.y + 32)
                end
                ctx.activeTooltip:update()
                lastMousePos = e.position
            end
        end
        return true
    end)
    return element
end

Templates.progressBar = function(props)
    props.value = math.floor(props.value or 0)
    props.maxValue = math.floor(props.maxValue or 100)
    props.size = props.size or v2(100, Templates.LINE_HEIGHT)
    props.color = props.color or constants.Colors.RED
    props.textColor = props.textColor or constants.Colors.DEFAULT

    local percentage = props.maxValue ~= 0 and math.min(math.max(props.value / props.maxValue, 0), 1) or 1

    return {
        name = 'value',
        template = I.MWUI.templates.borders,
        props = {
            size = props.size,
        },
        content = ui.content {
            {
                type = ui.TYPE.Image,
                props = {
                    relativeSize = v2(percentage, 1),
                    resource = BASE.createTexture('textures/menu_bar_gray.dds'),
                    color = props.color,
                }
            },
            {
                template = BASE.textNormal,
                props = {
                    anchor = v2(0.5, 1),
                    relativePosition = v2(0.5, 1),
                    text = props.text or tostring(helpers.addSeparators(props.value) .. '/' .. helpers.addSeparators(props.maxValue)),
                    textColor = props.textColor,
                    textSize = Templates.TEXT_SIZE,
                    textAlignV = ui.ALIGNMENT.Center,
                }
            },
        }
    }
end

Templates.tooltip = function(padding, content, name)
    return {
        layer = 'Notification',
        name = name,
        template = BASE.boxSolid,
        props = {
        },
        content = ui.content {
            {
                name = 'padding',
                template = BASE.padding(padding),
                content = content or ui.content {},
            }
        }
    }
end

Templates.lineTooltip = function(text, name)
    return Templates.tooltip(4, ui.content {
        {
            template = BASE.textNormal,
            props = {
                text = text or '',
                autoSize = true,
            }
        }
    }, name)
end

local tooltipData = require('scripts.InventoryExtender.interop.tooltips')


local function tooltipItemIcon(icon, enchanted)
    local content = ui.content {}
    if icon and icon ~= '' then
        content:add({
            type = ui.TYPE.Image,
            props = {
                size = v2(TOOLTIP_ITEM_ICON_SIZE, TOOLTIP_ITEM_ICON_SIZE),
                resource = BASE.createTexture(icon),
            }
        })
    else
        content:add({
            type = ui.TYPE.Flex,
            props = {
                size = v2(TOOLTIP_ITEM_ICON_SIZE, TOOLTIP_ITEM_ICON_SIZE),
            }
        })
    end

    if enchanted then
        content:add({
            type = ui.TYPE.Image,
            props = {
                position = v2(TOOLTIP_ITEM_ICON_SIZE - 22, TOOLTIP_ITEM_ICON_SIZE - 22),
                size = v2(20, 20),
                resource = BASE.createTexture('textures/menu_icon_magic_mini.dds'),
                alpha = 0.85,
            }
        })
    end

    return {
        name = 'itemIcon',
        type = ui.TYPE.Container,
        props = {
            size = v2(TOOLTIP_ITEM_ICON_SIZE, TOOLTIP_ITEM_ICON_SIZE),
        },
        content = content,
    }
end

Templates.itemTooltip = function(item, showIcon, ctx)
    local function mergeExtraProps(props, extraProps)
        -- Some call paths / older patches can pass a number here instead of a
        -- property table. Do not let that kill the whole tooltip.
        if type(extraProps) == 'table' then
            for k, v in pairs(extraProps) do props[k] = v end
        elseif extraProps ~= nil then
            -- Treat a numeric third argument as a text-size shortcut. Anything
            -- else is ignored safely.
            if type(extraProps) == 'number' then
                props.textSize = extraProps
            end
        end
        return props
    end

    local function textNormal(name, text, extraProps)
        local props = mergeExtraProps({ text = text }, extraProps)
        return { name = name, template = BASE.textNormal, props = props }
    end
    local function textHeader(name, text, extraProps)
        local props = mergeExtraProps({ text = text }, extraProps)
        return { name = name, template = BASE.textHeader, props = props }
    end

    local itemRecord = item.type.record(item)
    local itemData = types.Item.itemData(item)

    local nameString = helpers.getItemName(item)
    if item.count > 1 then
        nameString = nameString .. ' (' .. helpers.addSeparators(item.count) .. ')'
    end

    local function getTooltipNameProps()
        local props = {
            textSize = Templates.TEXT_SIZE + 2,
            autoSize = true,
        }

        -- Make special items stand out without relying on unsupported glyphs.
        local equipped = false
        pcall(function()
            equipped = helpers.isItemEquipped(item, omwself)
        end)

        if equipped then
            props.textColor = constants.Colors.GOLD
        elseif itemRecord.enchant and itemRecord.enchant ~= '' then
            props.textColor = constants.Colors.BAR_MAGIC
        else
            props.textColor = constants.Colors.WHITE
        end

        return props
    end

    local nameProps = getTooltipNameProps()

    local innerContent = ui.content {}

    if not showIcon then
        innerContent:add(textHeader('name', nameString, nameProps)) 
    else
        -- Item icon tooltip layout: icon first, item name underneath.
        -- This follows the Inventory Manager-style vertical presentation instead
        -- of putting the item name inline beside the icon.
        innerContent:add({
            name = 'header',
            type = ui.TYPE.Flex,
            external = { stretch = 1 },
            props = {
                horizontal = false,
                arrange = ui.ALIGNMENT.Center,
                align = ui.ALIGNMENT.Center,
            },
            content = ui.content {
                tooltipItemIcon(itemRecord.icon, itemRecord.enchant and itemRecord.enchant ~= ''),
                BASE.intervalV(4),
                { template = I.MWUI.templates.horizontalLine, external = { stretch = 1 } },
                BASE.intervalV(4),
                {
                    name = 'nameBlock' ,
                    type = ui.TYPE.Flex,
                    props = {
                        arrange = ui.ALIGNMENT.Center,
                        align = ui.ALIGNMENT.Center,
                    },
                    content = ui.content {
                        textHeader('name', nameString, nameProps),
                    }
                },
            }
        }) 
    end

    innerContent:add(BASE.intervalV(4))

    local conditionLabel, condition, conditionMax, conditionText
    if types.Item.itemData(item).condition then
        condition = types.Item.itemData(item).condition
        if itemRecord.health then
            conditionLabel = constants.Strings.CONDITION
            conditionMax = util.round(itemRecord.health)
        elseif itemRecord.maxCondition then
            conditionLabel = constants.Strings.USES
            conditionMax = util.round(itemRecord.maxCondition)
        elseif itemRecord.duration and condition ~= -1 then
            conditionLabel = constants.Strings.DURATION
            conditionMax = util.round(itemRecord.duration)
            conditionText = helpers.createDurationString(condition)
        end
    end

    local conditionBar
    if condition and conditionLabel and conditionMax then
        local inner = ui.content {}
        if not configPlayer.tweaks.b_HideConditionChargeLabels then
            inner:add(textNormal(nil, conditionLabel .. ':'))
            inner:add(BASE.intervalH(5))
        end
        inner:add(Templates.progressBar {
            value = condition,
            maxValue = conditionMax,
            size = v2(204, Templates.LINE_HEIGHT),
            color = constants.Colors.BAR_HEALTH,
            text = conditionText,
            textColor = constants.Colors.DEFAULT,
        })
        conditionBar = {
            name = 'condition',
            type = ui.TYPE.Flex,
            props = {
                horizontal = true,
                arrange = ui.ALIGNMENT.Center,
            },
            content = inner,
        }
    end

    if types.Armor.objectIsInstance(item) then
        if configPlayer.tweaks.b_CondensedWeightValue then
            local skillString = core.getGMST('sSkill' .. I.Combat.getArmorSkill(item))
            innerContent:add(textNormal('type', constants.Strings.TYPE .. ' ' .. skillString))
        end
        innerContent:add(textNormal('armorRating', constants.Strings.ARMOR_RATING .. ': ' .. math.modf(I.Combat.getEffectiveArmorRating(item, omwself))))
    elseif types.Weapon.objectIsInstance(item) then
        local weaponInfo = helpers.getWeaponInfo(item)
        if weaponInfo then
            innerContent:add(textNormal('type', constants.Strings.TYPE .. ' ' .. core.getGMST('sSkill' .. weaponInfo.skill)))
            
            if weaponInfo.class == constants.WeaponClass.Melee then
                local handedString = weaponInfo.isTwoHanded and constants.Strings.TWO_HANDED or constants.Strings.ONE_HANDED
                innerContent.type.props.text = innerContent.type.props.text .. ', ' .. handedString
                innerContent:add(textNormal('chop', constants.Strings.CHOP .. ': ' .. itemRecord.chopMinDamage .. ' - ' .. itemRecord.chopMaxDamage))
                innerContent:add(textNormal('slash', constants.Strings.SLASH .. ': ' .. itemRecord.slashMinDamage .. ' - ' .. itemRecord.slashMaxDamage))
                innerContent:add(textNormal('thrust', constants.Strings.THRUST .. ': ' .. itemRecord.thrustMinDamage .. ' - ' .. itemRecord.thrustMaxDamage))
            else
                local attackMin, attackMax = itemRecord.chopMinDamage, itemRecord.chopMaxDamage
                if weaponInfo.class == constants.WeaponClass.Thrown then
                    attackMin = attackMin * 2
                    attackMax = attackMax * 2
                end
                innerContent:add(textNormal('attack', constants.Strings.ATTACK .. ': ' .. attackMin .. ' - ' .. attackMax)) 
            end

            if weaponInfo.class == constants.WeaponClass.Melee then
                innerContent:add(textNormal('range', constants.Strings.RANGE .. ': ' .. helpers.roundToPlaces(helpers.getWeaponRangeInFeet(item), 1) .. ' ' .. constants.Strings.FEET)) 
            end

            if weaponInfo.class ~= constants.WeaponClass.Ammo then
                innerContent:add(textNormal('speed', constants.Strings.SPEED .. ': ' .. util.round(itemRecord.speed * 100) .. '%')) 
            end
        end
    end

    if itemRecord.quality then
        innerContent:add(textNormal('quality', constants.Strings.QUALITY .. ': ' .. helpers.roundToPlaces(itemRecord.quality, 3)))
    end

    local showEnchantCapacity = false
    if itemRecord.enchantCapacity and itemRecord.enchantCapacity > 0 then
        -- Always show used/capacity for enchanted items. For unenchanted items,
        -- keep the existing tooltip setting.
        if itemRecord.enchant then
            showEnchantCapacity = true
        elseif configPlayer.tweaks.s_EnchantCapacityInTooltips == 'EnchantCapacityInTooltips_Always' then
            showEnchantCapacity = true
        elseif configPlayer.tweaks.s_EnchantCapacityInTooltips == 'EnchantCapacityInTooltips_UnenchantedOnly' then
            showEnchantCapacity = true
        end
    end
    if showEnchantCapacity then
        local usedCapacity = 0
        if itemRecord.enchant then
            local capacityEnchantment = core.magic.enchantments.records[itemRecord.enchant]
            usedCapacity = getEnchantmentUsedCost(capacityEnchantment)
        end
        local actualCapacity = itemRecord.enchantCapacity
        innerContent:add(textNormal(
            'enchantCapacity',
            l10n('UI_EnchantCapacity') .. ': ' .. formatTooltipNumber(usedCapacity) .. ' / ' .. formatTooltipNumber(actualCapacity),
            { textColor = getEnchantCapacityColor(usedCapacity, actualCapacity) }
        ))
    end

    if configPlayer.tweaks.b_SoulGemValueInTooltips and itemData.soul and itemData.soul ~= '' then
        local creatureRecord = types.Creature.records[itemData.soul]
        if creatureRecord and creatureRecord.soulValue then
            innerContent:add(textNormal('soul', l10n('UI_SoulValue') .. ': ' .. creatureRecord.soulValue))
        end
    end

    local showSoulCapacity = false
    if configPlayer.tweaks.s_SoulGemCapacityInTooltips == 'SoulGemCapacityInTooltips_Always' then
        showSoulCapacity = true
    elseif configPlayer.tweaks.s_SoulGemCapacityInTooltips == 'SoulGemCapacityInTooltips_EmptyOnly' and not itemData.soul then
        showSoulCapacity = true
    end
    if showSoulCapacity then
        local capacity = helpers.getSoulGemCapacity(item)
        if capacity then
            innerContent:add(textNormal('soulCapacity', l10n('UI_SoulCapacity') .. ': ' .. capacity))
        end
    end

    if itemRecord.weight > 0 then
        innerContent:add(textNormal('weight', constants.Strings.WEIGHT .. ': ' .. helpers.roundToPlaces(itemRecord.weight, 3)))
        if types.Armor.objectIsInstance(item) then
            local skill = I.Combat.getArmorSkill(item)
            local skillString
            if skill == 'lightarmor' then
                skillString = constants.Strings.LIGHT
            elseif skill == 'mediumarmor' then
                skillString = constants.Strings.MEDIUM
            elseif skill == 'heavyarmor' then
                skillString = constants.Strings.HEAVY
            else
                skillString = core.getGMST('sSkill' .. skill)
            end
            innerContent.weight.props.text = innerContent.weight.props.text .. ' (' .. skillString .. ')'
        end
    end

    local value = helpers.getItemValue(item)
    if value > 0 and itemRecord.id ~= 'gold_001' then
        innerContent:add(textNormal('value', constants.Strings.VALUE .. ': ' .. (value)))
    end

    if conditionBar then
        innerContent:add(BASE.intervalV(4))
        innerContent:add(conditionBar) 
    end

    local enchantment
    local castTypeString
    local cost = 0
    local doCharge
    local maxCharge
    if itemRecord.enchant then
        enchantment = core.magic.enchantments.records[itemRecord.enchant]
        if enchantment then
            if enchantment.type == core.magic.ENCHANTMENT_TYPE.CastOnStrike then
                castTypeString = constants.Strings.ITEM_CAST_WHEN_STRIKES
                cost = helpers.getModifiedSpellCost(itemRecord.enchant, true)
                doCharge = true
            elseif enchantment.type == core.magic.ENCHANTMENT_TYPE.CastOnUse then
                castTypeString = constants.Strings.ITEM_CAST_WHEN_USED
                cost = helpers.getModifiedSpellCost(itemRecord.enchant, true)
                doCharge = true
            elseif enchantment.type == core.magic.ENCHANTMENT_TYPE.CastOnce then
                castTypeString = constants.Strings.ITEM_CAST_ONCE
            elseif enchantment.type == core.magic.ENCHANTMENT_TYPE.ConstantEffect then
                castTypeString = constants.Strings.ITEM_CAST_CONSTANT
            end

            if enchantment.autocalcFlag then
                maxCharge = helpers.getEnchantMaxCharge(enchantment)
            else
                maxCharge = enchantment.charge
            end
        end
    end

    if castTypeString then
        if conditionBar then
            innerContent:add(BASE.intervalV(4))
        end
        innerContent:add(textNormal('castType', castTypeString))
    end

    if cost > 0 and configPlayer.tweaks[constants.OPT_KEYS.TooltipShowItemUseCost] then
        innerContent:add(textNormal('castCost', l10n('UI_Tooltip_Magic_Item_Use_Cost'):gsub('%%{cost}', helpers.addSeparators(cost))))
    end

    -- Handle effects for enchantments, potions, and ingredients.
    local effectsToShow = helpers.getTooltipMagicEffectEntries(item)
    
    -- Build effect layouts if we have any effects
    if #effectsToShow > 0 then
        local effectLayouts = {}
        for i, effectData in ipairs(effectsToShow) do
            local effect = effectData.effect
            local isVisible = effectData.visible ~= false
            local content = ui.content {}
            
            if isVisible then
                content:add(Templates.effectIcon(effect.id))
                content:add(BASE.intervalH(4))
                local effectText = effectData.text or '?'
                content:add(textNormal('effect_' .. i, effectText))
            else
                content:add(textNormal('effect_' .. i, '?'))
            end
            
            local effectLayout = {
                type = ui.TYPE.Flex,
                props = {
                    horizontal = true,
                    arrange = ui.ALIGNMENT.Center,
                },
                content = content,
            }
            
            if i ~= 1 then
                table.insert(effectLayouts, BASE.intervalV(8))
            end
            table.insert(effectLayouts, effectLayout)
        end
        
        innerContent:add(BASE.intervalV(4))
        innerContent:add({
            name = 'effects',
            type = ui.TYPE.Flex,
            props = {
                arrange = enchantment and ui.ALIGNMENT.Start or ui.ALIGNMENT.Center,
            },
            content = ui.content {
                table.unpack(effectLayouts)
            }
        })
    end

    if doCharge then
        innerContent:add(BASE.intervalV(8))

        local inner = ui.content {}
        if not configPlayer.tweaks.b_HideConditionChargeLabels then
            inner:add(textNormal(nil, constants.Strings.CHARGE))
            inner:add(BASE.intervalH(5))
        end
        inner:add(Templates.progressBar {
            value = itemData.enchantmentCharge or 0,
            maxValue = maxCharge,
            size = v2(204, Templates.LINE_HEIGHT),
            color = constants.Colors.BAR_MAGIC,
            textColor = constants.Colors.DEFAULT,
        })

        innerContent:add({
            name = 'charge',
            type = ui.TYPE.Flex,
            props = {
                horizontal = true,
                arrange = ui.ALIGNMENT.Center,
            },
            content = inner,
        })
    end

    local readablePreview = getReadableItemPreview(item, itemRecord)
    if readablePreview then
        innerContent:add(BASE.intervalV(10))
        innerContent:add({
            template = I.MWUI.templates.horizontalLine,
            props = {
                size = v2(READABLE_TEXT_WIDTH, 2),
                position = v2(0, 0),
            }
        })
        innerContent:add(BASE.intervalV(8))
        innerContent:add({
            name = 'readableTextPreview',
            template = BASE.textParagraph,
            props = {
                text = readablePreview,
                textColor = constants.Colors.DEFAULT,
                autoSize = true,
                size = v2(READABLE_TEXT_WIDTH, 0),
            }
        })
    end

    if configPlayer.modIntegration.b_TooltipsComplete and tooltipData[item.recordId] then
        innerContent:add(BASE.intervalV(12))
        innerContent:add({
            template = I.MWUI.templates.horizontalLine,
            props = {
                size = v2(280, 2),
                position = v2(20, 0),
            }
        })
        innerContent:add(BASE.intervalV(12))
        innerContent:add({
            name = 'lore',
            template = BASE.textParagraph,
            props = {
                text = tooltipData[item.recordId],
                textColor = constants.Colors.DISABLED,
                autoSize = true,
                size = v2(320, 0),
            }
        })
    end

    if configPlayer.tweaks.b_CondensedWeightValue then
        if innerContent:indexOf('weight') then
            innerContent.weight = nil
        end
        if innerContent:indexOf('value') then
            innerContent.value = nil
        end

        local flexContent = ui.content {}

        if value > 0 and itemRecord.id ~= 'gold_001' then
            flexContent:add({
                type = ui.TYPE.Image,
                props = {
                    size = v2(16, 16),
                    resource = BASE.createTexture('icons/gold.dds'),
                }
            })
            flexContent:add(textNormal(nil, ' ' .. helpers.addSeparators(util.round(value))))
        end

        if itemRecord.weight > 0 then
            if #flexContent > 0 then
                flexContent:add(BASE.intervalH(4))
            end
            flexContent:add({
                type = ui.TYPE.Image,
                props = {
                    size = v2(16, 16),
                    resource = BASE.createTexture('icons/weight.dds'),
                }
            })
            flexContent:add(textNormal(nil, ' ' .. helpers.roundToPlaces(itemRecord.weight, 2)))
        end

        if #flexContent > 0 then
            local flex = {
                name = 'weightValue',
                type = ui.TYPE.Flex,
                props = {
                    horizontal = true,
                    align = ui.ALIGNMENT.End,
                    arrange = ui.ALIGNMENT.Center,
                },
                external = {
                    stretch = 1,
                },
                content = flexContent
            }
            innerContent:add(BASE.intervalV(8))
            innerContent:add(flex)
        end
    end

    if #innerContent == 2 then
        innerContent[2] = nil -- remove extra interval if no details
    end

    local layout = Templates.tooltip(8, ui.content {
        {
            name = 'tooltip',
            type = ui.TYPE.Flex,
            props = {
                align = ui.ALIGNMENT.Center,
                arrange = ui.ALIGNMENT.Center,
            },
            content = innerContent,
        }
    }, item.id)

    if ctx and ctx.modifiers and ctx.modifiers.tooltip then
        for _, modifier in ipairs(ctx.modifiers.tooltip) do
            local ok, modifiedLayout = pcall(function()
                return modifier.modifier(item, layout)
            end)
            if ok and modifiedLayout then
                layout = modifiedLayout
            elseif not ok then
                print('[InventoryExtender] tooltip modifier failed for ' .. tostring(item.recordId or item.id or '<item>') .. ': ' .. tostring(modifiedLayout))
            end
        end
    end

    return layout
end

Templates.compareItemsTooltip = function(item, other, showIcon, ctx)
    local tip = Templates.itemTooltip(item, showIcon, ctx)

    if not other or #other <= 0 then return tip end

    local tips = {}

    for _, itm in ipairs(other) do
        if itm ~= item then
            local okOther, t = pcall(function()
                return Templates.itemTooltip(itm, showIcon, ctx)
            end)
            if okOther and t then
                local ok, inner = pcall(function() return t.content.padding.content.tooltip.content end)
                if ok and inner then
                    inner:add(BASE.intervalV(4))
                    inner:add(ui.create {
                        template = BASE.textNormal,
                        props = {
                            text = l10n('UI_Tooltip_Equipped'),
                            autoSize = true,
                        }
                    })
                end

                table.insert(tips, t)
            elseif not okOther then
                print('[InventoryExtender] equipped comparison tooltip failed: ' .. tostring(t))
            end
        end
    end

    if #tips <= 0 then return tip end

    table.insert(tips, tip)

    return {
        layer = 'Notification',
        name = item.id,
        template = {
            type = ui.TYPE.Container,
            content = ui.content {}
        },
        props = {
        },
        content = ui.content {
            {
                name = 'tooltip',
                type = ui.TYPE.Flex,
                props = {
                    horizontal = true,
                    autoSize = true,
                    align = ui.ALIGNMENT.Start,
                    arrange = ui.ALIGNMENT.Start,
                },
                content = ui.content(tips)
            }
        }
    }

end

Templates.sectionDivider = {
    props = {
        relativeSize = v2(1, 0),
        size = v2(0, Templates.SECTION_DIVIDER_HEIGHT),
    },
    content = ui.content {
        {
            template = I.MWUI.templates.horizontalLine,
            props = {
                anchor = v2(0, 0.5),
                relativePosition = v2(0, 0.5),
                size = v2(-4, 2),
                position = v2(2, 0),
            },
        }
    }
}

Templates.modal = function(content)
    return {
        layer = "Windows",
        props = {
            relativeSize = v2(1, 1), -- Block input for entire layer
        },
        content = ui.content {
            {
                template = BASE.boxSolidThick,
                props = {
                    anchor = v2(0.5, 0.5),
                    relativePosition = v2(0.5, 0.5),
                },
                content = ui.content(content)
            }
        }
    }
end

Templates.choiceModal = function(headerLayout, choices, ctx)
    -- example: choices = { { text = "Yes", onClick = fn }, { text = "No", onClick = fn } }
    local choiceButtons = {}
    for i, choice in ipairs(choices) do
        table.insert(choiceButtons, Templates.interactive({
            onClick = function()
                if ctx.modalElement then
                    auxUi.deepDestroy(ctx.modalElement)
                    ctx.modalElement = nil
                end
                choice.onClick()
            end,
        }, BASE.button(choice.text, choice.onClick), ctx))
        if i ~= #choices then
            table.insert(choiceButtons, BASE.intervalH(8))
        end
    end
    local content = {
        {
            template = BASE.padding(8),
            content = ui.content {
                {
                    name = 'modalContent',
                    type = ui.TYPE.Flex,
                    props = {
                        arrange = ui.ALIGNMENT.Center,
                    },
                    content = ui.content {
                        headerLayout or {},
                        BASE.intervalV(8),
                        {
                            type = ui.TYPE.Flex,
                            props = {
                                horizontal = true,
                                arrange = ui.ALIGNMENT.Center,
                            },
                            content = ui.content {
                                table.unpack(choiceButtons)
                            }
                        }
                    }
                }
            }
        }
    }
    return Templates.modal(content)
end

Templates.effectIcon = function(effectId)
    local effectRecord = core.magic.effects.records[effectId] or (I.MagicWindow and I.MagicWindow.Spells.getCustomEffect(effectId))
    local layout = {
        type = ui.TYPE.Image,
        props = {
            size = v2(16, 16),
            resource = BASE.createTexture(effectRecord.icon),
        },
    }
    return layout
end

return Templates