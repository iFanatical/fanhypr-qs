-- On a vertical monitor (taller than it is wide, as turned by its transform),
-- the master layout stacks windows top to bottom instead of side by side:
--
--     1 | 2        1
--                  -
--                  2
--
-- A workspace rule per vertical monitor (m[<name>]) sets the master layout's
-- orientation to "top" for every workspace on it, and is switched on and off
-- as monitors are added, removed or turned.
--
-- A host opts in from config/workspaces/<hostname>.lua:
--
--     require("config/vertical_monitors").setup()

local M = {
    -- Monitor name -> its rule, made the first time it is vertical.
    rules = {},
}

-- Whether `mon` is taller than it is wide once turned: width and height are
-- its mode's, and transforms 1, 3, 5 and 7 turn it by 90 or 270 degrees.
local function vertical(mon)
    local w, h = mon.width, mon.height
    if mon.transform % 2 == 1 then
        w, h = h, w
    end
    return h > w
end

function M.refresh()
    local seen = {}
    for _, mon in ipairs(hl.get_monitors()) do
        seen[mon.name] = true
        local rule = M.rules[mon.name]
        if vertical(mon) then
            if rule then
                rule:set_enabled(true)
            else
                M.rules[mon.name] = hl.workspace_rule({
                    workspace = "m[" .. mon.name .. "]",
                    layout_opts = { orientation = "top" },
                })
            end
        elseif rule then
            rule:set_enabled(false)
        end
    end
    -- A monitor that went away takes its rule off: another may come back
    -- under its name the other way round.
    for name, rule in pairs(M.rules) do
        if not seen[name] then
            rule:set_enabled(false)
        end
    end
end

function M.setup()
    M.refresh()
    for _, event in ipairs({ "monitor.added", "monitor.removed", "monitor.layout_changed" }) do
        hl.on(event, M.refresh)
    end
end

return M
