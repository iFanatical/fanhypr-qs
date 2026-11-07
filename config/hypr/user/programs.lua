return {
    terminal = "alacritty",
    file_manager = "fanfm",
    browser = "gtk-launch $(xdg-settings get default-web-browser)",
    browser_alt = "firefox",
    -- Tokyo Music TUI in its own terminal; the class lets the window rule
    -- (config/window_rules.lua) send it to the tokyo_music special workspace.
    tokyo_music = "alacritty --class tokyo-music -e tokyo-music-tui",
    media_play_pause = "playerctl play-pause",
    volume_down = "fanshell-qs-ipc audio volume-down",
    volume_up = "fanshell-qs-ipc audio volume-up",
    volume_mute = "fanshell-qs-ipc audio toggle-mute",
    microphone_mute = "fanshell-qs-ipc audio toggle-mic",
    monitor_brightness_up = "fanshell-qs-ipc brightness monitor-up",
    monitor_brightness_down = "fanshell-qs-ipc brightness monitor-down",
    brightness_up = "fanshell-qs-ipc brightness up",
    brightness_down = "fanshell-qs-ipc brightness down",
    -- Saved to screenshot.dir (shell.conf; ~/Pictures/screenshots) and copied.
    screenshot_region = "fanshell-qs-ipc screenshot region",
    screenshot_output = "fanshell-qs-ipc screenshot output",
    screenshot_screen = "fanshell-qs-ipc screenshot screen",
    shell = "fanshell-qs-shell",
}
