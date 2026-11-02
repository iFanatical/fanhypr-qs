-- Toggle the focused workspace between the default layout (general.layout,
-- master) and scrolling, as a workspace rule turned on and off: Hyprland
-- applies both at once to a workspace that exists. Each workspace keeps its
-- own, until Hyprland reloads its config, which brings them all back to the
-- default.
--
-- A host opts in from user/keybindings/<hostname>.lua:
--
--     require("config/scrolling").bind("SUPER + S")

local M = {
    -- Workspace name -> its scrolling rule, made the first time it is
    -- toggled and switched on and off after.
    rules = {},
}

-- Whether the focused workspace is scrolling.
function M.active()
    local ws = hl.get_active_workspace()
    return ws ~= nil and ws.tiled_layout == "scrolling"
end

function M.toggle()
    local ws = hl.get_active_workspace()
    if not ws or ws.special then
        return
    end
    -- A named workspace is "name:<name>" to a rule.
    local selector = tostring(ws.id) == ws.name and ws.name or "name:" .. ws.name
    local rule = M.rules[selector]
    if ws.tiled_layout == "scrolling" then
        if rule then
            rule:set_enabled(false)
        end
    elseif rule then
        rule:set_enabled(true)
    else
        M.rules[selector] = hl.workspace_rule({ workspace = selector, layout = "scrolling" })
    end
end

function M.bind(keys)
    hl.bind(keys, M.toggle)
end

return M
