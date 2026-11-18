hl.on("hyprland.start", function()
    -- Sunshine, so Moonlight on the laptops can stream this desktop. Started
    -- here because graphical-session.target never comes up under Hyprland,
    -- and with linger on, a default.target unit would start at boot, before
    -- there is a desktop.
    hl.exec_cmd("systemctl --user start app-dev.lizardbyte.app.Sunshine.service")
end)
