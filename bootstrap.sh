#!/bin/sh
# Set up this desktop on a fresh Fedora minimal (Server) install, from a
# console: the desktop basics a minimal install lacks, Hyprland built from
# source, and fanshell-qs, fanhypr-qs and (if wanted) fanvid-qs.
#
#     sudo dnf install -y git
#     git clone https://github.com/iFanatical/fanhypr-qs.git ~/projects/window-managers/fanhypr-qs
#     ~/projects/window-managers/fanhypr-qs/bootstrap.sh [--dry-run]
#
# Run it as yourself; it asks for your sudo password once. It asks its
# questions first and then runs to the end. Run it again later to update
# everything: it pulls the three repos, rebuilds what has a newer release
# (tools/build-hyprland), and installs again.
#
# The repos live in ~/projects/window-managers (WM_DIR elsewhere). The
# questions come first (one more, if this machine's name is new to
# fanhypr-qs, once the repos are there); the rest runs unattended.
#
# The whole script is one function, called from its last line: the shell
# has read all of it before it runs, so pulling a newer copy of this repo
# while it runs cannot change what it does.

main() {
    set -eu
    dry_run=0
    case "${1:-}" in
    "") ;;
    -n | --dry-run) dry_run=1 ;;
    *)
        printf 'usage: %s [--dry-run]\n' "$0" >&2
        exit 2
        ;;
    esac
    flag=
    [ "$dry_run" = 1 ] && flag=--dry-run

    say() { printf '\n==> %s\n' "$*"; }
    die() { printf 'error: %s\n' "$*" >&2; exit 1; }
    run() {
        if [ "$dry_run" = 1 ]; then
            printf '  would: %s\n' "$*"
        else
            "$@"
        fi
    }
    # ask VAR QUESTION DEFAULT(yes/no)
    ask() {
        if [ "$3" = yes ]; then _hint='[Y/n]'; else _hint='[y/N]'; fi
        printf '%s %s ' "$2" "$_hint"
        read -r _a || _a=
        case $_a in
        [Yy] | [Yy][Ee][Ss]) eval "$1=yes" ;;
        [Nn] | [Nn][Oo]) eval "$1=no" ;;
        *) eval "$1=$3" ;;
        esac
    }

    [ "$(id -u)" -ne 0 ] || die "run this as your user; it uses sudo where it must"
    [ -r /etc/os-release ] && . /etc/os-release
    [ "${ID:-}" = fedora ] || die "this is for Fedora (this is ${ID:-unknown})"
    [ -t 0 ] || die "run this from a terminal: it asks a few questions first"
    command -v sudo >/dev/null 2>&1 || die "sudo is needed"
    command -v git >/dev/null 2>&1 || die "git is needed first: sudo dnf install -y git"

    wm_dir=${WM_DIR:-"$HOME/projects/window-managers"}
    github=https://github.com/iFanatical

    # ------------------------------------------------------------ questions
    say "a few questions first; then it runs to the end"
    current=$(hostname 2>/dev/null || cat /etc/hostname)
    printf 'hostname for this machine [%s]: ' "$current"
    read -r host || host=
    host=${host:-$current}
    case $host in
    *[!a-z0-9-]* | -* | *-) die "a hostname is lower-case letters, digits and inner hyphens: $host" ;;
    esac
    ask video "video wallpapers (fanvid-qs, with FFmpeg and the GPU's video driver)?" yes
    ask debrand "hide the Fedora name (boot menu, boot splash, console login)?" yes
    selinux_state=$(getenforce 2>/dev/null || echo Disabled)
    if [ "$selinux_state" = Disabled ]; then
        selinux=no
    else
        ask selinux "disable SELinux (now $selinux_state)?" no
    fi

    # ------------------------------------------------------------ sudo
    say "sudo"
    sudo -v || die "sudo failed"
    # Kept fresh until this script ends, so the long builds never stop to
    # ask again.
    (while kill -0 $$ 2>/dev/null; do sudo -n true 2>/dev/null; sleep 50; done) &
    keepalive=$!
    trap 'kill $keepalive 2>/dev/null || true' EXIT INT TERM

    # ------------------------------------------------------------ the repos
    say "the repos, in $wm_dir"
    run mkdir -p "$wm_dir"
    for r in fanshell-qs fanhypr-qs fanvid-qs; do
        [ "$r" = fanvid-qs ] && [ "$video" = no ] && [ ! -d "$wm_dir/$r" ] && continue
        if [ -d "$wm_dir/$r/.git" ]; then
            printf 'pull    %s\n' "$r"
            run git -C "$wm_dir/$r" pull --ff-only ||
                die "$r has changes of its own; commit or stash them, and run again"
        elif [ -e "$wm_dir/$r" ]; then
            die "$wm_dir/$r is in the way and is not a git checkout"
        else
            printf 'clone   %s\n' "$r"
            run git clone "$github/$r.git" "$wm_dir/$r"
        fi
    done
    hypr=$wm_dir/fanhypr-qs

    # fanhypr-qs keeps a file per machine (monitors, workspaces, keys and
    # environment), by hostname. A new name starts from another machine's.
    # Hyprland reads /etc/hostname and loads <hostname>.lua from each of
    # config/environment, config/workspaces, settings/monitors and
    # user/keybindings, falling back on the defaults.
    per_host=$hypr/config/hypr
    if [ "$dry_run" = 0 ] && [ -d "$per_host" ] &&
        [ -z "$(find "$per_host" -mindepth 3 -name "$host.lua" -print -quit)" ]; then
        known=$(find "$per_host" -mindepth 3 -name '*.lua' | sed 's|.*/||; s|\.lua$||' |
            grep -vx default | sort -u | tr '\n' ' ')
        say "fanhypr-qs has no files for $host"
        printf 'start from which machine'"'"'s (%s), or none? [fanteks-gentoo]: ' "$known"
        read -r from || from=
        from=${from:-fanteks-gentoo}
        if [ "$from" != none ]; then
            [ -n "$(find "$per_host" -mindepth 3 -name "$from.lua" -print -quit)" ] ||
                die "fanhypr-qs has no files for $from"
            find "$per_host" -mindepth 3 -name "$from.lua" | while read -r f; do
                cp "$f" "$(dirname "$f")/$host.lua"
                printf 'copied  %s -> %s.lua\n' "${f#"$per_host"/}" "$host"
            done
            echo "(new files in fanhypr-qs: commit them once they suit this machine)"
        fi
    fi

    # ------------------------------------------------------------ system
    say "updating the system"
    run sudo dnf upgrade -y --refresh

    say "the desktop basics a minimal install lacks"
    # GPU (Mesa's OpenGL and Vulkan), sound (PipeWire, which fanshell's
    # volume controls talk to as PulseAudio), the shell's fonts but its Nerd
    # Font (below), and what a desktop session expects to find.
    run sudo dnf install -y \
        git curl tar xz \
        mesa-dri-drivers mesa-vulkan-drivers vulkan-loader \
        pipewire pipewire-pulseaudio pipewire-alsa wireplumber \
        rsms-inter-fonts google-noto-sans-fonts google-noto-color-emoji-fonts \
        dejavu-sans-fonts fontconfig \
        xdg-user-dirs polkit

    if [ "$host" != "$current" ]; then
        say "hostname: $current -> $host"
        run sudo hostnamectl set-hostname "$host"
    fi

    # ------------------------------------------------------------ the font
    say "the shell's Nerd Font"
    if fc-list 2>/dev/null | grep -q 'JetBrainsMono Nerd Font Propo'; then
        echo ok
    else
        fonts=$HOME/.local/share/fonts/JetBrainsMonoNerd
        run mkdir -p "$fonts"
        if [ "$dry_run" = 1 ]; then
            printf '  would: fetch the latest JetBrainsMono.tar.xz from nerd-fonts into %s\n' "$fonts"
        else
            curl -fsSL https://github.com/ryanoasis/nerd-fonts/releases/latest/download/JetBrainsMono.tar.xz |
                tar -xJ -C "$fonts" || die "fetching the Nerd Font failed"
            fc-cache -f "$fonts" >/dev/null
            echo "installed into $fonts"
        fi
    fi

    # ------------------------------------------------------------ install
    # Hyprland first: install.sh checks for it, and fanshell builds against
    # none of it but runs on it.
    [ -x "$hypr/tools/build-hyprland" ] || [ "$dry_run" = 1 ] ||
        die "$hypr/tools/build-hyprland is missing"
    if [ -x "$hypr/tools/build-hyprland" ]; then
        FANSHELL_QS_DIR="$wm_dir/fanshell-qs" "$hypr/tools/build-hyprland" $flag
    else
        printf '  would: %s/tools/build-hyprland\n' "$hypr"
    fi

    if [ -x "$hypr/install.sh" ]; then
        # ASSUME_YES: installs what is missing without asking again.
        FANSHELL_QS_DIR="$wm_dir/fanshell-qs" FANVID_QS_DIR="$wm_dir/fanvid-qs" \
            VIDEO_WALLPAPERS=$video ASSUME_YES=1 "$hypr/install.sh" $flag
    else
        printf '  would: %s/install.sh\n' "$hypr"
    fi

    # ------------------------------------------------------------ choices
    if [ "$selinux" = yes ]; then
        say "SELinux: off from the next boot"
        # The kernel argument is how Fedora turns it off now; the config
        # file says so too, for anyone reading it.
        run sudo grubby --update-kernel ALL --args selinux=0
        run sudo sed -i 's/^SELINUX=.*/SELINUX=disabled/' /etc/selinux/config
    fi

    if [ "$debrand" = yes ]; then
        say "hiding the Fedora name"
        # The boot menu, unless a boot failed or Shift is held; it lists
        # kernels by the distribution's name.
        run sudo grub2-editenv - set menu_auto_hide=1
        # The boot splash's logo: rhgb asks for it.
        run sudo grubby --update-kernel ALL --remove-args rhgb
        # The console login's banner names the distribution; this one says
        # the machine and the terminal. /etc/os-release stays: dnf and the
        # installers read it.
        if [ "$dry_run" = 1 ]; then
            printf '  would: write /etc/issue (the hostname and the tty)\n'
        else
            sudo rm -f /etc/issue
            printf '\\n \\l\n\n' | sudo tee /etc/issue >/dev/null
        fi
        echo "the boot menu, boot splash and console login no longer show it"
    fi

    say "done"
    cat <<EOF
Reboot, log in on the console, and start Hyprland with: start-hyprland
To update everything later, run this again: $hypr/bootstrap.sh
To update only Hyprland: $hypr/tools/build-hyprland
EOF
}

main "$@"; exit
