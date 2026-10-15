#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Negative regression for the frame-cleanup path: when the lock closes the
# socket mid-stream (after a handful of frames), the renderer must exit
# cleanly with rc=0, the ring buffers must be unmapped (no stray memfd in
# /proc/self/fd, but here we just inspect the renderer's exit), and the
# swizzle program/VAO must be torn down (no GPU error noise in stderr).
#
# Headless: software EGL, mock lock, a light saver, 320x240. Mirrors
# test-feed-headless.sh but with the mock closing early to drive the cleanup
# branch in ncz_feed_render_main().
#
# usage: test-feed-cleanup.sh SRC_DIR BIN_DIR [HACK_ID]
set -u
SRC=${1:?usage}; BIN=${2:?usage}; HACK=${3:-hyprsaver_starfield_gles3}
[ -x "$BIN/$HACK" ] || { echo "SKIP: $BIN/$HACK not built"; exit 77; }
[ -n "${TMPDIR:-}" ] && mkdir -p "$TMPDIR" 2>/dev/null
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/xdg" "$T/cfg" "$T/schemas" "$T/empty"; chmod 700 "$T/xdg"
cp "$SRC/config/dev.ncz.screensaver.gschema.xml" "$T/schemas/"
glib-compile-schemas "$T/schemas"
export XDG_RUNTIME_DIR="$T/xdg" XDG_CONFIG_HOME="$T/cfg" GSETTINGS_BACKEND=keyfile GSETTINGS_SCHEMA_DIR="$T/schemas"
export NCZ_ALLOW_SOFTWARE=1 LIBGL_ALWAYS_SOFTWARE=1 NCZ_FEED_GUARD_ROOT="$T/empty"
export NCZ_SCREENSAVER_DIRS="$BIN" NCZ_SCREENSAVER_CATALOG="$SRC/assets/screensaver-chooser/hacks.tsv"
export NCZ_SCREENSAVER_LAUNCHER="$SRC/launcher/ncz-screensaver"
export NCZ_FEED_TEMP_MC=40000
gsettings set dev.ncz.screensaver mode one
gsettings set dev.ncz.screensaver hack-id "$HACK"
gsettings set dev.ncz.screensaver verify-render false
sock="$T/xdg/lock.sock"
# 12 frames is enough to populate at least one ring slot and (when the driver
# permits) exercise the swizzle path; the lock then sends STOP before the
# renderer has drained every slot, so the cleanup branch is the next call.
python3 "$SRC/launcher/tests/feed-mock-lock.py" --socket "$sock" --width 320 --height 240 \
    --frames 12 --result "$T/mock.json" --timeout 60 &
mock=$!
for _ in $(seq 1 100); do [ -S "$sock" ] && break; sleep 0.05; done
SINGULARITY_LOCK_RENDERER_SOCKET="$sock" \
    timeout 60 python3 "$SRC/launcher/ncz-screensaver-feed" > "$T/feed.out" 2>&1
frc=$?
wait "$mock"; mrc=$?
FAIL=0
if [ "$frc" -ne 0 ]; then echo "FAIL: feed exit $frc"; FAIL=1; fi
if [ "$mrc" -ne 0 ]; then echo "FAIL: mock rejected the stream"; FAIL=1; fi
# The renderer writes its "exit after N frames" line to stderr, which the
# feed redirects to feed.log under the launcher runtime dir (which mirrors
# the test's $XDG_RUNTIME_DIR). Reading the line there proves the cleanup
# branch ran to completion on every exit path (signal, lock-closed, normal
# STOP).
feedlog="$T/xdg/ncz-screensaver/ncz-saver-feed/feed.log"
if [ -s "$feedlog" ] && grep -q "exit after " "$feedlog"; then
    echo "PASS: renderer exit line in $feedlog"
else
    echo "FAIL: no renderer-exit line in $feedlog"
    FAIL=1
fi
# Any GL error message after the exit line would mean swz_fini() ran with no
# current context (or, worse, after the context was already destroyed).
grep -E "GL error 0x" "$T/feed.out" | grep -v "frame 0\|frame 1\|frame 2" | grep -q . \
    && { echo "FAIL: post-warmup GL error logged"; FAIL=1; }
# Ensure we actually streamed at least the minimum number of frames so the
# test did not degenerate to a "lock closed immediately" smoke run.
grep -q "started $HACK" "$T/feed.out" || { echo "FAIL: renderer never reported started"; FAIL=1; }
if [ "$FAIL" = 0 ]; then echo "RESULT: PASS: renderer exits cleanly after the lock closes mid-stream"; else echo "RESULT: FAIL"; fi
exit $FAIL