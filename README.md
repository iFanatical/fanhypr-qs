# fanhypr-qs

The [Hyprland](https://hyprland.org) session for
[fanshell-qs](https://github.com/iFanatical/fanshell-qs), the C++/Qt bar and
desktop shell. The shell itself, with its scripts, lives in fanshell-qs and
runs under qtile and oxwm too
([fanqtile-qs](https://github.com/iFanatical/fanqtile-qs),
[fanoxwm-qs](https://github.com/iFanatical/fanoxwm-qs)). This repo has what
only Hyprland needs: its config and which portal answers.

Until fanshell-qs grew a Hyprland backend this repo was the bar itself
(`fanhypr-qs-shell`); its history is still here, and the shell's code
carries on in fanshell-qs, which started from it.

## Status

Under Hyprland the shell is a set of wlr-layer-shell surfaces, and has the
same feature set as under qtile:

- the panel, popups, tray, notifications, OSDs, launcher, menu and the
  submap pill, on Hyprland's IPC;
- the wallpaper, drawn by the shell on the background layer (no awww);
- Displays: mode, refresh, rotation, scale, adaptive sync and arrangement,
  as Hyprland monitor rules, with saved layouts per set of monitors;
- vibrance and the night light, in one screen shader (no hyprvibrance or
  hyprsunset);
- the lock screen (ext-session-lock-v1), lock on idle and before sleep, and
  screen power (ext-idle-notify-v1, wlr-output-power-management);
- Keyboard and Mouse & touchpad, as Hyprland input and device options;
- screenshots and the colour picker (grim), game mode, keep awake.

## Layout

```text
~/projects/window-managers/
├── fanshell-qs/              the shell (build and install it too)
└── fanhypr-qs/
    ├── install.sh, tools/link-config
    └── config/
        ├── hypr/             ~/.config/hypr: hyprland.lua and its modules,
        │                     per-host files under */<hostname>.lua
        └── xdg-desktop-portal/hyprland-portals.conf
```

## Requirements

| What | Fedora | Gentoo | For |
|---|---|---|---|
| fanshell-qs | — | — | the bar and shell; see its README |
| Hyprland 0.56 (Lua config) | `hyprland` | `gui-wm/hyprland` | the compositor; `hyprland.lua` uses the Lua API |
| xdg-desktop-portal-hyprland | `xdg-desktop-portal-hyprland` | `gui-libs/xdg-desktop-portal-hyprland` | screen sharing |

and the shell's Wayland side, which `install.sh` checks for
(`need_shell_wayland` in fanshell-qs's `tools/lib.sh`):

```sh
# Gentoo (dev-qt/qtbase with USE wayland)
sudo emerge gui-wm/hyprland gui-libs/xdg-desktop-portal-hyprland \
    kde-plasma/layer-shell-qt dev-libs/wayland dev-libs/wayland-protocols \
    dev-util/wayland-scanner x11-libs/libxkbcommon gui-apps/grim \
    gui-apps/wl-clipboard app-misc/jq
```

## Running it

```sh
./install.sh               # check dependencies, link the session, install fanshell-qs
```

It expects `../fanshell-qs` (or `$FANSHELL_QS_DIR`), links `config/hypr` to
`~/.config/hypr` (a real directory there is moved to `hypr.bak-DATE`),
removes the old `fanhypr-qs-*` programs from `/usr/local/bin`, and runs
fanshell-qs's `tools/install-shell` with `WAYLAND=1`, so the build fails
rather than leaving Hyprland support out. `--dry-run` lists the steps.

Hyprland starts the shell (`user/autostart.lua`). The shell's settings are
`~/.config/fanshell-qs/shell.conf`, the same file under every window
manager (`vpn.tunnels`, `ups.host`, ...), not Hyprland environment
variables.

What the shell changes at run time -- monitor rules, input and device
options, the screen shader -- it applies again after a config reload, which
puts `hyprland.lua`'s back.
