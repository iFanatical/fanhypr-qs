hl.on("hyprland.start", function()
    -- The bar and desktop shell (fanshell-qs). It draws the wallpaper, and
    -- does vibrance and the night light itself (Control Panel), so neither
    -- awww, hyprvibrance nor hyprsunset runs beside it.
    hl.exec_cmd("fanshell-qs-shell --no-duplicate")
    hl.exec_cmd("xsettingsd")
    hl.exec_cmd("/usr/libexec/polkit-mate-authentication-agent-1")
    hl.exec_cmd("gsr-ui launch-daemon")
    -- Sunshine, so Moonlight on the laptops can stream this desktop. Started
    -- here because graphical-session.target never comes up under Hyprland,
    -- and with linger on, a default.target unit would start at boot, before
    -- there is a desktop. The check skips machines without Sunshine.
    hl.exec_cmd("systemctl --user cat app-dev.lizardbyte.app.Sunshine.service >/dev/null 2>&1 && systemctl --user start app-dev.lizardbyte.app.Sunshine.service")
end)
