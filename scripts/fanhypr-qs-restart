#!/usr/bin/env bash
# Gracefully replace fanhypr-qs-shell, allowing aboutToQuit helper cleanup.
set -u

shell_pattern='(^|/)fanhypr-qs-shell( --no-duplicate)?$'

shell_running() {
    pgrep -f "$shell_pattern" >/dev/null 2>&1
}

wait_for_exit() {
    local attempts=${1:-40}
    local i
    for ((i = 0; i < attempts; ++i)); do
        shell_running || return 0
        sleep 0.05
    done
    return 1
}

# The normal path: QCoreApplication::quit() returns through the event loop,
# emits aboutToQuit, and terminates all tracked helper process groups.
fanhypr-qs-shell ipc call shell quit >/dev/null 2>&1 || true
wait_for_exit 40 || true

# Bounded fallback for a stale/unresponsive IPC server. These patterns match
# only the shell executable, never helper scripts or unrelated processes.
if shell_running; then
    pkill -TERM -f "$shell_pattern" >/dev/null 2>&1 || true
    wait_for_exit 20 || true
fi
if shell_running; then
    pkill -KILL -f "$shell_pattern" >/dev/null 2>&1 || true
    wait_for_exit 10 || true
fi

# Migration safeguard: clean only old network-watch group leaders already
# orphaned under PID 1. Remove this block once existing sessions have cycled;
# unlike a broad `pkill nmcli monitor`, it cannot touch the replacement tree
# or a user-run monitor. Each watcher owns its process group, so its nmcli
# descendant receives the same signal.
while read -r pgid; do
    [[ $pgid =~ ^[0-9]+$ ]] && ((pgid > 1)) || continue
    kill -TERM -- "-$pgid" >/dev/null 2>&1 || true
done < <(ps -eo ppid=,pgid=,args= | awk '
    $1 == 1 && $(NF - 1) ~ /(^|\/)fanhypr-qs-net$/ && $NF == "watch" {
        print $2
    }' | sort -u)

fanhypr-qs-shell --no-duplicate &
