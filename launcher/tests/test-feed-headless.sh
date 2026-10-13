#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Headless test for ncz-screensaver-feed: the feed renders a REAL hack (off-screen,
# via each hack's NCZ_FEED_SOCKET mode) and streams frames to a mock lock that
# speaks the exact lock_framesrc wire protocol. No compositor and no GPU are
# required (software EGL is allowed here). Three scenarios: 1x 320x240, HiDPI
# 640x480 @ scale 2, and two outputs at once.
#
# usage: test-feed-headless.sh SRC_DIR BIN_DIR [HACK_ID]
#   SRC_DIR  repo root (has launcher/, assets/, config/)
#   BIN_DIR  directory of built hack binaries
set -u
SRC=${1:?usage: $0 SRC_DIR BIN_DIR [HACK_ID]}
BIN=${2:?usage: $0 SRC_DIR BIN_DIR [HACK_ID]}
HACK=${3:-blackhole_gles3}

if [ ! -x "$BIN/$HACK" ]; then
    echo "SKIP: $BIN/$HACK not built"
    exit 77
fi

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/xdg" "$T/cfg" "$T/schemas"
chmod 700 "$T/xdg"
cp "$SRC/config/dev.ncz.screensaver.gschema.xml" "$T/schemas/"
GLIB_BIN=$(command -v glib-compile-schemas || echo /usr/bin/glib-compile-schemas)
"$GLIB_BIN" "$T/schemas"

export XDG_RUNTIME_DIR="$T/xdg"
export XDG_CONFIG_HOME="$T/cfg"
export GSETTINGS_BACKEND=keyfile
export GSETTINGS_SCHEMA_DIR="$T/schemas"
# Software GPU is fine here; this only tests the producer path. The guard's
# hardware probe is pointed at an empty root because a build sandbox has no
# /dev/dri (the real host leaves NCZ_FEED_GUARD_ROOT unset).
export NCZ_ALLOW_SOFTWARE=1
export LIBGL_ALWAYS_SOFTWARE=1
mkdir -p "$T/empty"
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
CASE=0
run_case() { # $1 name, $2 frames, rest = extra mock args
    local name="$1" frames="$2"; shift 2
    CASE=$((CASE+1))
    local sock="$T/xdg/lock-$CASE.sock"
    echo "== $name =="
    rm -rf "$XDG_RUNTIME_DIR/ncz-screensaver"
    python3 "$SRC/launcher/tests/feed-mock-lock.py" \
        --socket "$sock" --frames "$frames" \
        --result "$T/mock-$CASE.json" --timeout "${NCZ_TEST_TIMEOUT:-90}" "$@" &
    local mock=$!
    for _ in $(seq 1 100); do [ -S "$sock" ] && break; sleep 0.05; done
    SINGULARITY_LOCK_RENDERER_SOCKET="$sock" \
        timeout "${NCZ_TEST_TIMEOUT:-100}" python3 "$SRC/launcher/ncz-screensaver-feed" >"$T/feed-$CASE.out" 2>&1
    local frc=$?
    wait "$mock"; local mrc=$?
    tail -6 "$T/feed-$CASE.out" | sed 's/^/  feed: /'
    cat "$T/mock-$CASE.json" 2>/dev/null; echo
    if [ "$frc" -ne 0 ]; then echo "FAIL: $name: feed exit $frc"; FAIL=1; return; fi
    if [ "$mrc" -ne 0 ]; then echo "FAIL: $name: mock-lock rejected frames"; FAIL=1; return; fi
    echo "PASS: $name"
}

run_case "single 320x240" 8
run_case "hidpi scale 2 (640x480)" 8 --outputs "640x480@2"
run_case "two outputs" 12 --outputs "320x240@1,200x150@1"

if [ "$FAIL" -ne 0 ]; then echo "RESULT: FAIL"; exit 1; fi
echo "RESULT: PASS: ncz-screensaver-feed streamed real frames in all scenarios"
exit 0
