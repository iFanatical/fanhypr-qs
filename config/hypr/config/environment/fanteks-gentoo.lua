-- vpn.tunnels=1 in ~/.config/fanshell-qs/shell.conf on this machine.

-- The session bus is OpenRC's user dbus service, on the standard socket,
-- but nothing in the login exports its address. GLib and Qt find the
-- socket anyway; Chromium (Brave) does not, and without a bus it shows
-- web notifications as windows of its own (which tile) instead of sending
-- them to the shell.
local runtime = os.getenv("XDG_RUNTIME_DIR")
if not os.getenv("DBUS_SESSION_BUS_ADDRESS") and runtime then
    hl.env("DBUS_SESSION_BUS_ADDRESS", "unix:path=" .. runtime .. "/bus")
end
