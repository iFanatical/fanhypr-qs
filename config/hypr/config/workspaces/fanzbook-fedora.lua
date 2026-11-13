-- 1-5 on the laptop's panel; a monitor plugged in takes 6-10, each
-- numbered 1-5 on its monitor (config/monitor_workspaces.lua).
require("config/monitor_workspaces").setup({ "eDP-1" })

-- Windows stack top to bottom on a vertical monitor (config/vertical_monitors.lua).
require("config/vertical_monitors").setup()
