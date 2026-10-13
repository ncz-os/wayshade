#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Regression test for the lock-hosted saver warm-up path (the Mali/CIX failure
# observed on Orion O6N): a hack whose first frame is late - shader compile / EGL
# init on a slow GPU can take longer than the lock's 3 s frame watchdog - must
# NOT starve the lock. The feed keeps the lock's watchdog fed with a black
# keepalive frame (FALLBACK_AFTER) and, when the real frame finally lands,
# forwards it. Reproduces "frame source failed (frame watchdog)" if the feed
# ever stops advertising a frame inside the watchdog budget.
#
# Uses a FAKE hack (launcher/tests/fake-slow-child.py) that speaks the child
# protocol, so no GPU/no compositor is required.
#
# usage: test-feed-slow-child.sh SRC_DIR [BIN_DIR]
set -u
SRC=${1:?usage: $0 SRC_DIR [BIN_DIR]}
BIN=${2:-$SRC/launcher/tests}

[ -n "${TMPDIR:-}" ] && mkdir -p "$TMPDIR" 2>/dev/null
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/xdg" "$T/cfg" "$T/schemas" "$T/empty" "$T/bin"; chmod 700 "$T/xdg"
cp "$SRC/config/dev.ncz.screensaver.gschema.xml" "$T/schemas/"
GLIB_BIN=$(command -v glib-compile-schemas || echo /usr/bin/glib-compile-schemas)
"$GLIB_BIN" "$T/schemas"

# The feed resolves the hack binary via NCZ_SCREENSAVER_DIRS; install the fake
# under the shipped id so it is picked up exactly like the real hack.
cp "$BIN/fake-slow-child.py" "$T/bin/blackhole_gles3"
chmod +x "$T/bin/blackhole_gles3"

export XDG_RUNTIME_DIR="$T/xdg"
export XDG_CONFIG_HOME="$T/cfg"
export GSETTINGS_BACKEND=keyfile
export GSETTINGS_SCHEMA_DIR="$T/schemas"
export NCZ_FEED_GUARD_ROOT="$T/empty"
export NCZ_SCREENSAVER_DIRS="$T/bin"
export NCZ_SCREENSAVER_CATALOG="$SRC/assets/screensaver-chooser/hacks.tsv"
export NCZ_SCREENSAVER_LAUNCHER="$SRC/launcher/ncz-screensaver"

S=dev.ncz.screensaver
gsettings set $S mode one
gsettings set $S hack-id blackhole_gles3
gsettings set $S hack-idle-delay 300
gsettings set $S verify-render false

# The lock's ext-session-lock frame watchdog is 3 s; keep a small margin.
WATCHDOG_BUDGET=2.6
FAIL=0

run_case() { # $1 name, $2 mock-extra, rest = CHILD/feed env assignments
    local name="$1" mock_extra="$2"; shift 2
    echo "== $name =="
    local sock="$T/xdg/lock-$name.sock"
    rm -rf "$XDG_RUNTIME_DIR/ncz-screensaver" "$XDG_RUNTIME_DIR/ncz-saver-feed"
    # shellcheck disable=SC2086
    python3 "$SRC/launcher/tests/feed-mock-lock.py" \
        --socket "$sock" --width 320 --height 240 $mock_extra \
        --result "$T/mock-$name.json" --timeout "${NCZ_TEST_TIMEOUT:-30}" &
    local mock=$!
    _deadline=$(( $(date +%s) + 10 )); while [ ! -S "$sock" ] && [ "$(date +%s)" -lt "$_deadline" ]; do sleep 0.05; done
    env "$@" NCZ_SAVER_FREEZE=never \
        SINGULARITY_LOCK_RENDERER_SOCKET="$sock" \
        timeout "${NCZ_TEST_TIMEOUT:-40}" python3 "$SRC/launcher/ncz-screensaver-feed" \
        >"$T/feed-$name.out" 2>&1
    local frc=$?
    wait "$mock"; local mrc=$?
    cat "$T/mock-$name.json" 2>/dev/null; echo
    if [ "$frc" -ne 0 ]; then echo "FAIL: $name: feed exit $frc"; FAIL=1; return; fi
    if [ "$mrc" -ne 0 ]; then echo "FAIL: $name: mock-lock rejected the stream"; FAIL=1; return; fi
    echo "PASS: $name"
}

# Case 1: child is silent forever (the failed-EGL case). The feed must keep the
# lock fed with black keepalives, none later than the watchdog budget.
run_case silent-child "--frames 4 --allow-static --require-first-within $WATCHDOG_BUDGET" \
    FAKE_CHILD_FRAMES=0

# Case 2: child warms up for 4 s (> the 3 s watchdog) then streams real frames.
# The keepalive covers the gap; real (distinct) frames then reach the lock.
run_case slow-then-live "--frames 6 --require-first-within $WATCHDOG_BUDGET" \
    FAKE_CHILD_DELAY=4 FAKE_CHILD_FRAMES=4

if [ "$FAIL" -ne 0 ]; then echo "RESULT: FAIL"; exit 1; fi
echo "RESULT: PASS: the feed keeps the lock fed inside the 3 s watchdog budget while the child warms up"
exit 0
