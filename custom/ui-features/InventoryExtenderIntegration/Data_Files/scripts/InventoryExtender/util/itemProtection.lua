local self = require('openmw.self')
local config = require('scripts.InventoryExtender.config.player')

local M = {}

function M.flags(ctx, item)
    return {
        favorite = ctx.favoriteItems[item.id] == true,
        equipped = self.type.hasEquipped(self, item),
    }
end

-- UI loss prevention only: does not block quest scripts, consumption or confiscation.
function M.reason(ctx, item, source, destination, dragFlags)
    if destination == self or (source or item.parentContainer) ~= self then return nil end
    local flags = M.flags(ctx, item)
    local settings = config.tweaks or {}
    if settings.b_ProtectFavorites ~= false and (flags.favorite or (dragFlags and dragFlags.favorite)) then
        return 'UI_Msg_FavoriteItem'
    end
    if settings.b_ProtectEquipped ~= false and (flags.equipped or (dragFlags and dragFlags.equipped)) then
        return 'UI_Msg_EquippedProtected'
    end
    return nil
end

return M
