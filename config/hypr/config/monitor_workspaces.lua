-- Per-monitor workspaces, as in fanqtile-qs and dwm: every monitor has its
-- own five, numbered 1-5 on it, and Mod+1-5 act on the focused monitor's.
--
-- Hyprland's workspaces are global, as qtile's groups are, so this does
-- what fanqtile-qs does with them: the first monitor in a host's list owns
-- workspaces 1-5, the second 6-10, and so on, pinned there by workspace
-- rules; a key works out which set the focused monitor has. A monitor not
-- in the list takes the first free set, in Hyprland's monitor order, and
-- the shell's bar numbers each monitor's set 1-5 by the same rule.
--
-- A host opts in from config/workspaces/<hostname>.lua:
--
--     require("config/monitor_workspaces").setup({ "desc:BNQ BenQ GW2790T", ... })
--
-- Without it (durandal) user/keybindings/default.lua keeps the global
-- Mod+1-5 and Alt+1-5.

local M = {
    per_monitor = 5,
    enabled = false,
    monitors = {},
}

-- Whether `selector`, as a workspace rule names a monitor, is `mon`: its
-- connector name, or "desc:" and the start of its description.
local function matches(selector, mon)
    if selector == mon.name then
        return true
    end
    local desc = selector:match("^desc:(.*)$")
    return desc ~= nil and mon.description:sub(1, #desc) == desc
end

-- The set (0, 1, ...) each connected monitor has, by name.
function M.sets()
    local monitors = hl.get_monitors()
    table.sort(monitors, function(a, b) return a.id < b.id end)
    local sets, taken = {}, {}
    for _, mon in ipairs(monitors) do
        for i, selector in ipairs(M.monitors) do
            if matches(selector, mon) then
                sets[mon.name] = i - 1
                taken[i - 1] = true
                break
            end
        end
    end
    local free = 0
    for _, mon in ipairs(monitors) do
        if sets[mon.name] == nil then
            while taken[free] do
                free = free + 1
            end
            sets[mon.name] = free
            taken[free] = true
        end
    end
    return sets
end

-- Workspace `n` (1-5) of the focused monitor.
function M.workspace(n)
    local mon = hl.get_active_monitor()
    local set = mon and M.sets()[mon.name] or 0
    return set * M.per_monitor + n
end

function M.setup(monitors)
    M.enabled = true
    M.monitors = monitors
    for i, selector in ipairs(monitors) do
        for n = 1, M.per_monitor do
            hl.workspace_rule({
                workspace = (i - 1) * M.per_monitor + n,
                monitor = selector,
                default = n == 1,
            })
        end
    end
end

-- Mod+1-5 go to the focused monitor's workspace, Mod+Shift+1-5 send the
-- focused window there; Mod+scroll stays on the monitor.
function M.bind(mod)
    for n = 1, M.per_monitor do
        hl.bind(mod .. " + " .. n, function()
            hl.dispatch(hl.dsp.focus({ workspace = M.workspace(n) }))
        end)
        hl.bind(mod .. " + SHIFT + " .. n, function()
            hl.dispatch(hl.dsp.window.move({ workspace = M.workspace(n), follow = false }))
        end)
    end
    hl.bind(mod .. " + mouse_down", hl.dsp.focus({ workspace = "m+1" }))
    hl.bind(mod .. " + mouse_up", hl.dsp.focus({ workspace = "m-1" }))
end

return M
