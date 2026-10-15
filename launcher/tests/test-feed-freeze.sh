#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Headless test for the lock-hosted saver POLICY knobs of ncz-screensaver-feed:
#   * the lock frame rate and the reduced render resolution are applied
#     (NCZ_SAVER_LOCK_FPS / NCZ_SAVER_RENDER_DIV appear in the feed's log);
#   * default (NCZ_SAVER_FREEZE unset = prompt) -> frames keep coming well past
#     the freeze-after time and the feed never sends HOLD: the idle saver keeps
#     animating; only the lock freezes it, when it raises the prompt;
#   * NCZ_SAVER_FREEZE=never  -> frames keep coming, no HOLD;
#   * NCZ_SAVER_FREEZE=always -> after NCZ_SAVER_FREEZE_AFTER_S the feed sends
#     LFS_HOLD and stops rendering (opt-in intro-then-freeze power saver).
# It runs a REAL hack off-screen (software EGL) and streams to a mock lock that
# speaks the lock_framesrc wire protocol. No compositor and no GPU required.
#
# usage: test-feed-freeze.sh SRC_DIR BIN_DIR [HACK_ID]
set -u
SRC=${1:?usage: $0 SRC_DIR BIN_DIR [HACK_ID]}
BIN=${2:?usage: $0 SRC_DIR BIN_DIR [HACK_ID]}
HACK=${3:-blackhole_gles3}

if [ ! -x "$BIN/$HACK" ]; then
    echo "SKIP: $BIN/$HACK not built"
    exit 77
fi

[ -n "${TMPDIR:-}" ] && mkdir -p "$TMPDIR" 2>/dev/null
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/xdg" "$T/cfg" "$T/schemas" "$T/empty"; chmod 700 "$T/xdg"
cp "$SRC/config/dev.ncz.screensaver.gschema.xml" "$T/schemas/"
GLIB_BIN=$(command -v glib-compile-schemas || echo /usr/bin/glib-compile-schemas)
"$GLIB_BIN" "$T/schemas"

export XDG_RUNTIME_DIR="$T/xdg"
export XDG_CONFIG_HOME="$T/cfg"
export GSETTINGS_BACKEND=keyfile
export GSETTINGS_SCHEMA_DIR="$T/schemas"
export NCZ_ALLOW_SOFTWARE=1
# The build host may run hot; pin the thermal governor so a hot pause cannot
# starve the test of frames (the governor has its own tests).
export NCZ_FEED_TEMP_MC=${NCZ_FEED_TEMP_MC:-40000} LIBGL_ALWAYS_SOFTWARE=1
export NCZ_FEED_GUARD_ROOT="$T/empty"
export NCZ_SCREENSAVER_DIRS="$BIN"
export NCZ_SCREENSAVER_CATALOG="$SRC/assets/screensaver-chooser/hacks.tsv"
export NCZ_SCREENSAVER_LAUNCHER="$SRC/launcher/ncz-screensaver"

S=dev.ncz.screensaver
gsettings set "$S" mode one
gsettings set "$S" hack-id "$HACK"
gsettings set "$S" hack-idle-delay 300
gsettings set "$S" verify-render false

FAIL=0

run_case() { # $1 name, $2 mock-extra, $3 expect_hold, rest = FEED ENV assignments
    local name="$1" mock_extra="$2" expect_hold="$3"; shift 3
    echo "== $name =="
    local sock="$T/xdg/lock-$name.sock"
    rm -rf "$XDG_RUNTIME_DIR/ncz-screensaver" "$XDG_RUNTIME_DIR/ncz-saver-feed"
    # shellcheck disable=SC2086
    python3 "$SRC/launcher/tests/feed-mock-lock.py" \
        --socket "$sock" --width 320 --height 240 $mock_extra \
        --result "$T/mock-$name.json" --timeout "${NCZ_TEST_TIMEOUT:-60}" &
    local mock=$!
    _deadline=$(( $(date +%s) + 10 )); while [ ! -S "$sock" ] && [ "$(date +%s)" -lt "$_deadline" ]; do sleep 0.05; done
    env "$@" \
        SINGULARITY_LOCK_RENDERER_SOCKET="$sock" \
        timeout "${NCZ_TEST_TIMEOUT:-90}" python3 "$SRC/launcher/ncz-screensaver-feed" \
        >"$T/feed-$name.out" 2>&1
    local frc=$?
    wait "$mock"; local mrc=$?
    echo "  feed log: $(grep -aE 'settings mode|HOLD after|freeze' "$T/feed-$name.out" 2>/dev/null | tr '\n' '|')"
    cat "$T/mock-$name.json" 2>/dev/null; echo
    if [ "$frc" -ne 0 ]; then echo "FAIL: $name: feed exit $frc"; FAIL=1; return; fi
    if [ "$mrc" -ne 0 ]; then echo "FAIL: $name: mock-lock rejected the stream"; FAIL=1; return; fi
    if ! grep -q "fps=8 render_div=2" "$T/feed-$name.out" 2>/dev/null; then
        echo "FAIL: $name: policy knobs not applied"; FAIL=1; return; fi
    if [ "$expect_hold" = yes ]; then
        grep -q "HOLD after" "$T/feed-$name.out" 2>/dev/null || { echo "FAIL: $name: no HOLD in the feed log"; FAIL=1; return; }
    else
        grep -q "HOLD after" "$T/feed-$name.out" 2>/dev/null && { echo "FAIL: $name: unexpected HOLD"; FAIL=1; return; }
    fi
    echo "PASS: $name"
}

# --frames must come through the mock-extra; split so both cases can override.
run_case pc-never-freeze "--frames 8" no \
    NCZ_SAVER_LOCK_FPS=8 NCZ_SAVER_RENDER_DIV=2 NCZ_SAVER_FREEZE=never
# Default policy: no timer. The freeze-after time is shortened to 1 s and the
# mock must still receive 40 frames (5 s at 8 fps) with no HOLD.
run_case default-no-timer-freeze "--frames 40" no \
    NCZ_SAVER_LOCK_FPS=8 NCZ_SAVER_RENDER_DIV=2 NCZ_SAVER_FREEZE= NCZ_SAVER_FREEZE_AFTER_S=1
run_case freeze-always-optin "--frames 9999 --hold" yes \
    NCZ_SAVER_LOCK_FPS=8 NCZ_SAVER_RENDER_DIV=2 NCZ_SAVER_FREEZE=always NCZ_SAVER_FREEZE_AFTER_S=1

if [ "$FAIL" -ne 0 ]; then echo "RESULT: FAIL"; exit 1; fi
echo "RESULT: PASS: lock fps/division applied; no timer freeze by default; HOLD only when opted in"
exit 0
