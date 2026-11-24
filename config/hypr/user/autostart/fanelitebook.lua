hl.on("hyprland.start", function()
    -- Lan Mouse, sharing keyboard and mouse with fanforge (right edge).
    -- Only the daemon: the window (lan-mouse) attaches to it when opened.
    -- Skipped if one is already running. lan-mouse is 0.11.0 in
    -- /usr/local/bin until Terra packages 0.11 (its rpm is 0.10.0).
    hl.exec_cmd("pgrep -x lan-mouse >/dev/null || lan-mouse daemon")
end)
