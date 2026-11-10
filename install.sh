#!/bin/sh
# Install the Hyprland session: the session links and fanshell-qs, the bar,
# and if wanted fanvid-qs, which plays video wallpapers.
#
#     ./install.sh [--dry-run]
#
# Run it as yourself; only the installs into /usr/local use sudo. Safe to
# run again after a `git pull` here, in fanshell-qs or in fanvid-qs. It
# stops at the first failure, naming the step.
#
# Expects fanshell-qs beside this repo (../fanshell-qs) or at
# $FANSHELL_QS_DIR. It asks whether to install video wallpapers;
# VIDEO_WALLPAPERS=yes or no answers without asking. fanvid-qs is used
# beside this repo (../fanvid-qs) or at $FANVID_QS_DIR, and cloned there if
# it is not. Hyprland itself comes from the distribution.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
shell_dir=${FANSHELL_QS_DIR:-"$repo/../fanshell-qs"}
if [ ! -f "$shell_dir/tools/lib.sh" ]; then
    printf 'error: fanshell-qs not found at %s; clone it there or set FANSHELL_QS_DIR\n' \
        "$shell_dir" >&2
    exit 1
fi
# parse_dry_run, step, run, die, require_user, need, need_shell,
# need_shell_wayland, need_shell_optional, need_optional_begin/end,
# install_needed, link
. "$shell_dir/tools/lib.sh"
parse_dry_run "$@"
require_user
command -v need_shell_optional >/dev/null 2>&1 ||
    die "fanshell-qs at $shell_dir is older than this repo; git pull there and run again"
flag=
[ "$dry_run" = 1 ] && flag=--dry-run
prefix=${PREFIX:-/usr/local}

# Video wallpapers are fanvid-qs's, an add-on: asked, as it brings FFmpeg
# and the GPU's video driver with it. Without it the shell offers only
# images. No undoes nothing: an installed fanvid stays.
step "fanhypr-qs: video wallpapers"
installed=no
command -v fanvid >/dev/null 2>&1 && installed=yes
video=${VIDEO_WALLPAPERS:-}
if [ -z "$video" ] && [ -t 0 ]; then
    if [ "$installed" = yes ]; then
        printf 'fanvid is installed; update it? [Y/n] '
        default=yes
    else
        printf 'install video wallpapers (fanvid-qs, with FFmpeg and the GPU'"'"'s video driver)? [y/N] '
        default=no
    fi
    read -r answer || answer=
    case $answer in
    [Yy] | [Yy][Ee][Ss]) video=yes ;;
    "") video=$default ;;
    *) video=no ;;
    esac
elif [ -z "$video" ]; then
    # No terminal to ask on: keep things as they are.
    video=$installed
fi
case $video in
yes | no) ;;
*) die "VIDEO_WALLPAPERS is yes or no, not $video" ;;
esac
fanvid_dir=${FANVID_QS_DIR:-"$repo/../fanvid-qs"}
if [ "$video" = no ]; then
    echo "skipped"
elif [ -f "$fanvid_dir/tools/need.sh" ]; then
    echo "fanvid-qs at $fanvid_dir"
elif [ -e "$fanvid_dir" ]; then
    die "$fanvid_dir is not a fanvid-qs checkout (no tools/need.sh); git pull there, or set FANVID_QS_DIR"
else
    command -v git >/dev/null 2>&1 || die "git is needed to fetch fanvid-qs"
    run git clone https://github.com/iFanatical/fanvid-qs.git "$fanvid_dir"
fi
# A dry run clones nothing, so it cannot check fanvid's dependencies.
if [ "$video" = yes ] && [ -f "$fanvid_dir/tools/need.sh" ]; then
    . "$fanvid_dir/tools/need.sh"
elif [ "$video" = yes ]; then
    echo "  (its dependencies are checked once it is cloned)"
    video=dry
fi

step "fanhypr-qs: checking dependencies"
need Hyprland hyprland gui-wm/hyprland hyprland "the compositor"
need hyprctl hyprland gui-wm/hyprland hyprland "the shell's Hyprland IPC fallbacks"
need /usr/share/xdg-desktop-portal/portals/hyprland.portal \
    xdg-desktop-portal-hyprland gui-libs/xdg-desktop-portal-hyprland \
    xdg-desktop-portal-hyprland "screen sharing (config/xdg-desktop-portal)"
# The shell's too, X11 and Wayland sides, so everything is checked (and
# asked about once) before anything is linked or built.
need_shell
need_shell_wayland
# Everything else too, so a fresh install has every feature: what the shell
# can do without (Wi-Fi, Bluetooth, ...) and what the
# config starts or binds. One still missing after the install (a Gentoo USE
# flag, say) is reported, not fatal.
need_shell_optional
need_optional_begin
need Xwayland xorg-x11-server-Xwayland x11-base/xwayland xorg-xwayland \
    "X11 programs, and their tray icons"
need firefox firefox www-client/firefox firefox "Mod+Alt+B"
need pavucontrol pavucontrol media-sound/pavucontrol pavucontrol "Mod+P"
need_optional_end
[ "$video" = yes ] && need_fanvid
install_needed

step "fanhypr-qs: linking the session (hypr/, hyprland-portals.conf)"
run "$repo/tools/link-config"

# fanhypr-qs installed its own shell and scripts before they moved into
# fanshell-qs; Hyprland now starts fanshell-qs-shell instead.
step "fanhypr-qs: removing the pre-merge fanhypr-qs shell"
found=
for name in shell battery bt dunst menu-action net power powerdraw restart \
            sysinfo ups vpn weather; do
    [ -e "$prefix/bin/fanhypr-qs-$name" ] && found="$found $prefix/bin/fanhypr-qs-$name"
done
if [ -n "$found" ]; then
    # A running old bar would hold the notification name the new one needs.
    if pgrep -u "$(id -u)" -x fanhypr-qs-shel >/dev/null 2>&1; then
        run "$prefix/bin/fanhypr-qs-shell" ipc call shell quit || true
    fi
    # shellcheck disable=SC2086
    run sudo rm -f $found
else
    echo "none left"
fi

# Before the shell, so the bar it restarts finds fanvid.
if [ "$video" = yes ]; then
    step "fanvid-qs: building and installing into $prefix/bin"
    run make -C "$fanvid_dir"
    run sudo make -C "$fanvid_dir" install PREFIX="$prefix"
elif [ "$video" = dry ]; then
    step "fanvid-qs: building and installing into $prefix/bin"
    printf '  would: make -C %s\n' "$fanvid_dir"
    printf '  would: sudo make -C %s install PREFIX=%s\n' "$fanvid_dir" "$prefix"
fi

# Last, so the bar restarts onto everything above. WAYLAND=1: a missing
# Wayland dependency fails the build rather than leaving Hyprland out.
export WAYLAND=1
# shellcheck disable=SC2086
"$shell_dir/tools/install-shell" $flag

step "fanhypr-qs: done"
echo "Start Hyprland from a console (Hyprland, or start-hyprland), or reload a running one."
