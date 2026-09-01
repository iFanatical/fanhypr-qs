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
| brightnessctl | internal-display and supported keyboard-backlight control |
| ddcutil | external-monitor brightness control over DDC/CI |
| pw-play or paplay | optional volume-change feedback sound playback |
| **wl-clipboard** | clipboard history (`wl-paste --watch` / `wl-copy`) |
| **awww** (or swww) | wallpaper picker — needs `awww-daemon` running |
| A Nerd Font | `JetBrainsMono Nerd Font Propo` for the icons/glyphs |
| Noto Color Emoji | preferred color rendering in the emoji picker |
| power-profiles-daemon | optional provider for the System power-profile selector |
| X11, XComposite, XDamage, XFixes development files | optional legacy Wine/XWayland tray support |

On Gentoo, `qtwayland` needs the `wayland` USE flag enabled globally:

```sh
emerge dev-qt/qtbase dev-qt/qtwayland kde-plasma/layer-shell-qt \
       gui-apps/wl-clipboard sys-power/power-profiles-daemon
```

To compile optional legacy XEmbed tray support on Gentoo, also install:

```sh
emerge x11-libs/libX11 x11-libs/libXcomposite x11-libs/libXdamage \
       x11-libs/libXfixes
```

On Arch Linux, the corresponding optional build dependencies are:

```sh
sudo pacman -S libx11 libxcomposite libxdamage libxfixes
```

On Arch Linux, install the optional profile provider with
`sudo pacman -S power-profiles-daemon`. The selector talks to the standard
`net.hadess.PowerProfiles` system D-Bus API, so compatible alternate providers
work as well. It displays only profiles advertised by the current machine and
hides completely when no provider is available.

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
hl.bind("SUPER + CTRL + Space", hl.dsp.exec_cmd("fanhypr-qs-shell ipc call emoji toggle"))
hl.bind("SUPER + N",         hl.dsp.exec_cmd("fanhypr-qs-shell ipc call notifications toggle-dnd"))
hl.bind("SUPER + V",         hl.dsp.exec_cmd("fanhypr-qs-shell ipc call vpn toggle"))
hl.bind("SUPER + W",         hl.dsp.exec_cmd("fanhypr-qs-shell ipc call wallpaper toggle"))
```

`hyprland.conf`:

```
exec-once = fanhypr-qs-shell --no-duplicate

bind = SUPER, D,       exec, fanhypr-qs-shell ipc call launcher toggle
bind = SUPER SHIFT, D, exec, fanhypr-qs-shell ipc call runner toggle
bind = SUPER CTRL, Space, exec, fanhypr-qs-shell ipc call emoji toggle
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

The normal tray is native StatusNotifierItem/D-Bus. When the optional X11,
XComposite, XDamage, and XFixes development packages were present at build
time, the same binary also provides an XEmbed tray manager whenever a reachable
XWayland `$DISPLAY` exists. Legacy Wine icons are captured offscreen and added
to the normal tray; left, middle, and right clicks are relayed to the X11 icon.
Without those libraries or without XWayland, this compatibility path is simply
not compiled or remains inactive. No Plasma component or helper daemon is
used. A Wine application that already created its floating fallback before the
shell started may need to be restarted once so it notices the new tray owner.

Colours follow the dwm tag convention: **active** purple/pink, **has windows** white,
**empty** gray.

### Emoji picker

The launcher has a searchable emoji mode:

```sh
fanhypr-qs-shell ipc call emoji toggle
```

Searches match Unicode names, groups, and subgroups; multiple words can be
entered in any matching combination. Arrow keys or `Ctrl-N/P/F/B` move through
the two-column results, Page Up/Page Down move by one visible page, and Enter
or a click copies the selected sequence with `wl-copy` before closing the
picker. The complete Unicode Emoji 17.0
fully-qualified set is compiled into the binary, so lookup needs no runtime
data package or network connection. `Noto Color Emoji` is preferred for color
glyphs, with Qt's normal font fallback when it is unavailable.

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
timeout. The history header has a **Clear all** button that dismisses visible
toasts and empties the in-memory history. Clicking a toast invokes the
application's default action when one was provided. Otherwise the shell focuses
a Hyprland window only when the DBus sender PID identifies exactly one
client—ambiguous matches are never guessed.

The former dunst rules are built in: normal notifications use the shell's
purple/pink accent, critical notifications are red, and special colors remain
for `network`, `screenshot`, and warning summaries. Successful low/normal
`volume` and `brightness` notifications are instead consumed by a temporary
bottom-center hardware OSD. It distinguishes speaker volume/mute, microphone
mute/unmute, laptop or DDC monitor brightness, and keyboard backlight (when a
sender uses `keyboard-brightness`, `keyboard_backlight`, or a `brightness`
notification whose summary contains “Keyboard”). Send an `int:value` hint or
a body containing a percentage. Critical hardware failures remain regular,
sticky notifications rather than disappearing into the OSD.

The OSD is pinned to the tray output (the output derived by
`HyprState::primaryMonitor()`) and uses its own `fanhypr-qs-osd` layer
namespace. For matching compositor blur, add the same rules used for
notifications:

```lua
hl.layer_rule({
    match = { namespace = "fanhypr-qs-osd" },
    blur = true,
    ignore_alpha = 0.1,
})
```

Hardware keys can call the shell directly; the old volume and brightness
notification scripts are no longer required:

```lua
local shell = "fanhypr-qs-shell ipc call "
hl.bind("XF86AudioRaiseVolume",  hl.dsp.exec_cmd(shell .. "audio volume-up"))
hl.bind("XF86AudioLowerVolume",  hl.dsp.exec_cmd(shell .. "audio volume-down"))
hl.bind("XF86AudioMute",         hl.dsp.exec_cmd(shell .. "audio toggle-mute"))
hl.bind("XF86AudioMicMute",      hl.dsp.exec_cmd(shell .. "audio toggle-mic"))
hl.bind("XF86AudioPlay",         hl.dsp.exec_cmd(shell .. "media play-pause"))
hl.bind("XF86AudioPrev",         hl.dsp.exec_cmd(shell .. "media previous"))
hl.bind("XF86AudioNext",         hl.dsp.exec_cmd(shell .. "media next"))
hl.bind("XF86MonBrightnessUp",   hl.dsp.exec_cmd(shell .. "brightness up"))
hl.bind("XF86MonBrightnessDown", hl.dsp.exec_cmd(shell .. "brightness down"))
hl.bind("SUPER + F1", hl.dsp.exec_cmd(shell .. "brightness monitor-up"))
hl.bind("SUPER + F2", hl.dsp.exec_cmd(shell .. "brightness monitor-down"))
```

Media keys use the standard MPRIS session D-Bus API, with no player-specific
helper or continuous polling. Previous/next target only a player whose MPRIS
state is actively `Playing`, so an unrelated paused browser tab cannot capture
the command. Play/pause prefers a playing player but retains a paused fallback
so playback can be resumed. It shows the current title and artist;
previous/next wait for the player to publish new metadata, then show only the
newly selected title and artist. Navigation metadata is briefly stabilized
before display because Chromium can expose a paused tab while its playing tab
changes sources. Titles are elided to the OSD width.

Volume and brightness use 5% steps. Display brightness follows a
`... 10, 5, 1` floor when decreasing and `1, 5, 10 ...` when increasing.
Every detected DDC display is updated together, asynchronously, so a slow I²C
operation cannot freeze the panel. `brightness keyboard-up` and
`brightness keyboard-down` are registered only when a keyboard-backlight LED
is detected.

Volume adjustment retains the freedesktop `audio-volume-change` feedback
sound. The shell checks `$XDG_CONFIG_HOME/hypr/sounds/audio-volume-change.*`
first, then the standard freedesktop sound theme in the XDG data directories,
and plays it through `pw-play` with a `paplay` fallback. This keeps the path
portable; no sound asset or home directory is compiled into the binary.

VPN toggles use a distinct shield confirmation in the same temporary
bottom-center surface: connected shows the assigned tunnel address when one
exists, while disconnected shows the tunnel interface. The confirmation is
shown only after `fanhypr-qs-vpn` verifies the requested WireGuard state. A
failed toggle becomes a sticky critical notification instead. Clicking the
Set `FANHYPR_TUN_COUNT` to expose `tun1` through `tunN` (default one). With
one tunnel, the System menu's VPN tile is a direct connect/disconnect button;
with multiple tunnels, it opens an exclusive selector. Selecting the connected
tunnel disconnects it; selecting another tunnel switches to it. Each tunnel
also becomes an IPC function (for example, `vpn tun2`); `vpn toggle` retains
its existing behavior for keybind compatibility.

Toast backgrounds are translucent; their alpha is
`Theme::notificationOpacity` in `shell/theme.h`. The toast box shadow mirrors
a Hyprland range-4, power-3 shadow but uses the much softer
`Theme::notificationShadowAlpha`; Hyprland does not apply window-decoration
shadows to layer surfaces. Background blur is a compositor effect—a Wayland
client cannot sample surfaces behind itself—so enable it for the toast's
dedicated layer namespace in `hyprland.lua`:

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
is fixed, so a change needs the bar restarted. Prefer `fanhypr-qs-restart`,
which asks the running shell to exit through IPC and waits for its helper
cleanup before starting the replacement.


| variable | effect |
|---|---|
| `FANHYPR_TUN_COUNT` | number of sequential WireGuard tunnels (`tun1`…`tunN`; default `1`, maximum `32`) |
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
