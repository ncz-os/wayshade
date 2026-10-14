#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Exit-status mapping for ncz-screensaver-idled.
#
# Regression targets:
#   * O6N 2026-10-04: after a logout the unit restarted with no
#     WAYLAND_DISPLAY; the daemon exited 78 (EX_CONFIG), which the unit lists in
#     RestartPreventExitStatus=, so the idle timer sat `failed` across logins.
#   * O6N 2026-10-05: after `systemctl restart greetd` the auto-restart raced
#     the new session's environment import and connected to the login greeter,
#     a compositor without ext-idle-notify-v1, and latched failed 78 again.
#
# Contract now:
#   75 (EX_TEMPFAIL): no usable session yet; systemd must keep auto-restarting.
#                     Covers a missing display, connect failures, and a
#                     compositor that does not (yet) advertise
#                     ext-idle-notify-v1.
#   78 (EX_CONFIG):   permanent, non-retriable misconfiguration only: the
#                     settings schema is missing.
#
# usage: test-idled-exit-status.sh IDLED_BINARY NO_IDLE_COMPOSITOR
set -eu
BIN=${1:?usage: $0 IDLED_BINARY NO_IDLE_COMPOSITOR}
COMPOSITOR=${2:?usage: $0 IDLED_BINARY NO_IDLE_COMPOSITOR}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/schemas" "$T/cfg" "$T/run" "$T/run-wait" "$T/noschema"
chmod 700 "$T/run" "$T/run-wait"
cp "$ROOT/config/dev.ncz.screensaver.gschema.xml" "$T/schemas/"
glib-compile-schemas "$T/schemas"
export GSETTINGS_BACKEND=keyfile GSETTINGS_SCHEMA_DIR="$T/schemas" XDG_CONFIG_HOME="$T/cfg"
export XDG_RUNTIME_DIR="$T/run"
fail=0

# 1) No Wayland session at all: transient, never EX_CONFIG.
rc=0
env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET NCZ_IDLED_SESSION_WAIT_MS=0 \
    "$BIN" >/dev/null 2>&1 || rc=$?
if [ "$rc" -eq 75 ]; then
    echo "PASS no session exits 75 (EX_TEMPFAIL)"
else
    echo "FAIL no session exit code $rc (want 75; 78 would latch the unit failed)" >&2
    fail=1
fi

# 2) A compositor that advertises wl_seat but not ext-idle-notify-v1 is exactly
#    what a login greeter looks like after a greetd restart: retriable, not a
#    permanent misconfiguration.
"$COMPOSITOR" >"$T/socket" 2>"$T/compositor.log" &
compositor_pid=$!
i=0
while [ ! -s "$T/socket" ] && [ "$i" -lt 100 ]; do
    sleep 0.05
    i=$((i + 1))
done
if [ ! -s "$T/socket" ]; then
    echo "FAIL fake compositor did not report a socket" >&2
    cat "$T/compositor.log" >&2 || true
    fail=1
else
    rc=0
    env NCZ_IDLED_SESSION_WAIT_MS=0 WAYLAND_DISPLAY="$(cat "$T/socket")" \
        "$BIN" >/dev/null 2>&1 || rc=$?
    if [ "$rc" -eq 75 ]; then
        echo "PASS compositor without ext-idle-notify-v1 exits 75 (retriable)"
    elif [ "$rc" -eq 78 ]; then
        echo "FAIL compositor without ext-idle-notify-v1 exits 78; a greeter would latch failed" >&2
        fail=1
    else
        echo "FAIL missing-protocol exit code $rc (want 75)" >&2
        fail=1
    fi
fi
kill "$compositor_pid" 2>/dev/null || true
wait "$compositor_pid" 2>/dev/null || true

# 3) A missing settings schema is the one permanent misconfiguration: 78.
rc=0
env GSETTINGS_SCHEMA_DIR="$T/noschema" XDG_DATA_DIRS="$T/noschema" \
    "$BIN" >/dev/null 2>&1 || rc=$?
if [ "$rc" -eq 78 ]; then
    echo "PASS missing schema exits 78 (EX_CONFIG)"
else
    echo "FAIL missing schema exit code $rc (want 78)" >&2
    fail=1
fi

# 4) The 2026-10-05 race in full: the daemon must wait for a session instead of
#    exiting immediately, and must be able to reach the default wayland-0
#    socket even though WAYLAND_DISPLAY has not been imported yet. Run it with
#    no session, bring the compositor up 1.5s later, then prove it waited (not
#    an instant exit) and did reach the greeter over the default socket.
export XDG_RUNTIME_DIR="$T/run-wait"
start=$(date +%s)
env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET NCZ_IDLED_SESSION_WAIT_MS=4000 \
    "$BIN" --verbose >/dev/null 2>"$T/wait.log" &
idled_pid=$!
sleep 1.5
"$COMPOSITOR" >"$T/socket-wait" 2>"$T/compositor-wait.log" &
compositor_wait_pid=$!
i=0
while [ ! -s "$T/socket-wait" ] && [ "$i" -lt 100 ]; do
    sleep 0.05
    i=$((i + 1))
done
rc=0
wait "$idled_pid" || rc=$?
elapsed=$(( $(date +%s) - start ))
kill "$compositor_wait_pid" 2>/dev/null || true
wait "$compositor_wait_pid" 2>/dev/null || true
if [ "$rc" -eq 75 ] && [ "$elapsed" -ge 3 ]; then
    echo "PASS no session: waited ${elapsed}s before exiting 75 (bounded wait)"
else
    echo "FAIL bounded wait: rc=$rc elapsed=${elapsed}s (want 75 after >=3s)" >&2
    fail=1
fi
if grep -q "ext_idle_notifier_v1 not advertised yet" "$T/wait.log"; then
    echo "PASS reached the compositor over the default socket without WAYLAND_DISPLAY"
else
    echo "FAIL did not reach the compositor without WAYLAND_DISPLAY" >&2
    sed 's/^/    /' "$T/wait.log" >&2 || true
    fail=1
fi

exit $fail
