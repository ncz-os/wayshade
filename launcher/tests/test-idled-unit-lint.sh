#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Lint the shipped ncz-screensaver-idled user unit and assert the login/logout
# wiring this fix depends on: the unit is pulled and stopped by the real
# Singularity desktop session target (as well as graphical-session.target),
# only EX_CONFIG (78) prevents a restart, and the restart policy does not burn
# the start-limit while a greetd restart brings a new session up (Orion O6N
# 2026-10-05).
#
# usage: test-idled-unit-lint.sh IDLED_BINARY
set -eu
BIN=${1:?usage: $0 IDLED_BINARY}
# meson passes build targets relative to the build dir; systemd-analyze needs an
# absolute ExecStart.
case "$BIN" in
    /*) : ;;
    *) BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN") ;;
esac
[ -x "$BIN" ] || { echo "FAIL test binary not executable: $BIN" >&2; exit 1; }
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
UNIT="$ROOT/launcher/ncz-screensaver-idled.service"

check() { # description pattern
    if grep -q -- "$2" "$UNIT"; then
        echo "PASS unit $1"
    else
        echo "FAIL unit $1 (missing: $2)" >&2
        exit 1
    fi
}

# Lifetime is bound to the session target that the Singularity launcher starts
# at login and stops at logout, so a new login re-pulls the unit even if it was
# failed before.
check "WantedBy includes singularity-session.target" '^WantedBy=.*singularity-session.target'
check "PartOf includes singularity-session.target" '^PartOf=.*singularity-session.target'
check "still works under graphical-session.target" '^WantedBy=.*graphical-session.target'
# 78 stays a permanent failure; 75 must NOT be listed here.
check "RestartPreventExitStatus is 78" '^RestartPreventExitStatus=78$'
if grep -qE '^RestartPreventExitStatus=.*\b75\b' "$UNIT"; then
    echo "FAIL EX_TEMPFAIL (75) must not be in RestartPreventExitStatus" >&2
    exit 1
fi
# A greetd restart can trigger many fast failed starts before the login imports
# WAYLAND_DISPLAY; the default start-limit would latch the unit `failed`.
check "Restart=on-failure" '^Restart=on-failure$'
check "start-limit disabled during session bring-up" '^StartLimitIntervalSec=0$'

if ! command -v systemd-analyze >/dev/null 2>&1; then
    echo "SKIP systemd-analyze not available (unit pattern checks still ran)"
    exit 0
fi

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
# Verify our directives, not the install-time path, by pointing ExecStart at the
# freshly built binary.
sed "s#^ExecStart=.*#ExecStart=$BIN#" "$UNIT" >"$T/idled.service"
out=$(systemd-analyze verify "$T/idled.service" 2>&1) && rc=0 || rc=$?
if [ "$rc" -ne 0 ]; then
    echo "FAIL systemd-analyze verify ($rc):" >&2
    echo "$out" >&2
    exit 1
fi
if printf '%s\n' "$out" | grep -qiE 'unknown (key|lvalue)'; then
    echo "FAIL systemd-analyze verify flagged an unknown directive:" >&2
    echo "$out" >&2
    exit 1
fi
echo "PASS systemd-analyze verify"
exit 0
