#!/usr/bin/env bash
# Integration test for the multi-window / single-instance arbitration in
# main.cpp. Everything here needs real processes and a real unix socket, so it
# lives as a script rather than a QtTest case.
#
# Skips (exit 77) when there is no Wayland session, since HyprFM refuses to
# start without one — that is the case on CI runners.
set -u

HYPRFM=${1:?usage: tst_instance_launch.sh /path/to/hyprfm}

[ -n "${WAYLAND_DISPLAY:-}" ] || { echo "SKIP: no WAYLAND_DISPLAY"; exit 77; }

SANDBOX=$(mktemp -d)
cleanup() {
    for pid in "${PIDS[@]:-}"; do kill "$pid" 2>/dev/null; done
    sleep 0.5
    for pid in "${PIDS[@]:-}"; do kill -9 "$pid" 2>/dev/null; done
    rm -rf "$SANDBOX"
}
PIDS=()
trap cleanup EXIT

# QLocalServer names the instance socket per-uid inside QDir::tempPath(), which
# follows TMPDIR. Pointing TMPDIR at the sandbox keeps any HyprFM the developer
# is running on its own socket, so it never takes part in these handoffs.
export TMPDIR="$SANDBOX/tmp"
mkdir -p "$TMPDIR"

export HOME="$SANDBOX/home"
# HyprFM follows XDG_CONFIG_HOME, which a desktop session often sets to the
# real ~/.config: keep it inside the sandbox too.
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_CACHE_HOME="$HOME/.cache"
mkdir -p "$HOME/.config/hyprfm"
SESSION="$HOME/.config/hyprfm/session.json"
SOCKET="$TMPDIR/hyprfm-$(id -u)"

failures=0

# Launch in the background, publish the pid in SPAWNED, and remember it so
# cleanup can reap it. Deliberately not echoing the pid: callers would need
# command substitution, whose subshell would discard the PIDS append and leak
# every window this test starts.
spawn() {
    "$HYPRFM" "$@" >/dev/null 2>&1 &
    SPAWNED=$!
    PIDS+=("$SPAWNED")
    sleep 3
}

yesno() { kill -0 "$1" 2>/dev/null && echo yes || echo no; }

# Poll instead of sleeping a fixed amount: a cold start right after a relink
# can take several seconds, and a hardcoded wait turns that into a flake.
wait_for() {
    local deadline=$((SECONDS + $1)); shift
    while [ "$SECONDS" -lt "$deadline" ]; do
        "$@" >/dev/null 2>&1 && return 0
        sleep 0.2
    done
    return 1
}

socket_up() { [ -S "$SOCKET" ]; }
tabs() { grep -o '"path"' "$SESSION" 2>/dev/null | wc -l; }
more_tabs_than() { [ "$(tabs)" -gt "$1" ]; }

check() {
    if [ "$2" = "$3" ]; then
        echo "PASS: $1"
    else
        echo "FAIL: $1 (expected '$3', got '$2')"
        failures=$((failures + 1))
    fi
}

# --- 1. first launch becomes primary and owns the socket -------------------
spawn; primary=$SPAWNED
check "first launch stays alive" "$(yesno "$primary")" "yes"
wait_for 20 socket_up
check "first launch owns the IPC socket" "$(socket_up && echo yes || echo no)" "yes"

# --- 2. path handoff: no new process, primary gains a tab ------------------
# The session file only appears once something changes, hence the missing-file
# tolerance on the "before" count.
tabs_before=$(tabs)
"$HYPRFM" /etc >/dev/null 2>&1
check "hyprfm <path> exits 0 while an instance runs" "$?" "0"
wait_for 10 more_tabs_than "$tabs_before"
check "handoff added a tab to the primary" \
      "$(more_tabs_than "$tabs_before" && echo yes || echo no)" "yes"

# --- 3. bare relaunch opens an independent window --------------------------
spawn; second=$SPAWNED
check "bare relaunch stays alive as a second window" "$(yesno "$second")" "yes"

# --- 4. secondary windows never write the shared session -------------------
# Checked by content, not mtime: the primary legitimately saves whenever its
# geometry changes, and a tiling compositor resizes it when a third window
# appears. What must never happen is the secondary's own tab landing in the
# file, so give it a path the primary does not have.
spawn -n /usr; third=$SPAWNED
check "--new-window <path> opens its own window" "$(yesno "$third")" "yes"
kill "$third"; sleep 1
check "secondary window's tab is not in session.json" \
      "$(grep -c '"/usr"' "$SESSION")" "0"

# --- 5. stale socket from a crashed instance is recovered ------------------
kill -9 "$primary" "$second" 2>/dev/null
wait "$primary" "$second" 2>/dev/null   # reap quietly, no "Killed" job notices
sleep 1
check "crash leaves a stale socket behind" "$(socket_up && echo yes || echo no)" "yes"
spawn; revived=$SPAWNED
wait_for 20 socket_up
check "launch after crash starts despite the stale socket" "$(yesno "$revived")" "yes"
"$HYPRFM" /etc >/dev/null 2>&1
check "revived instance answers handoffs" "$?" "0"

[ "$failures" -eq 0 ] || { echo "$failures check(s) failed"; exit 1; }
echo "all checks passed"
