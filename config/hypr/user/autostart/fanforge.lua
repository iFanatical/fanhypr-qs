hl.on("hyprland.start", function()
    -- Sunshine, so Moonlight on the laptops can stream this desktop. Started
    -- here because graphical-session.target never comes up under Hyprland,
    -- and with linger on, a default.target unit would start at boot, before
    -- there is a desktop.
    hl.exec_cmd("systemctl --user start app-dev.lizardbyte.app.Sunshine.service")

    -- Lan Mouse, sharing this keyboard and mouse with fanelitebook (left
    -- edge). Only the daemon: the window (lan-mouse) attaches to it when
    -- opened. Skipped if one is already running.
    hl.exec_cmd("pgrep -x lan-mouse >/dev/null || lan-mouse daemon")
end)
