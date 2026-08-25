# fanhypr-qs-shell — architecture

One process, one binary. `main.cpp` builds a `Panel` per `QScreen`, a single
`AppLauncher`, and an `IpcServer`, and hands them all a shared `HyprState`.

```
hypripc.{h,cpp}    Hyprland IPC transport (both sockets)
hyprstate.{h,cpp}  live WM state: monitors, workspaces, titles, layout
wlutil.{h,cpp}     wlr-layer-shell surface setup + position arithmetic
panel.{h,cpp}      the bar itself, one layer surface per output
popup.{h,cpp}      dropdown popups + the click-outside scrim
launcher.{h,cpp}   fullscreen app/run launcher
wallpaper.{h,cpp}  fullscreen wallpaper picker, backed by awww
widgets/pills      shared leaf widgets (BarPill, ShellButton, ValueSlider, …)
                   plus the script-backed and submap pills
tray/traymenu      StatusNotifierItem host + com.canonical.dbusmenu client
network bluetooth volume battery system calendar clipboard   the widgets
ipc.{h,cpp}        control socket for `fanhypr-qs-shell ipc call …`
notification       freedesktop notification daemon, toast stack + history
theme.h            all colours, sizes, fonts, and the two layout knobs
```

## Talking to Hyprland

Hyprland publishes two unix sockets per instance under
`$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/`:

- **`.socket.sock`** — request/response. Write a command, read the reply, the
  compositor hangs up. This is the protocol `hyprctl` speaks, so `hypripc.cpp`
  speaks it directly and **never spawns a process**. A `j/` prefix asks for
  JSON.
- **`.socket2.sock`** — a push event stream, one `EVENT>>DATA` line per change.

`HyprState` keeps a persistent reader on the event stream and treats every
event as "something changed, re-query". Re-queries are coalesced through a
zero-delay timer, because one user action fans out into several events (moving
a window emits `movewindow` + `activewindow` + `workspace`) and each refresh
costs a few socket round trips. `configreloaded` additionally re-reads the
things that only change when `hyprland.conf` does — the layout and the
workspace rules.

A refresh reads `j/monitors`, `j/workspaces` and `j/clients`. The per-monitor
window title comes from `j/clients` rather than `j/activewindow`, because
`activewindow` is global and the bar wants the title of the window *each
output* is showing: the client with the lowest `focusHistoryID` on that
output's visible workspace. That is the same thing dwm's per-monitor `sel`
meant.

If Hyprland restarts underneath the bar, the event stream reconnects on its
own and a transient empty `j/monitors` reply is ignored rather than blanking
the panel.

## Windows: wlr-layer-shell

There are no override-redirect windows, dock hints, or grabs on Wayland, and
none are needed — `wlr-layer-shell` is the protocol for exactly this job.
`wlutil.cpp` wraps LayerShellQt.

| surface | layer | anchors | exclusive zone | keyboard |
|---|---|---|---|---|
| panel | Top | top + left + right | `panelHeight` | none |
| popup | Overlay | top + left | −1 | on-demand + activate-on-show |
| scrim | Top | all four, inset by `panelHeight` | −1 | none |
| launcher | Overlay | all four | −1 | on-demand + activate-on-show |

Three things fall out of that table, each replacing a pile of X11 code:

**Reserving the strip.** The panel's exclusive zone *is* the reservation. The
dwm build needed a patched `drawbar`, an unmapped `barwin`, a `DOCK`-window
special case in `manage()`, and a `barheight` in `config.h` that had to be kept
equal to `Theme::panelHeight` by hand. None of that exists here, and nothing in
`hyprland.conf` has to agree with anything in `theme.h`.

**Focus.** A popup asks for `KeyboardInteractivityOnDemand` plus
`activateOnShow`, so the compositor hands it the keyboard as it maps and gives
focus back to whatever had it on unmap. The X11 build had to `XGrabKeyboard`,
remember the previous `XGetInputFocus`, and hand it back manually while
unwinding a grab stack.

Not `Exclusive`, which is the obvious-looking choice and is wrong: it makes the
compositor route **pointer** input to the surface as well, so clicks intended
for the scrim or for the bar never arrive and the only way out of a popup is
Escape. `OnDemand` alone would not focus on open (breaking Escape and the
Wi-Fi password field), which is what `activateOnShow` restores.

**Dismissal.** A transparent scrim surface covers everything *except* the bar
strip while a popup is open; clicking it closes the popup. Popups are on
Overlay and the scrim on Top, so the protocol — not surface creation order —
guarantees the stacking. Leaving the bar uncovered is what keeps
click-to-toggle and click-another-pill working as they did under X11.

The one behavioural difference from X11: the dismissing click is *consumed* by
the scrim instead of being replayed to the window underneath. The X11 build
replayed it with `XAllowEvents(ReplayPointer)`; Wayland has no protocol for
that, and every Wayland shell behaves this way.

**Positioning.** A Wayland client is never told where it is on screen, so
`QWidget::mapToGlobal()` is meaningless across top-levels. Instead every
surface `wlutil` places records the output-relative origin it asked for, and
`WlUtil::screenPos()` walks a widget up to its top-level and adds that origin
back — exact, because we chose the margins ourselves.

## Submap indicator

`SubmapWidget` shows the active keybind submap, or `global` in the default one.
It is the one piece of state that is *not* re-queried: `submap>>NAME` carries
the whole answer, so the event is taken at its word and the three-round-trip
refresh is skipped. Only the initial value is read, in `refreshConfig()`.

Hyprland spells "no submap" two ways depending on where you ask — the `submap`
command answers with the literal string `default`, the event stream sends an
empty payload — so `normalizeSubmap()` folds both to an empty string.

Clicking leaves the submap (`hl.dsp.submap("reset")`, with the pre-0.56
spelling as fallback). It stays visible in the default submap rather than
hiding: the point of a modal-keybind indicator is telling you which mode you
are in, and "none" is one of the answers.

## Battery, or power draw

One slot, two occupants, picked by hardware rather than configuration:
`BatteryWidget` shows when a battery exists, `PowerDrawWidget` when one does
not. So a laptop gets charge and a desktop gets watts with nothing to set.

The power figure comes from two independent sources, either of which may be
missing. The GPU reports watts directly through amdgpu's hwmon (`power1_average`,
labelled PPT — whole board, not just the die) and is world-readable. The CPU
side is a *cumulative* microjoule counter, so watts means differencing
`energy_uj` across a sample window, handling the wrap at
`max_energy_range_uj`; and it is root-only by default as a PLATYPUS
mitigation, which `tools/install-rapl-udev.sh` is there to relax.

When the CPU counter is unreadable the script emits `cpu_locked=1` rather than
guessing, and the popup says which rule to run.

A third source outranks both: if `$FANHYPR_QS_UPS_HOST` names a reachable SNMP
card, the pill switches to measured wall power. `fanhypr-qs-ups` reads
CyberPower's enterprise MIB rather than RFC1628, because an RMCARD205 fills
RFC1628's `upsOutputPower` with 0 and exposes only percent load; the enterprise
tree has real watts. The component figures stay in the popup, muted, so the
share belonging to this machine is still legible. If the card stops answering
the script prints nothing and the widget falls back rather than freezing on a
stale reading. Deliberately absent is any
"total system" number: nothing on a desktop can see wall power without a smart
PSU or UPS, and labelling CPU+GPU as a total would invite exactly that
misreading.

## Which output things land on

Two different answers, because two different jobs:

**The tray** wants to stay put — an icon that hops screens is an icon you have
to hunt for. Hyprland has no notion of a primary monitor (there is no such
field on `j/monitors`), so rather than hardcoding connector names,
`HyprState::primaryMonitor()` derives it: **the output owning the
lowest-numbered workspace wins.** That falls straight out of the user's own
`workspace = N, monitor:X` rules. Falls back to the focused output, then to the
lowest monitor id.

**The launcher** wants to follow you — it is a thing you summon, so it should
appear where you are looking. It uses `HyprState::focusedMonitor()`, re-read on
every open.

Following focus means rebinding: a layer surface's `wl_output` is fixed when it
binds, so moving between outputs drops the native window and builds a new one
against the target. `LauncherWindow::openOn()` and `ShellPopup::openPopup()`
both do this when their screen has changed since the last map.

## Reaping helpers

Every process started through `procutil` gets `PR_SET_PDEATHSIG`, so the
kernel kills it when the bar's main thread goes away — including on SIGKILL,
which no cleanup code could cover. Without it the long-lived watchers outlived
the bar: a session where the bar had been restarted a dozen times accumulated
a dozen orphaned `... watch` processes, each still polling. A matching
`aboutToQuit` sweep terminates them on a clean exit. Programs the *user*
launches deliberately bypass this — they must outlive the bar.

## Widget dependencies

Only what you actually use needs to be installed; a missing helper makes that
one widget hide itself or show defaults.

- **Volume/mic** — native via libpulse (pipewire-pulse). No script, no tool.
- **Network** — `nmcli` (NetworkManager) enables the full Wi-Fi manager;
  without it the widget falls back to `iproute2` status + `net-bridge.sh`
  re-apply.
- **Bluetooth** — `bluetoothctl` (bluez).
- **VPN** — `wg-quick` (WireGuard) + passwordless `sudo` for it. Tunnel `tun1`,
  override with `$FANHYPR_QS_VPN_TUNNEL`.
- **Battery** — none; reads `/sys/class/power_supply/BAT*` directly. Override
  with `$FANHYPR_QS_BATTERY_PATH`.
- **CPU/Memory/Temperature** (System dropdown) — `free`; temperature needs a
  k10temp (AMD) or coretemp (Intel) `hwmon` node, else that row hides itself.
- **Weather** — `curl`; hits wttr.in (no API key, geolocates by IP).
  Fahrenheit by default, `$FANHYPR_QS_WEATHER_UNIT=C` for Celsius.
- **Notifications** — native over the session DBus; no dunst or helper script.
- **Submap** — none; straight off the Hyprland event stream.
- **Wallpaper** — `awww` (an swww fork) with `awww-daemon` running.
- **Power draw** (desktops) — none for GPU; CPU needs the RAPL udev rule,
  see `tools/install-rapl-udev.sh` and the README.
- **Clipboard** (System dropdown) — `wl-clipboard` (`wl-paste`, `wl-copy`).
- **Power** (System dropdown) — `loginctl` (elogind/systemd-logind) for
  power-off/reboot/suspend; logout uses `hyprctl dispatch exit`. Lock tries
  `hyprlock` then `swaylock`, else falls back to `loginctl lock-session`.

> The helper scripts **must** be on the `PATH` that `fanhypr-qs-shell`
> inherits — the widgets exec them by bare name. `make install` puts them in
> `/usr/local/bin` alongside the binary for exactly this reason.

## Clipboard

`clipboard.cpp` keeps the last 50 `CLIPBOARD` text entries with a
pick-one-to-restore popup, backed by **wl-clipboard**.

Not `QClipboard`, which is what the X11 build used: on Wayland a client may
only read the clipboard while it holds focus, so `QClipboard::dataChanged`
never fires for a copy made in another window — which is every copy worth
recording. Reading it regardless is what the `wlr-data-control` protocol is
for, and `wl-paste --watch` is its reference implementation: the compositor
hands it each new selection no matter who owns the focus.

`--watch` runs a command per selection with the content on stdin, and puts no
delimiter between them, so the watcher runs:

```sh
wl-paste --type text --watch sh -c 'cat; printf "\000"'
```

`cat` copies the payload through byte for byte and the `printf` terminates it,
giving a NUL-delimited stream that `BlockWatchProcess` splits — exact bytes,
no newline mangling, and no polling. Restoring an entry pipes it to `wl-copy`,
which forks its own server to hold the selection afterwards; the watcher sees
that come back round and the existing dedup promotes it to the front instead
of duplicating it.

Without `wl-paste` on `PATH` the widget hides itself, the same way the battery
and weather widgets do.

## Wallpaper picker

A fullscreen Overlay surface in the launcher's mould, raised over IPC
(`ipc call wallpaper toggle`) so it can live on a keybind. Arrow keys move,
Enter/Space applies and closes, Escape or a click outside the box dismisses.

`awww img <path>` sets every output at once — that is the CLI default, so no
`--outputs` is passed. `awww query` reports what each output currently shows;
that image gets an accent frame, distinct from the surface fill marking the
keyboard cursor, so "selected" and "applied" stay tellable apart. Opening the
picker parks the cursor on the applied wallpaper.

`$FANHYPR_QS_WALLPAPER_TRANSITION` picks the awww transition (default `fade`)
and `$FANHYPR_QS_WALLPAPER_DIR` the directory. Like every other knob here these
are read from the bar's environment, which comes from the compositor — a shell
`export` never reaches a process started at login, so they belong in
`hyprland.lua`'s `hl.env`.

The directory is scanned **non-recursively** on purpose: a `backup/` subtree of
old wallpapers lives under it, and that is not what a picker should offer.

Thumbnails are decoded one per event-loop turn rather than in a batch.
Ten wallpapers cost ~440ms even with `QImageReader::setScaledSize` (which lets
libjpeg/libpng downscale *during* the read instead of decoding a 12MB PNG in
full) — enough to be a visible hitch. Spreading the work keeps the UI
responsive, and since it starts at login the thumbnails are ready well before
the picker is first opened. That is also why `WallpaperPicker` is constructed
eagerly in `main()`: left lazy, the first open would both pay the decode cost
on the spot and race its own `awww query`, so the cursor would land on the
wrong cell.

## Control socket

`ipc.{h,cpp}` runs a `QLocalServer` at
`$XDG_RUNTIME_DIR/fanhypr-qs-shell-$HYPRLAND_INSTANCE_SIGNATURE.sock`, keyed
per compositor instance so two sessions on one machine don't collide.

```
fanhypr-qs-shell ipc call launcher toggle|show|hide
fanhypr-qs-shell ipc call runner   toggle|show|hide
fanhypr-qs-shell ipc call notifications toggle-dnd
fanhypr-qs-shell ipc call vpn      toggle|refresh
fanhypr-qs-shell ipc call wallpaper toggle|show|hide
```

`--no-duplicate` makes a second instance exit immediately if one is already
listening.

## What the X11 build had that this doesn't

- **Status text.** dwm's bar read `dwmblocks`-style segments off the root
  window's `WM_NAME`. There is no root window and no equivalent protocol on
  Wayland; the segments are gone. Anything that lived there belongs in a widget.
- **Tag masks.** dwm tags were a 9-bit mask with several tags viewable at once.
  Hyprland has one active workspace per output, so the buttons are a fixed
  per-output block instead (see README).
- **Keyboard-layout widget.** It was `setxkbmap`/`xkb-switch`-based and X11-only.
  Hyprland reports layout through `j/devices` and the `activelayout` event — a
  clean re-add, just not ported yet.
- **`dwm-qs-state`.** The C bridge that polled EWMH properties is deleted
  outright; `hypripc.cpp` replaces it with an event-driven socket.
