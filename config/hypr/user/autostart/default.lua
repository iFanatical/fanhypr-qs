hl.on("hyprland.start", function()
    -- The GTK theme, icons and font, the same as fanshell-qs-session sets
    -- under X. GTK on Wayland, and Chromium-based browsers such as Brave,
    -- read these rather than gtk-3.0/settings.ini.
    hl.exec_cmd("gsettings set org.gnome.desktop.interface font-name 'JetBrainsMonoNL Nerd Font Propo'")
    hl.exec_cmd("gsettings set org.gnome.desktop.interface color-scheme 'prefer-dark'")
    hl.exec_cmd("gsettings set org.gnome.desktop.interface icon-theme Tela-circle-black-dark")
    hl.exec_cmd("gsettings set org.gnome.desktop.interface gtk-theme Tokyo-Night")
    hl.exec_cmd("gsettings set org.gnome.desktop.interface cursor-size 24")
    hl.exec_cmd("gsettings set org.gtk.Settings.FileChooser sort-directories-first true")
    hl.exec_cmd("gsettings set org.gtk.gtk4.Settings.FileChooser sort-directories-first true")
    -- The bar and desktop shell (fanshell-qs). It draws the wallpaper, and
    -- does vibrance and the night light itself (Control Panel), so neither
    -- awww, hyprvibrance nor hyprsunset runs beside it.
    hl.exec_cmd("fanshell-qs-shell --no-duplicate")
    hl.exec_cmd("xsettingsd")
    hl.exec_cmd("/usr/libexec/polkit-mate-authentication-agent-1")
    hl.exec_cmd("gsr-ui launch-daemon")
end)
