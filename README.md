## fanhypr-qs

A hand-rolled C++/Qt status bar for **Hyprland**. No QML, no QuickShell
runtime, no scripting layer — one binary (`fanhypr-qs-shell`) draws the panel,
the popups, the SNI system tray, and the application launcher.

Ported from a dwm/X11 bar of the same lineage. Everything the X11 build did
with EWMH root-window properties, `_NET_WM_WINDOW_TYPE_DOCK` and raw X grabs is
done with Hyprland's IPC socket and `wlr-layer-shell` instead.

```sh
git clone https://github.com/ifanatical/fanhypr-qs.git
cd fanhypr-qs
make && sudo make install
```

### Requirements

| | |
|---|---|
| Hyprland | the compositor; the bar reads its IPC socket directly |
| Qt 6 base | `Qt6Widgets` `Qt6Gui` `Qt6Core` `Qt6DBus` `Qt6Network` |
| **qtwayland** | the Qt Wayland platform plugin — without it Qt has no Wayland backend |
| **layer-shell-qt** | `wlr-layer-shell` binding (`kde-plasma/layer-shell-qt`) |
| libpulse | native volume/mic control (works against pipewire-pulse) |
| **wl-clipboard** | clipboard history (`wl-paste --watch` / `wl-copy`) |
| **awww** (or swww) | wallpaper picker — needs `awww-daemon` running |
| A Nerd Font | `JetBrainsMono Nerd Font Propo` for the icons/glyphs |

On Gentoo, `qtwayland` needs the `wayland` USE flag enabled globally:

```sh
emerge dev-qt/qtbase dev-qt/qtwayland kde-plasma/layer-shell-qt \
       gui-apps/wl-clipboard
```

### Running it

Hyprland 0.56 reads **`hyprland.lua`** in preference to `hyprland.conf` when
both exist, and only one of them is live — editing the wrong one is silent.
Check which you are on with `hyprctl version` / the `Using config:` line at the
top of `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/hyprland.log`.

`hyprland.lua`:

```lua
hl.on("hyprland.start", function()
    hl.exec_cmd("fanhypr-qs-shell --no-duplicate")
end)

hl.bind("SUPER + D",         hl.dsp.exec_cmd("fanhypr-qs-shell ipc call launcher toggle"))
hl.bind("SUPER + SHIFT + D", hl.dsp.exec_cmd("fanhypr-qs-shell ipc call runner toggle"))
hl.bind("SUPER + N",         hl.dsp.exec_cmd("fanhypr-qs-shell ipc call notifications toggle-dnd"))
hl.bind("SUPER + V",         hl.dsp.exec_cmd("fanhypr-qs-shell ipc call vpn toggle"))
hl.bind("SUPER + W",         hl.dsp.exec_cmd("fanhypr-qs-shell ipc call wallpaper toggle"))
```

`hyprland.conf`:

```
exec-once = fanhypr-qs-shell --no-duplicate

bind = SUPER, D,       exec, fanhypr-qs-shell ipc call launcher toggle
bind = SUPER SHIFT, D, exec, fanhypr-qs-shell ipc call runner toggle
bind = SUPER, N,       exec, fanhypr-qs-shell ipc call notifications toggle-dnd
bind = SUPER, V,       exec, fanhypr-qs-shell ipc call vpn toggle
bind = SUPER, W,       exec, fanhypr-qs-shell ipc call wallpaper toggle
```

**You do not need to reserve space for the bar.** The panel is a layer surface
with an exclusive zone of `Theme::panelHeight`, so Hyprland tiles everything
beneath it automatically — there is no `monitor` reserved-area line to keep in
sync, which is the one config chore the dwm build could never avoid.

### Workspaces

Each output shows a **fixed** block of workspace buttons, present whether or
not the workspace exists yet, so the bar never reflows as workspaces come and
go. The assignment is read from Hyprland's own workspace rules:

```
workspace = 1, monitor:DP-1
workspace = 2, monitor:DP-1
...
workspace = 6, monitor:HDMI-A-1
```

(or `hl.workspace_rule({ workspace = 1, monitor = "DP-2" })` in Lua).

With no such rules, each output falls back to the next contiguous block of
`Theme::workspacesPerMonitor` (default 5) — so two monitors read 1–5 and 6–10.

The **tray** sits on whichever output owns the lowest-numbered workspace, so it
stays put. Hyprland has no primary-monitor setting, so this is derived rather
than configured — nothing to keep in sync. The **launcher** instead opens on
whichever output has focus, so it appears where you are working.

Colours follow the dwm tag convention: **active** purple/pink, **has windows** white,
**empty** gray.

### Notifications

The shell is its own freedesktop notification daemon; do not start dunst or
another daemon alongside it. It owns `org.freedesktop.Notifications`, renders a
scrollable stack below the upper-right of the tray output, and keeps the last
20 notifications in memory for the life of the bar.

Each toast shows its arrival time at the right. Notification image data (for
example, a message avatar supplied by Discord) appears beneath the timestamp,
separate from the sending application's icon on the left.

Left-click the bell on any output to open history on that output. Right-click
toggles do-not-disturb globally. Critical notifications bypass do-not-disturb
and remain visible until explicitly dismissed; hovering a toast pauses its
timeout. Clicking a toast invokes the application's default action when one was
provided. Otherwise the shell focuses a Hyprland window only when the DBus
sender PID identifies exactly one client—ambiguous matches are never guessed.

The former dunst rules are built in: normal notifications use the shell's
purple/pink accent, critical notifications are red, and special
colors for notifications whose application name is `volume`, `brightness`,
`network`, or `screenshot`, plus warning summaries. Volume and brightness are
still ordinary notifications for now; they are natural candidates for a
dedicated OSD later.

Toast backgrounds are translucent; their alpha is
`Theme::notificationOpacity` in `shell/theme.h`. Background blur is a
compositor effect—a Wayland client cannot sample surfaces behind itself—so
enable it for the toast's dedicated layer namespace in `hyprland.lua`:

```lua
hl.layer_rule({
    match = { namespace = "fanhypr-qs-notifications" },
    blur = true,
    ignore_alpha = 0.1,
})
```

The equivalent legacy `hyprland.conf` rules are:

```ini
layerrule = blur on, match:namespace fanhypr-qs-notifications
layerrule = ignore_alpha 0.1, match:namespace fanhypr-qs-notifications
```

### Power draw (desktops)

Where there is no battery, the battery pill is replaced by a plug pill showing
current power draw, with a breakdown in its dropdown. Nothing to configure —
the bar shows charge on a machine with a battery and watts on one without.

**GPU wattage works out of the box**; amdgpu's hwmon sensors are world-readable.

**CPU wattage needs one-time setup.** The kernel's RAPL energy counter
(`/sys/class/powercap/intel-rapl:*/energy_uj`) is mode `0400` on most
distributions — deliberately, because it samples finely enough that a local
unprivileged process can infer another process's secrets from its power
signature (PLATYPUS, CVE-2020-8694). To let the bar read it:

```sh
sudo tools/install-rapl-udev.sh          # grants the "power" group; or:
sudo tools/install-rapl-udev.sh mygroup
```

That installs a udev rule so the permission survives reboots, and tells you
whether you are already in the group. **Understand the trade before running
it**: it re-opens that side channel to everyone in the group. Reasonable on a
single-user desktop, not on a shared machine. Skip it and you simply get GPU
watts only, with the dropdown saying why. Undo with:

```sh
sudo rm /etc/udev/rules.d/99-fanhypr-qs-rapl.rules   # takes effect next boot
```

**With a UPS, it shows real wall power instead.** Point the bar at an SNMP
management card and the pill reports measured output watts for everything on
the UPS, with load against the unit's rating, runtime remaining and battery
state in the dropdown. The component figures stay visible underneath, so you
can see how much of the wall total is this machine.

```lua
hl.env("FANHYPR_QS_UPS_HOST", "10.1.0.10")   -- SNMP card address
hl.env("FANHYPR_QS_UPS_COMMUNITY", "public") -- optional, defaults to public
```

Needs `net-analyzer/net-snmp` and SNMPv1 enabled on the card. Read via
CyberPower's enterprise MIB (`.1.3.6.1.4.1.3808`), because an RMCARD205
populates the standard RFC1628 `upsOutputPower` with 0 and offers only percent
load there. Tested against an OR1500LCDRM1U.

The pill turns amber past 70% of the UPS rating and red past 85%, so
approaching the limit is visible before you hit it, and switches to a warning
icon on battery.

> The card ignores any manager not in its NMS access list, which is
> indistinguishable from a timeout. The address it sees is the one
> `ip route get <card>` picks as the source — not necessarily the address you
> think of as this machine.

Without a UPS the figure covers the CPU package and GPU board only, and
excludes RAM, drives, fans and PSU losses.

### Configuration

Everything visual lives in `shell/theme.h` — colours, sizes, the font, and:

- `panelHeight` — bar height *and* the reserved strip, in one place
- `workspacesPerMonitor` — fallback block size (see above)

Runtime overrides. These are read from the **bar's own environment**, which it
inherits from the compositor — exporting them in a shell rc has no effect on a
process started at login. Put them next to your other `hl.env` lines in
`hyprland.lua` (or `env = NAME,value` in `hyprland.conf`):

```lua
hl.env("FANHYPR_QS_WALLPAPER_TRANSITION", "wipe")
```

They are re-read on each use, but the environment of an already-running process
is fixed, so a change needs the bar restarted (relogin, or `pkill -x
fanhypr-qs-shel` and start it again).


| variable | effect |
|---|---|
| `FANHYPR_QS_VPN_TUNNEL` | WireGuard tunnel name (default `tun1`) |
| `FANHYPR_QS_BATTERY_PATH` | battery `power_supply` dir (multi-battery machines) |
| `FANHYPR_QS_WEATHER_UNIT` | `C` for Celsius, default Fahrenheit |
| `FANHYPR_QS_NET_BRIDGE` | path to `net-bridge.sh` for the non-NetworkManager backend |
| `FANHYPR_QS_WALLPAPER_DIR` | wallpaper directory (default `~/Pictures/wallpapers`) |
| `FANHYPR_QS_WALLPAPER_TRANSITION` | awww transition (default `fade`; `wipe`, `grow`, `outer`, `random`, …) |
| `FANHYPR_QS_POWER_WINDOW` | RAPL sample window in seconds (default `0.5`) |
| `FANHYPR_QS_POWERCAP_DIR` | powercap root (default `/sys/class/powercap`) |
| `FANHYPR_QS_UPS_HOST` | SNMP card address; unset disables UPS polling |
| `FANHYPR_QS_UPS_COMMUNITY` | SNMPv1 community (default `public`) |

See [BAR.md](BAR.md) for the architecture, the widget-by-widget dependency
list, and what changed in the port from X11.
