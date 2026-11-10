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
├── fanvid-qs/                video wallpapers, if wanted (install.sh clones it)
└── fanhypr-qs/
    ├── install.sh, tools/link-config
    └── config/
        ├── hypr/             ~/.config/hypr: hyprland.lua and its modules,
        │                     per-host files under */<hostname>.lua
        └── xdg-desktop-portal/hyprland-portals.conf
```

## Requirements

Package names are Fedora / Gentoo / Arch. `install.sh` checks for all of
them, the optional ones included, and offers to install what is missing
(`dnf`, `emerge` or `pacman`, through sudo). It stops without a required
one; an optional one it could not install (a Gentoo USE flag `emerge` does
not turn on, say) is reported and left out. It asks first whether to
install video wallpapers, and checks for their packages only if so. On
Fedora it enables RPM Fusion first when it needs its `ffmpeg` or VA-API
driver.

### Required

The compositor and the session:

| What | Fedora | Gentoo | Arch | For |
|---|---|---|---|---|
| Hyprland | built from source (`tools/build-hyprland`, see [Hyprland on Fedora](#hyprland-on-fedora)) | `gui-wm/hyprland` | `hyprland` | the compositor, and `hyprctl`. `config/hypr` is a Lua config, which needs Hyprland 0.56 or later; check your distribution's version |
| Hyprland's portal | built with Hyprland | `gui-libs/xdg-desktop-portal-hyprland` | `xdg-desktop-portal-hyprland` | screen sharing and screenshots for other programs (`hyprland-portals.conf`) |
| xdg-desktop-portal and its GTK backend | `xdg-desktop-portal` `xdg-desktop-portal-gtk` | `sys-apps/xdg-desktop-portal` `sys-apps/xdg-desktop-portal-gtk` | `xdg-desktop-portal` `xdg-desktop-portal-gtk` | file pickers and the colour scheme |
| D-Bus | `dbus-tools` | `sys-apps/dbus` | `dbus` | notifications, the tray, media controls |
| glib2 (`gsettings`) | `glib2` | `dev-libs/glib` | `glib2` | GTK theme, icons, font and colour scheme |

Building the shell. Its X11 side is always built (the same binary runs the
qtile and oxwm sessions, and the XEmbed tray works through XWayland), so
its libraries are needed here too:

| What | Fedora | Gentoo | Arch | For |
|---|---|---|---|---|
| A C++ compiler, make, pkg-config | `gcc-c++` `make` `pkgconf-pkg-config` | `sys-devel/gcc` `dev-build/make` `dev-util/pkgconf` | `gcc` `make` `pkgconf` | building |
| Qt 6 base | `qt6-qtbase-devel` | `dev-qt/qtbase` | `qt6-base` | the shell; on Gentoo with USE `dbus gui network widgets X wayland` |
| Qt's Wayland platform plugin | `qt6-qtwayland` | part of `dev-qt/qtbase` with USE `wayland` (Qt 6.10 and later; `dev-qt/qtwayland` before) | `qt6-wayland` | running as a Wayland client |
| layer-shell-qt | `layer-shell-qt-devel` | `kde-plasma/layer-shell-qt` | `layer-shell-qt` | the panel and popups as layer-shell surfaces |
| Wayland client, cursor and scanner | `wayland-devel` | `dev-libs/wayland` `dev-util/wayland-scanner` | `wayland` | the lock screen, idle and screen power |
| wayland-protocols | `wayland-protocols-devel` | `dev-libs/wayland-protocols` | `wayland-protocols` | ext-session-lock and ext-idle-notify |
| xkbcommon | `libxkbcommon-devel` | `x11-libs/libxkbcommon` | `libxkbcommon` | the lock screen's keyboard |
| libxcb | `libxcb-devel` | `x11-libs/libxcb` | `libxcb` | the X11 side |
| X11, XComposite, XDamage, XFixes | `libX11-devel` `libXcomposite-devel` `libXdamage-devel` `libXfixes-devel` | `x11-libs/libX11` `x11-libs/libXcomposite` `x11-libs/libXdamage` `x11-libs/libXfixes` | `libx11` `libxcomposite` `libxdamage` `libxfixes` | the XEmbed tray (Wine and other X11 tray icons) |
| libxkbfile | `libxkbfile-devel` | `x11-libs/libxkbfile` | `libxkbfile` | the X11 Keyboard settings |
| libpulse | `pulseaudio-libs-devel` | `media-libs/libpulse` | `libpulse` | volume and microphone (works against pipewire-pulse) |
| PAM | `pam-devel` | `sys-libs/pam` | `pam` | the lock screen's password check |

What the shell runs under Hyprland:

| What | Fedora | Gentoo | Arch | For |
|---|---|---|---|---|
| wl-clipboard | `wl-clipboard` | `gui-apps/wl-clipboard` | `wl-clipboard` | clipboard history and copying |
| grim | `grim` | `gui-apps/grim` | `grim` | screenshots and the colour picker |
| jq | `jq` | `app-misc/jq` | `jq` | the Displays popup's saved layouts (`fanshell-qs-outputs`) |

### Video wallpapers

Optional: [fanvid-qs](https://github.com/iFanatical/fanvid-qs) plays them,
decoding each video once on the GPU for every output that shows it.
`install.sh` asks whether to install it (`VIDEO_WALLPAPERS=yes` or `no`
answers without asking; with no terminal, an installed fanvid is updated and
a missing one left out). If so, it clones fanvid-qs beside this repo, unless
it is there already or at `$FANVID_QS_DIR`, installs these, and builds and
installs it into `/usr/local/bin`. Without it, the shell's picker offers
only images. Answering no leaves an installed fanvid in place.

| What | Fedora | Gentoo | Arch | For |
|---|---|---|---|---|
| FFmpeg | `ffmpeg` `ffmpeg-devel` from RPM Fusion: Fedora's own `ffmpeg-free` decodes neither H.264 nor HEVC, which most video wallpapers are | `media-video/ffmpeg` with USE `vaapi` | `ffmpeg` | decoding, and each video's first frame |
| libva, libdrm | `libva-devel` `libdrm-devel` | `media-libs/libva` `x11-libs/libdrm` | `libva` `libdrm` | handing frames from the GPU to Hyprland |
| Wayland client, scanner and protocols | `wayland-devel` `wayland-protocols-devel` | `dev-libs/wayland` `dev-util/wayland-scanner` `dev-libs/wayland-protocols` | `wayland` `wayland-protocols` | building fanvid |
| The GPU's VA-API driver | Intel: `intel-media-driver` (RPM Fusion nonfree); AMD: `mesa-va-drivers-freeworld` (RPM Fusion) | Intel: `media-libs/libva-intel-media-driver`; AMD: `media-libs/mesa` with USE `vaapi` | Intel: `intel-media-driver`; AMD: `libva-mesa-driver` | decoding on the GPU (optional; without it a video shows its first frame) |
| libva-utils | `libva-utils` | `media-video/libva-utils` | `libva-utils` | `vainfo`, which lists what the GPU decodes |

A video the GPU cannot decode (AV1 on Intel graphics before 11th
generation, for example) shows its first frame instead of playing.

Not needed any more, and best removed from autostart if another config
starts them: awww or swww, hyprpaper, hyprvibrance, hyprsunset, hyprlock,
hypridle and hyprshot. The shell does each of their jobs, and some would
fight it over the same thing (the screen shader, the wallpaper layer).

### Used by the config

| What | Fedora | Gentoo | Arch | For |
|---|---|---|---|---|
| XWayland | `xorg-x11-server-Xwayland` | `x11-base/xwayland` | `xorg-xwayland` | X11 programs, and their tray icons |
| Alacritty | `alacritty` | `x11-terms/alacritty` | `alacritty` | the terminal (Mod+Return) |
| A polkit agent | `mate-polkit` | `mate-extra/mate-polkit` | `mate-polkit` | password prompts (`user/autostart.lua`) |
| xsettingsd | `xsettingsd` | `x11-misc/xsettingsd` | `xsettingsd` | GTK/Qt settings for XWayland programs |
| Thunar, Firefox, pavucontrol | `thunar` `firefox` `pavucontrol` | `xfce-base/thunar` `www-client/firefox` `media-sound/pavucontrol` | `thunar` `firefox` `pavucontrol` | Mod+E, Mod+Alt+B, Mod+P (`user/programs.lua`) |

The programs behind the special workspaces and a few keys are yours to
install, and their keys do nothing without them: Discord and TeamSpeak
(per host, under `user/keybindings/`), `tokyo-music-tui` for Tokyo
Music (Mod+T), and gpu-screen-recorder-ui (`gsr-ui`).

The shell's optional features (NetworkManager, BlueZ, brightnessctl,
ddcutil, power-profiles-daemon, WireGuard, the weather) need the same
packages as under X11: see fanshell-qs's
[Optional](https://github.com/iFanatical/fanshell-qs#optional).

### Installing

Everything required and used by the config (`install.sh` also installs
the optional packages above and in fanshell-qs's
[Optional](https://github.com/iFanatical/fanshell-qs#optional)):

```sh
# Fedora (Hyprland itself from source: tools/build-hyprland, below)
sudo dnf install xdg-desktop-portal \
    xdg-desktop-portal-gtk dbus-tools glib2 \
    gcc-c++ make pkgconf-pkg-config qt6-qtbase-devel qt6-qtwayland \
    layer-shell-qt-devel wayland-devel wayland-protocols-devel \
    libxkbcommon-devel libxcb-devel libX11-devel libXcomposite-devel \
    libXdamage-devel libXfixes-devel libxkbfile-devel pulseaudio-libs-devel \
    pam-devel wl-clipboard grim jq \
    xorg-x11-server-Xwayland alacritty mate-polkit xsettingsd

# Gentoo (dev-qt/qtbase with USE "dbus gui network widgets X wayland")
sudo emerge gui-wm/hyprland gui-libs/xdg-desktop-portal-hyprland \
    sys-apps/xdg-desktop-portal sys-apps/xdg-desktop-portal-gtk \
    sys-apps/dbus dev-libs/glib \
    dev-qt/qtbase kde-plasma/layer-shell-qt dev-libs/wayland \
    dev-util/wayland-scanner dev-libs/wayland-protocols x11-libs/libxkbcommon \
    x11-libs/libxcb x11-libs/libX11 x11-libs/libXcomposite \
    x11-libs/libXdamage x11-libs/libXfixes x11-libs/libxkbfile \
    media-libs/libpulse sys-libs/pam gui-apps/wl-clipboard gui-apps/grim \
    app-misc/jq x11-base/xwayland x11-terms/alacritty \
    mate-extra/mate-polkit x11-misc/xsettingsd

# Arch
sudo pacman -S hyprland xdg-desktop-portal-hyprland xdg-desktop-portal \
    xdg-desktop-portal-gtk dbus glib2 \
    gcc make pkgconf qt6-base qt6-wayland layer-shell-qt wayland \
    wayland-protocols libxkbcommon libxcb libx11 libxcomposite libxdamage \
    libxfixes libxkbfile libpulse pam wl-clipboard grim jq \
    xorg-xwayland alacritty mate-polkit xsettingsd
```

Plus a Nerd Font (`JetBrainsMono Nerd Font Propo`, from
[nerdfonts.com](https://www.nerdfonts.com)) for the bar's icons, as in
the X sessions.

### Hyprland on Fedora

Fedora's own `hyprland` package is orphaned (its last build, 0.45, is for
Fedora 42), and the current ones come from personal COPR repositories.
`tools/build-hyprland` builds Hyprland and the libraries it needs from
their latest releases on GitHub instead, into `/usr/local`:
hyprwayland-scanner, hyprutils, hyprlang, hyprcursor, hyprgraphics,
hyprwire, aquamarine, hyprland-protocols, hyprtoolkit, Hyprland,
xdg-desktop-portal-hyprland and hyprland-guiutils, with their build
dependencies from Fedora's own repositories. Hyprland's Lua config wants
Lua 5.5, which Fedora 44 does not have, so it builds that from lua.org too
(a static library, linked into Hyprland).

```sh
tools/build-hyprland [--dry-run]   # build what has a newer release, and install it
```

Run it again to update: a part is rebuilt when its latest release is newer
than the one installed, or when a part it builds on was rebuilt. Each
part's previous files are removed first, so nothing stale is left behind.
It adds `/usr/local/lib64` to the dynamic linker's path, and links the
portal's definition into `/usr/share/xdg-desktop-portal/portals`, the
only place xdg-desktop-portal looks.

### A fresh Fedora install

`bootstrap.sh` sets up the whole desktop on a minimal Fedora (Server)
install, from the console:

```sh
sudo dnf install -y git
git clone https://github.com/iFanatical/fanhypr-qs.git ~/projects/window-managers/fanhypr-qs
~/projects/window-managers/fanhypr-qs/bootstrap.sh [--dry-run]
```

It asks first for the machine's hostname, whether to install video
wallpapers, whether to hide the Fedora name, and whether to turn SELinux
off; the rest runs unattended, with one sudo password. It updates the
system, installs what a minimal install lacks for a desktop (Mesa, PipeWire,
fonts), clones (or pulls) fanshell-qs, fanhypr-qs and, for video
wallpapers, fanvid-qs, starts this machine's files in `config/hypr` from
another's when its hostname is new, fetches the shell's Nerd Font, builds
Hyprland (`tools/build-hyprland`), and runs `install.sh`. Hiding the
Fedora name hides the boot menu (it still appears after a failed boot, or
with Shift held), drops the boot splash's logo, and puts the hostname in the
console login's banner; `/etc/os-release` stays as it is, as `dnf` needs
it. Run it again to update everything.

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
