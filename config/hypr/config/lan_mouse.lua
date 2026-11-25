-- Pause and resume Lan Mouse (keyboard and mouse sharing between machines),
-- for a laptop taken away from the desk. Pausing stops Lan Mouse outright:
-- deactivating its client ("lan-mouse cli deactivate") only stops this
-- machine sending, and it would still take input from the other one.
-- Resuming starts the daemon again, as user/autostart/<hostname>.lua does at
-- login; the pause lasts until then.
--
-- A host opts in from user/keybindings/<hostname>.lua:
--
--     require("config/lan_mouse").bind("SUPER + SHIFT + M")

local M = {}

local notify = "notify-send -a 'Lan Mouse' -i de.feschber.LanMouse"

M.command = table.concat({
    "if pgrep -x lan-mouse >/dev/null; then",
    "  pkill -x lan-mouse && " .. notify .. " 'Lan Mouse paused' 'Keyboard and mouse sharing is off.';",
    "else",
    "  setsid -f lan-mouse daemon >/dev/null 2>&1 && " .. notify .. " 'Lan Mouse resumed' 'Keyboard and mouse sharing is on.';",
    "fi",
}, " ")

function M.toggle()
    hl.dispatch(hl.dsp.exec_cmd(M.command))
end

function M.bind(keys)
    hl.bind(keys, M.toggle)
end

return M
