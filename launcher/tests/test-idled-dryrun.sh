#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Checks ncz-screensaver-idled --dry-run plans against an isolated keyfile
# GSettings backend. usage: test-idled-dryrun.sh /path/to/ncz-screensaver-idled
set -eu
BIN=${1:?usage: $0 IDLED_BINARY}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/schemas" "$T/cfg"
cp "$ROOT/config/dev.ncz.screensaver.gschema.xml" "$T/schemas/"
glib-compile-schemas "$T/schemas"
export GSETTINGS_BACKEND=keyfile GSETTINGS_SCHEMA_DIR="$T/schemas" XDG_CONFIG_HOME="$T/cfg"
S=dev.ncz.screensaver
fail=0
check() { # name expected-json
    got=$("$BIN" --dry-run)
    if [ "$got" = "$2" ]; then echo "PASS $1"; else echo "FAIL $1: got $got expected $2"; fail=1; fi
}
# The shipped default for mode is 'one' since the Black Hole randomize merge;
# pin it to 'off' explicitly so this row tests the off-route regardless of the schema default.
gsettings set $S mode off
gsettings set $S hack-idle-delay 300
gsettings set $S lock-enabled true
gsettings set $S lock-delay 60
check "mode off: lock at idle delay" '{"saver":null,"lock":300,"dpms":null,"lock_on_suspend":true}'
gsettings set $S mode random
check "mode random: saver then lock" '{"saver":300,"lock":360,"dpms":null,"lock_on_suspend":true}'
gsettings set $S mode one
gsettings set $S display-off-delay 900
check "display off enabled" '{"saver":300,"lock":360,"dpms":900,"lock_on_suspend":true}'
gsettings set $S lock-enabled false
check "lock disabled" '{"saver":300,"lock":null,"dpms":900,"lock_on_suspend":false}'
gsettings set $S mode off
gsettings set $S display-off-delay 0
check "everything off" '{"saver":null,"lock":null,"dpms":null,"lock_on_suspend":false}'
# A missing session is transient: EX_TEMPFAIL (75), not EX_CONFIG (78), so the
# user unit auto-restarts instead of staying failed after a logout. The startup
# wait is disabled so this only checks the exit code; test-idled-exit-status.sh
# exercises the wait itself.
rc=0
env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET NCZ_IDLED_SESSION_WAIT_MS=0 \
    "$BIN" >/dev/null 2>&1 || rc=$?
if [ "$rc" -eq 75 ]; then echo "PASS no display exits 75 (EX_TEMPFAIL)"; else echo "FAIL no display exit code $rc (want 75)"; fail=1; fi
exit $fail
