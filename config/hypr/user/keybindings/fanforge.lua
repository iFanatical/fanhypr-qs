local mod = "SUPER"

hl.bind(mod .. " + D", function()
    hl.dispatch(hl.dsp.exec_cmd("pgrep -x Discord >/dev/null || discord"))
    hl.dispatch(hl.dsp.workspace.toggle_special("discord"))
end)
hl.bind(mod .. " + SHIFT + D", hl.dsp.window.move({ workspace = "special:discord", follow = false }))

hl.bind(mod .. " + X", function()
    hl.dispatch(hl.dsp.exec_cmd("pgrep -x TeamSpeak >/dev/null || TeamSpeak"))
    hl.dispatch(hl.dsp.workspace.toggle_special("teamspeak"))
end)
hl.bind(mod .. " + SHIFT + X", hl.dsp.window.move({ workspace = "special:teamspeak", follow = false }))

-- Roleplay journal (~/.local/bin/rp-journal): nvim on the file named in
-- ~/.config/rp-journal/current, shown over everything, fullscreen games included.
hl.bind(mod .. " + N", function()
    -- Checks for the window, not the process: pgrep -f would match this
    -- command's own sh -c line and never launch it.
    hl.dispatch(hl.dsp.exec_cmd([[hyprctl clients -j | jq -e 'any(.[]; .initialClass == "rp-journal")' >/dev/null || alacritty --class rp-journal -e rp-journal]]))
    hl.dispatch(hl.dsp.workspace.toggle_special("journal"))
end)

-- Toggle the focused workspace between master and scrolling.
require("config/scrolling").bind(mod .. " + S")
