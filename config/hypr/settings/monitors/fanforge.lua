-- The desktop (fanteks-gentoo before it moved to Fedora): two BenQs at
-- 100 Hz, side by side, and the Acer at 144 Hz turned upright to their
-- right. By description, like config/workspaces/fanforge.lua, as the
-- connector names change between kernels; the BenQs are the same model, so
-- only the serial at the end of theirs tells them apart.
hl.monitor({ output = "desc:BNQ BenQ GW2790T 6BR0209401Q", mode = "1920x1080@100", position = "0x443", scale = "1", cm = "auto" })
hl.monitor({ output = "desc:BNQ BenQ GW2790T 6BR0197001Q", mode = "1920x1080@100", position = "1920x443", scale = "1", cm = "auto" })
hl.monitor({ output = "desc:Acer Technologies Acer XFA240", mode = "1920x1080@144", position = "3840x0", scale = "1", transform = 1, cm = "auto" })

-- fallback
hl.monitor({ output = "", mode = "preferred", position = "auto", scale = "auto" })
