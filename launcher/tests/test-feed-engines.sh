#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Headless test: every listed hack must render VISIBLE, CHANGING frames in the
# off-screen frame-source mode the lock uses (ncz-screensaver-feed). Hacks that
# bind framebuffer 0 for their final pass (classics post-processing, Black
# Hole, Magmasimplex, ...) used to deliver black frames off-screen because there
# is no window framebuffer; src/ncz_fbo_redirect.c maps 0 to the feed's FBO.
# Software EGL, mock lock, no compositor and no GPU.
#
# usage: test-feed-engines.sh SRC_DIR BIN_DIR [HACK_ID...]
set -u
SRC=${1:?usage: $0 SRC_DIR BIN_DIR [HACK_ID...]}
BIN=${2:?usage: $0 SRC_DIR BIN_DIR [HACK_ID...]}
shift 2
HACKS=("$@")
[ ${#HACKS[@]} -gt 0 ] || HACKS=(cubestorm_gles3 voronoi_gles3 noof_gles3 magmasimplex_gles3 blackhole_gles3 hyprsaver_starfield_gles3 fractasia_gles3)

[ -n "${TMPDIR:-}" ] && mkdir -p "$TMPDIR" 2>/dev/null
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/xdg" "$T/cfg" "$T/schemas" "$T/empty"; chmod 700 "$T/xdg"
cp "$SRC/config/dev.ncz.screensaver.gschema.xml" "$T/schemas/"
GLIB_BIN=$(command -v glib-compile-schemas || echo /usr/bin/glib-compile-schemas)
"$GLIB_BIN" "$T/schemas"

export XDG_RUNTIME_DIR="$T/xdg" XDG_CONFIG_HOME="$T/cfg"
export GSETTINGS_BACKEND=keyfile GSETTINGS_SCHEMA_DIR="$T/schemas"
export NCZ_ALLOW_SOFTWARE=1 LIBGL_ALWAYS_SOFTWARE=1
export NCZ_FEED_GUARD_ROOT="$T/empty"
export NCZ_SCREENSAVER_DIRS="$BIN"
export NCZ_SCREENSAVER_CATALOG="$SRC/assets/screensaver-chooser/hacks.tsv"
export NCZ_SCREENSAVER_LAUNCHER="$SRC/launcher/ncz-screensaver"

S=dev.ncz.screensaver
gsettings set "$S" mode one
gsettings set "$S" verify-render false

FAIL=0; RAN=0
for HACK in "${HACKS[@]}"; do
    if [ ! -x "$BIN/$HACK" ]; then echo "SKIP: $HACK not built"; continue; fi
    RAN=$((RAN+1))
    gsettings set "$S" hack-id "$HACK"
    sock="$T/xdg/lock-$HACK.sock"
    rm -rf "$XDG_RUNTIME_DIR/ncz-screensaver"
    python3 "$SRC/launcher/tests/feed-mock-lock.py" --socket "$sock" --width 320 --height 240 \
        --frames 24 --require-nonblack --result "$T/mock-$HACK.json" \
        --timeout "${NCZ_TEST_TIMEOUT:-90}" &
    mock=$!
    _d=$(( $(date +%s) + 10 )); while [ ! -S "$sock" ] && [ "$(date +%s)" -lt "$_d" ]; do sleep 0.05; done
    env NCZ_SAVER_LOCK_FPS=12 NCZ_SAVER_RENDER_DIV=1 SINGULARITY_LOCK_RENDERER_SOCKET="$sock" \
        timeout "${NCZ_TEST_TIMEOUT:-90}" python3 "$SRC/launcher/ncz-screensaver-feed" \
        >"$T/feed-$HACK.out" 2>&1
    wait "$mock"; mrc=$?
    res=$(cat "$T/mock-$HACK.json" 2>/dev/null)
    if [ "$mrc" -eq 0 ]; then
        echo "PASS: $HACK renders visible, changing frames off-screen"
    else
        echo "FAIL: $HACK: $res"
        grep -a -E "GL error|compile|link|exit" "$T/feed-$HACK.out" | head -5 | sed 's/^/    /'
        FAIL=1
    fi
done
[ "$RAN" -gt 0 ] || { echo "SKIP: no listed hack is built"; exit 77; }
if [ "$FAIL" -ne 0 ]; then echo "RESULT: FAIL"; exit 1; fi
echo "RESULT: PASS: $RAN hack(s) render visible, changing frames off-screen"
exit 0
