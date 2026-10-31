hl.on("hyprland.start", function()
    -- The bar and desktop shell (fanshell-qs). It draws the wallpaper, and
    -- does vibrance and the night light itself (Control Panel), so neither
    -- awww, hyprvibrance nor hyprsunset runs beside it.
    hl.exec_cmd("fanshell-qs-shell --no-duplicate")
    hl.exec_cmd("xsettingsd")
    hl.exec_cmd("/usr/libexec/polkit-mate-authentication-agent-1")
    hl.exec_cmd("gsr-ui launch-daemon")
end)
