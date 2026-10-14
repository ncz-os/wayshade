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
# Delegated mode (the NCZ lock renderer is configured): the SHELL owns the idle
# lock (post-install/64-ncz-lock-saver-plan.sh sets
# dev.sinty.lockscreen:idle-delay = hack-idle-delay), so this daemon must arm NO
# lock timer of its own. Two lock clients started at the same instant otherwise
# race the lock's renderer socket, the loser's unlink() leaves the survivor with
# no frame source, and the screensaver never renders (build-host 2026-10-13).
# Suspend locking stays with the daemon (it covers system suspends the shell
# does not), so lock_on_suspend remains as the schema has it.
gsettings set $S mode one
gsettings set $S hack-idle-delay 300
gsettings set $S lock-enabled true
gsettings set $S lock-delay 60
gsettings set $S display-off-delay 0
got=$(NCZ_LOCKSAVER_ENABLE=1 "$BIN" --dry-run)
if [ "$got" = '{"saver":null,"lock":null,"dpms":null,"lock_on_suspend":true,"delegated":true}' ]; then
    echo "PASS delegated: no lock timer (shell is the single idle-lock owner)"
else
    echo "FAIL delegated: got $got"; fail=1
fi
# The Singularity "Lock on Suspend" toggle (dev.sinty.lockscreen lock-on-suspend)
# must be honoured too: with it off the daemon never locks on suspend, in both
# the normal and the delegated plan. A fixture copy of that one key stands in for
# the shell's schema.
cat > "$T/schemas/dev.sinty.lockscreen.gschema.xml" <<'XML'
<schemalist>
  <schema id="dev.sinty.lockscreen" path="/dev/sinty/lockscreen/">
    <key name="lock-on-suspend" type="b"><default>true</default></key>
  </schema>
</schemalist>
XML
glib-compile-schemas "$T/schemas"
gsettings set dev.sinty.lockscreen lock-on-suspend false
check "shell lock-on-suspend off: no suspend lock" '{"saver":300,"lock":360,"dpms":null,"lock_on_suspend":false}'
got=$(NCZ_LOCKSAVER_ENABLE=1 "$BIN" --dry-run)
if [ "$got" = '{"saver":null,"lock":null,"dpms":null,"lock_on_suspend":false,"delegated":true}' ]; then
    echo "PASS delegated + shell lock-on-suspend off: no suspend lock"
else
    echo "FAIL delegated + shell lock-on-suspend off: got $got"; fail=1
fi
gsettings set dev.sinty.lockscreen lock-on-suspend true
check "shell lock-on-suspend on: suspend lock kept" '{"saver":300,"lock":360,"dpms":null,"lock_on_suspend":true}'
# When the shell holds its own sleep-lock inhibitor (who=Singularity, delay,
# what includes sleep, same uid) the daemon leaves the suspend lock to the shell.
cls() { printf '%s\n' "$@" | "$BIN" --classify-inhibitors; }
[ "$(cls 'sleep Singularity delay self')" = shell ] && echo "PASS shell sleep inhibitor detected" || { echo "FAIL shell sleep inhibitor not detected"; fail=1; }
[ "$(cls 'shutdown:sleep Singularity delay self')" = shell ] && echo "PASS combined what list detected" || { echo "FAIL combined what list"; fail=1; }
[ "$(cls 'sleep NCZ_screensaver delay self' 'idle Singularity block self')" = none ] && echo "PASS other inhibitors ignored" || { echo "FAIL other inhibitors misread"; fail=1; }
[ "$(cls 'sleep Singularity block self')" = none ] && echo "PASS block-mode inhibitor ignored" || { echo "FAIL block-mode inhibitor misread"; fail=1; }
[ "$(cls 'sleep Singularity delay 4242')" = none ] && echo "PASS another user's shell ignored" || { echo "FAIL another user's shell counted"; fail=1; }
# A missing session is transient: EX_TEMPFAIL (75), not EX_CONFIG (78), so the
# user unit auto-restarts instead of staying failed after a logout. The startup
# wait is disabled so this only checks the exit code; test-idled-exit-status.sh
# exercises the wait itself.
rc=0
env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET NCZ_IDLED_SESSION_WAIT_MS=0 \
    "$BIN" >/dev/null 2>&1 || rc=$?
if [ "$rc" -eq 75 ]; then echo "PASS no display exits 75 (EX_TEMPFAIL)"; else echo "FAIL no display exit code $rc (want 75)"; fail=1; fi
exit $fail
