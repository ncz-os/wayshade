#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Headless regression test for the OLD-LOCK compatibility contract of
# ncz-screensaver-feed.
#
# Installed systems upgrade packages independently, so a 0.8.12 feed can meet a
# lock client built BEFORE LFS_HOLD existed. Such a lock understands HELLO and
# FRAME but treats HOLD as an unknown message type and faults/restarts the
# source -- so a feed with the default freeze=always policy would loop on
# "unknown message type" for ever.
#
# The feed must instead detect, from the WELCOME capability flag
# (LFS_FLAG_HOLD), that the lock does not understand HOLD, and then degrade
# safely: keep the low lock frame rate + the child-side thermal governor, send
# NO HOLD, log exactly once, and keep streaming frames.
#
#   * case hold-capable   -> WELCOME carries LFS_FLAG_HOLD: HOLD is sent;
#   * case pre-hold-lock  -> WELCOME carries flags=0: NO HOLD, frames keep
#                            coming, the feed logs the degrade exactly once and
#                            the mock (emulating the old lock) sees no fault.
#
# usage: test-feed-hold-compat.sh SRC_DIR BIN_DIR [HACK_ID]
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
export NCZ_ALLOW_SOFTWARE=1 LIBGL_ALWAYS_SOFTWARE=1
export NCZ_FEED_GUARD_ROOT="$T/empty"
export NCZ_SCREENSAVER_DIRS="$BIN"
export NCZ_SCREENSAVER_CATALOG="$SRC/assets/screensaver-chooser/hacks.tsv"
export NCZ_SCREENSAVER_LAUNCHER="$SRC/launcher/ncz-screensaver"

S=dev.ncz.screensaver
gsettings set $S mode one
gsettings set $S hack-id "$HACK"
gsettings set $S hack-idle-delay 300
gsettings set $S verify-render false

FAIL=0

run_case() { # $1 name, $2 mock-extra, $3 expect_hold(yes/no), rest = FEED ENV
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
    echo "  feed log: $(grep -aE 'settings mode|HOLD after|advertise HOLD' "$T/feed-$name.out" 2>/dev/null | tr '\n' '|')"
    cat "$T/mock-$name.json" 2>/dev/null; echo
    if [ "$frc" -ne 0 ]; then echo "FAIL: $name: feed exit $frc"; FAIL=1; return; fi
    if [ "$mrc" -ne 0 ]; then echo "FAIL: $name: mock-lock rejected the stream"; FAIL=1; return; fi
    if ! grep -q "fps=8 render_div=2" "$T/feed-$name.out" 2>/dev/null; then
        echo "FAIL: $name: low-fps/division policy not applied"; FAIL=1; return; fi
    if [ "$expect_hold" = yes ]; then
        grep -q "HOLD after" "$T/feed-$name.out" 2>/dev/null || { echo "FAIL: $name: no HOLD"; FAIL=1; return; }
    else
        grep -q "HOLD after" "$T/feed-$name.out" 2>/dev/null && { echo "FAIL: $name: unexpected HOLD"; FAIL=1; return; }
        grep -q "does not advertise HOLD" "$T/feed-$name.out" 2>/dev/null || { echo "FAIL: $name: no degrade log line"; FAIL=1; return; }
    fi
    echo "PASS: $name"
}

# Capability advertised (a HOLD-aware lock): the freeze is sent.
run_case hold-capable "--frames 9999 --hold" yes \
    NCZ_SAVER_LOCK_FPS=8 NCZ_SAVER_RENDER_DIV=2 NCZ_SAVER_FREEZE=always NCZ_SAVER_FREEZE_AFTER_S=1

# Pre-LFS_HOLD lock (flags=0): the feed must NOT freeze. The freeze timer (0.2s)
# expires well before the mock's 8 frames (~1s at 8 fps), so an unguarded feed
# would have sent HOLD first and the mock would record the fault.
run_case pre-hold-lock "--frames 8 --old-lock" no \
    NCZ_SAVER_LOCK_FPS=8 NCZ_SAVER_RENDER_DIV=2 NCZ_SAVER_FREEZE=always NCZ_SAVER_FREEZE_AFTER_S=0.2

if [ "$FAIL" -ne 0 ]; then echo "RESULT: FAIL"; exit 1; fi
echo "RESULT: PASS: HOLD only sent to a lock that advertised it; old lock degrades safely"
exit 0
