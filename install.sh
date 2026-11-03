#!/bin/sh
# Install the Hyprland session: the session links and fanshell-qs, the bar.
#
#     ./install.sh [--dry-run]
#
# Run it as yourself; only the shell's install into /usr/local uses sudo.
# Safe to run again after a `git pull` here or in fanshell-qs. It stops at
# the first failure, naming the step.
#
# Expects fanshell-qs beside this repo (../fanshell-qs) or at
# $FANSHELL_QS_DIR. Hyprland itself comes from the distribution.
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
# can do without (video wallpapers, Wi-Fi, Bluetooth, ...) and what the
# config starts or binds. One still missing after the install (a Gentoo USE
# flag, say) is reported, not fatal.
need_shell_optional
need_optional_begin
need Xwayland xorg-x11-server-Xwayland x11-base/xwayland xorg-xwayland \
    "X11 programs, and their tray icons"
need firefox firefox www-client/firefox firefox "Mod+Alt+B"
need pavucontrol pavucontrol media-sound/pavucontrol pavucontrol "Mod+P"
need_optional_end
install_needed

step "fanhypr-qs: linking the session (hypr/, hyprland-portals.conf)"
run "$repo/tools/link-config"

# fanhypr-qs installed its own shell and scripts before they moved into
# fanshell-qs; Hyprland now starts fanshell-qs-shell instead.
step "fanhypr-qs: removing the pre-merge fanhypr-qs shell"
prefix=${PREFIX:-/usr/local}
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

# Last, so the bar restarts onto everything above. WAYLAND=1: a missing
# Wayland dependency fails the build rather than leaving Hyprland out.
export WAYLAND=1
# shellcheck disable=SC2086
"$shell_dir/tools/install-shell" $flag

step "fanhypr-qs: done"
echo "Start Hyprland from a console (Hyprland, or start-hyprland), or reload a running one."
