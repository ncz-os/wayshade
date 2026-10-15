#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Headless test: with the default policy the feed delivers a real animation to
# the lock -- at least 25 fresh frames per second (thermal governor pinned cool) and no gap over 500 ms -- and
# logs that cadence. Software EGL, mock lock, a light saver, 320x240.
# usage: test-feed-cadence.sh SRC_DIR BIN_DIR [HACK_ID]
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
gsettings set dev.ncz.screensaver mode one
gsettings set dev.ncz.screensaver hack-id "$HACK"
gsettings set dev.ncz.screensaver verify-render false
sock="$T/xdg/lock.sock"
python3 "$SRC/launcher/tests/feed-mock-lock.py" --socket "$sock" --width 320 --height 240 \
    --frames 150 --result "$T/mock.json" --timeout 60 &
mock=$!
for _ in $(seq 1 100); do [ -S "$sock" ] && break; sleep 0.05; done
# Default policy (no NCZ_SAVER_LOCK_FPS / _RENDER_DIV; a container has no battery), cadence every 2 s.
env -u NCZ_SAVER_LOCK_FPS -u NCZ_FEED_FPS -u NCZ_SAVER_RENDER_DIV NCZ_FEED_CADENCE_S=2 NCZ_FEED_TEMP_MC=40000 NCZ_FEED_GPU_CLASS=strong \
    SINGULARITY_LOCK_RENDERER_SOCKET="$sock" \
    timeout 60 python3 "$SRC/launcher/ncz-screensaver-feed" > "$T/feed.out" 2>&1
wait "$mock"; mrc=$?
grep -aE "settings mode|started|cadence" "$T/feed.out" | sed 's/^/  /'
fail=0
[ "$mrc" = 0 ] || { echo "FAIL: mock lock rejected the stream: $(cat $T/mock.json)"; fail=1; }
grep -q "fps=30 render_div=1" "$T/feed.out" || { echo "FAIL: default policy is not 30 fps / full resolution"; fail=1; }
best=$(grep -aoE "cadence: [0-9]+ frames in [0-9.]+ s = [0-9.]+ fps, max gap [0-9]+ ms" "$T/feed.out" | \
       awk '{f=$8; g=$12; if (f+0 > bf+0) {bf=f; bg=g}} END{print bf, bg}')
set -- $best
if [ -z "${1:-}" ]; then echo "FAIL: no cadence line logged"; fail=1
elif awk -v f="$1" -v g="$2" 'BEGIN{exit !(f >= 25 && g <= 500)}'; then echo "PASS: cadence $1 fps, max gap $2 ms"
else echo "FAIL: cadence $1 fps, max gap $2 ms (want >= 25 fps, <= 500 ms)"; fail=1; fi
[ "$fail" = 0 ] && echo "RESULT: PASS" || echo "RESULT: FAIL"
exit $fail
