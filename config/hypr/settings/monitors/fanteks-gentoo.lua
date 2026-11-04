-- The same layout as outputs.conf's [DP-1+DP-2+DP-3], which the shell applies
-- when it starts: a reload puts these back.
hl.monitor({ output = "DP-1", mode = "1920x1080@100", position = "0x443", scale = "1", cm = "auto" })
hl.monitor({ output = "DP-2", mode = "1920x1080@100", position = "1920x443", scale = "1", cm = "auto" })
hl.monitor({ output = "DP-3", mode = "1920x1080@144", position = "3840x0", scale = "1", transform = 1, cm = "auto" })

-- fallback
hl.monitor({ output = "", mode = "preferred", position = "auto", scale = "auto" })
