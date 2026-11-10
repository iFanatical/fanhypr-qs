-- 1-5 on the first BenQ (DP-2), 6-10 on the Acer (DP-3), 11-15 on the second
-- BenQ (DP-1), each numbered 1-5 on its monitor
-- (config/monitor_workspaces.lua). By description, which stays put when the
-- connector names change, as DP-1 became DP-3. The BenQs are the same model,
-- so only the serial at the end of theirs tells them apart.
require("config/monitor_workspaces").setup({
    "desc:BNQ BenQ GW2790T 6BR0197001Q",
    "desc:Acer Technologies Acer XFA240",
    "desc:BNQ BenQ GW2790T 6BR0209401Q",
})
