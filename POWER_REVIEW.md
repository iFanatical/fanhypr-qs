# fanhypr-qs Power and Idle CPU Review

Date: 2026-08-27

## Implementation status

The findings below record the pre-fix state observed during the review. They
were addressed on 2026-08-27: helper process groups now clean up descendants,
System statistics/weather/Bluetooth polling is visibility-bound, network work
is event-driven with a lightweight 30-second signal sample, unchanged network
state is suppressed, notification expiry is deadline-driven, and the
second-free clock realigns to each exact minute boundary.

## Context

This review was prompted by unexpectedly high laptop battery drain and a CPU
temperature of approximately 50-55 C while the desktop appeared idle.

The installed `/usr/local/bin/fanhypr-qs-shell` and the repository build at
`shell/fanhypr-qs-shell` had identical SHA-256 hashes during the review, so the
source findings below apply to the running binary.

## Runtime observations

- The battery was discharging at approximately 10.7-13.6 W.
- Battery health was approximately 70% of design capacity: 37.3 Wh full versus
  53.0 Wh design.
- CPU package temperature was approximately 58-62 C during diagnosis. The
  diagnostic Codex session contributed load, but the shell's CPU use remained
  independently measurable.
- A quiet 15-second process delta measured:
  - `fanhypr-qs-shell`: approximately 4.1% of one CPU core.
  - Hyprland: approximately 6.1% of one CPU core.
  - The diagnostic Codex and terminal were additional temporary consumers.
- Multiple `fanhypr-qs-net watch` processes and two `nmcli monitor` processes
  were present. At least one network watcher was orphaned from an earlier shell
  instance.
- `fanhypr-qs-sysinfo` was observed launching every three seconds even though
  the System popup was closed.

The machine also had unrelated system-level power concerns: panel self-refresh
was disabled with the kernel argument `i915.enable_psr=0`, no laptop power
profile service was active, and Intel P-state used the `balance_performance`
energy preference. These amplify the impact but do not invalidate the shell
findings.

## Findings

### 1. High: watcher cleanup does not clean up subprocess trees

Relevant code:

- `shell/procutil.cpp`, around lines 18-48
- `shell/procutil.h`, around lines 5-9
- `scripts/fanhypr-qs-net`, around lines 133-147

`ProcUtil::reapWithParent()` applies `PR_SET_PDEATHSIG` to the direct
`QProcess` child. The setting is not inherited by grandchildren.

The network watcher creates a shell pipeline:

```sh
nmcli monitor 2>/dev/null | while IFS= read -r _line; do
    status
    printf '\n'
done
```

If the main shell exits, its immediate script child may receive `SIGTERM`, but
the pipeline processes can survive. The runtime process tree confirmed that
this is happening: duplicate `nmcli monitor` instances and orphaned network
watchers were present.

This contradicts the current `procutil.h` comment claiming that long-running
watchers cannot outlive the bar.

Recommended remediation:

1. Start every tracked helper in its own process group or session.
2. On shutdown, signal the entire process group rather than only the immediate
   `QProcess` PID.
3. Ensure the process-group setup and cleanup cannot accidentally target the
   shell's own group.
4. Prefer eliminating persistent shell pipelines. A native NetworkManager
   D-Bus subscription would avoid this process-tree problem entirely.
5. Add a restart test that launches the shell repeatedly and asserts that only
   one network watcher and one `nmcli monitor` remain.

### 2. High: hidden System popup performs continuous polling

Relevant code:

- `shell/system.cpp`, around lines 264-315
- `scripts/fanhypr-qs-sysinfo`, lines 9-68
- `shell/bluetooth.cpp`, around lines 20-27
- `scripts/fanhypr-qs-bt`, around lines 17-46

`SystemWidget` eagerly constructs `SystemPopup` at startup. The popup
constructor immediately starts a three-second timer for
`fanhypr-qs-sysinfo`, whether or not the popup is visible or has ever been
opened.

Every sysinfo invocation forks a shell and utilities, intentionally remains
alive for a 200 ms CPU sample, calls `free` and `awk`, and walks hardware-monitor
sysfs entries.

Eager System popup construction also initializes Bluetooth, clipboard,
weather, VPN, and audio facilities. The Bluetooth watcher polls every three
seconds and may invoke `bluetoothctl` repeatedly for the controller and every
known device.

Recommended remediation:

1. Construct expensive popup content lazily on first open.
2. Start sysinfo and Bluetooth polling when the popup becomes visible.
3. Stop those timers when it closes.
4. Perform one immediate refresh on open so the UI does not show stale data.
5. Longer term, use native D-Bus signals for Bluetooth state rather than
   polling `bluetoothctl`.

### 3. High: network monitoring is redundant and subprocess-heavy

Relevant code:

- `shell/network.cpp`, around lines 34-60
- `scripts/fanhypr-qs-net`, around lines 20-98 and 131-147

`NetworkState` runs both a persistent event-driven `nmcli monitor` watcher and
a full status poll every five seconds.

Each full NetworkManager status pass launches multiple commands. It repeatedly
tests the backend with `nmcli general status`, queries route state, queries
NetworkManager properties, and performs two separate Wi-Fi-list calls to obtain
signal and security. Every `nmcli monitor` event also triggers the full status
routine, so a burst of NetworkManager events creates a burst of processes.

The leaked watcher described in finding 1 multiplies this work.

Recommended remediation:

1. Use event-driven monitoring for connection changes.
2. If signal polling remains necessary, poll only signal strength and use a
   substantially slower interval.
3. Retrieve active signal and security with one `nmcli` call.
4. Avoid re-running backend detection during every status collection.
5. Prefer a native NetworkManager D-Bus client for properties and signals.

### 4. Medium: notification service wakes ten times per second forever

Relevant code:

- `shell/notification.cpp`, around lines 583-592 and 820-837

`NotificationService` starts a permanent 100 ms timer in its constructor. It
runs even when notification history is empty and no visible notification has an
expiration deadline.

The callback is not computationally large, but it guarantees ten application
wakeups per second on an otherwise idle desktop.

Recommended remediation:

1. Replace the repeating timer with a single-shot timer.
2. Schedule it for the nearest visible notification expiration.
3. Reschedule when notifications are added, replaced, dismissed, hovered, or
   unhovered.
4. Leave the timer stopped when nothing can expire.

### 5. Medium: clock forces a precise repaint every second

Relevant code:

- `shell/calendar.cpp`, around lines 12-35

The clock displays seconds and uses a one-second `Qt::PreciseTimer`. This forces
a widget repaint every second and can wake the Wayland compositor and GPU for a
layer-shell surface update.

This is a plausible contributor to the simultaneous steady CPU use seen in
`fanhypr-qs-shell` and Hyprland.

Recommended remediation:

1. Remove seconds from the default clock display.
2. Use a coarse or very-coarse timer aligned to the next minute boundary.
3. If seconds are desired, make them an explicit configuration option.

### 6. Medium: unchanged network state still propagates updates

Relevant code:

- `shell/network.cpp`, around lines 62-103
- `shell/network.cpp`, around lines 179-197 and 495-553

`NetworkState::parseStatus()` mutates the live state and unconditionally emits
`changed()`, even when every parsed value is identical to the previous sample.
This wakes the bar widget and the already-constructed hidden network popup every
five seconds.

Individual widget setters suppress some redundant painting, but parsing, Qt
signal delivery, string construction, popup synchronization, and layout checks
still occur.

Recommended remediation:

1. Parse into a temporary state object.
2. Compare it with the current state.
3. Commit and emit `changed()` only when relevant values differ.
4. Consider separate signals for bar-visible state and detailed popup state.

## Suggested implementation order

1. Correct process-tree ownership and clean up existing watcher leaks.
2. Make System popup polling lazy and visibility-bound.
3. Simplify network monitoring and remove redundant subprocess calls.
4. Replace the notification polling timer with deadline scheduling.
5. Change the clock to minute-resolution coarse updates by default.
6. Add state comparisons before emitting broad `changed()` signals.

## Suggested verification

After each change, measure from a fresh login with the desktop untouched:

```sh
ps -eo pid,ppid,pcpu,etimes,comm,args --sort=-pcpu | head -30
pgrep -af 'fanhypr-qs|nmcli monitor|bluetoothctl'
upower -i "$(upower -e | sed -n '/battery/{p;q;}')"
```

Acceptance targets:

- Exactly one shell instance, one intentional network watcher, and no orphaned
  watcher descendants after repeated shell restarts.
- No `fanhypr-qs-sysinfo` or Bluetooth status polling while the System popup is
  closed.
- No periodic notification wakeup when there are no expiring notifications.
- No once-per-second clock repaint under the default configuration.
- `fanhypr-qs-shell` close to 0% CPU over a quiet 30-60 second sample, aside
  from brief event-driven updates.

## Scope

This was a read-only review. No source or configuration changes were made while
collecting these findings.
