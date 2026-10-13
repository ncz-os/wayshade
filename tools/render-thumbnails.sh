#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# render-thumbnails.sh - reproduce the thumbnails and contact sheets under
# docs/screenshots/.
#
#   tools/render-thumbnails.sh [--build DIR] [--out DIR] [--only ID[,ID...]]
#                              [--timeout SECONDS] [--keep-work]
#
# Defaults: --build build  --out docs/screenshots  --timeout 25
#
# Reproducibility knobs (environment): WIDTH, HEIGHT, FRAME_SKIP, SEED,
# THUMB_W, THUMB_H. Exits non-zero if any saver produced no frame.
#
# docs/screenshots/README.md is the reference.

set -eu

BUILD=build
OUT=docs/screenshots
ONLY=""
TIMEOUT=25
KEEP_WORK=0

WIDTH=${WIDTH:-1280}
HEIGHT=${HEIGHT:-720}
FRAME_SKIP=${FRAME_SKIP:-60}
THUMB_W=${THUMB_W:-320}
THUMB_H=${THUMB_H:-180}
SEED=${SEED:-42}

die() { echo "render-thumbnails: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --build) BUILD=$2; shift 2 ;;
        --out) OUT=$2; shift 2 ;;
        --only) ONLY=$2; shift 2 ;;
        --timeout) TIMEOUT=$2; shift 2 ;;
        --keep-work) KEEP_WORK=1; shift ;;
        -h|--help) sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown argument '$1'" ;;
    esac
done

[ -d "$BUILD" ] || die "build directory '$BUILD' does not exist (meson setup $BUILD)"
[ -x "$BUILD/xshadertoy_render" ] || true
SRCDIR=$(pwd)
[ -f "$SRCDIR/assets/screensaver-chooser/hacks.tsv" ] \
    || die "run this from the source root (assets/screensaver-chooser/hacks.tsv not found)"

# XDG_RUNTIME_DIR must be an absolute path, so make the work area one.
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
WORK="$OUT/.work"
rm -rf "$WORK"
mkdir -p "$WORK" "$OUT/thumbs"

# Compositor
SWAY_PID=""
STARTED_COMPOSITOR=0

start_compositor() {
    command -v sway >/dev/null 2>&1 || die "no Wayland compositor found; set WAYLAND_DISPLAY to a running one"
    [ -n "${WLR_BACKENDS:-}" ] || WLR_BACKENDS=headless
    export WLR_BACKENDS WLR_RENDERER=${WLR_RENDERER:-gles2} EGL_PLATFORM=${EGL_PLATFORM:-surfaceless}
    export WLR_HEADLESS_OUTPUTS=1
    export XDG_RUNTIME_DIR="$WORK/xdg"
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 700 "$XDG_RUNTIME_DIR"
    setsid nohup sway -c /dev/null >"$WORK/sway.log" 2>&1 </dev/null &
    SWAY_PID=$!
    STARTED_COMPOSITOR=1
    # The runtime dir was created fresh above, so the only Wayland socket in
    # it is the one this compositor just made. Wait for that socket, then use
    # the name it has rather than assuming a number.
    sock=""
    i=0
    while [ "$i" -lt 150 ]; do
        for cand in "$XDG_RUNTIME_DIR"/wayland-*; do
            if [ -S "$cand" ]; then sock=$cand; break; fi
        done
        [ -n "$sock" ] && break
        i=$((i + 1))
        sleep 0.2
    done
    [ -n "$sock" ] || die "headless compositor did not come up; see $WORK/sway.log"
    WAYLAND_DISPLAY=$(basename "$sock")
    export WAYLAND_DISPLAY
    echo "render-thumbnails: headless compositor on $WAYLAND_DISPLAY"
}

cleanup() {
    if [ -n "$SWAY_PID" ]; then
        kill "$SWAY_PID" 2>/dev/null || true
        wait "$SWAY_PID" 2>/dev/null || true
    fi
    if [ "$KEEP_WORK" -eq 0 ] && [ "$STARTED_COMPOSITOR" -eq 1 ]; then
        rm -rf "$WORK"
    fi
}
trap cleanup EXIT INT TERM

if [ -n "${WAYLAND_DISPLAY:-}" ] && [ -S "${XDG_RUNTIME_DIR:-/nonexistent}/$WAYLAND_DISPLAY" ]; then
    :
else
    if [ -n "${WAYLAND_DISPLAY:-}" ] && [ "${NCZ_ALLOW_SOFTWARE:-0}" = "1" ]; then
        :
    else
        start_compositor
    fi
fi

# Frame file name: frame_%08lx.png, zero padded hex.
FRAME_FILE=$(printf 'frame_%08x.png' "$FRAME_SKIP")

# Saver list: the shipped binary ids from the generated catalogue
IDS=$(awk -F'\t' '!/^#/ && NF {print $1}' assets/screensaver-chooser/hacks.tsv)
if [ -n "$ONLY" ]; then
    # Fixed-string membership, one id at a time: a saver id is data, not a
    # pattern, so it must not reach grep as a regular expression.
    keep=$WORK/only.txt
    : > "$keep"
    OLDIFS=$IFS
    IFS=,
    for want in $ONLY; do
        printf '%s\n' "$want" >> "$keep"
    done
    IFS=$OLDIFS
    IDS=$(grep -F -x -f "$keep" <<EOF || true
$IDS
EOF
)
fi
[ -n "$IDS" ] || die "no savers selected"

TOTAL=0
OK=0
FAILED="$WORK/failed.txt"
BLANK=""
: > "$FAILED"

printf 'render-thumbnails: %s savers, %sx%s -> %sx%s thumbs, frame %s, seed %s\n' \
    "$(printf '%s\n' "$IDS" | wc -l | tr -d ' ')" "$WIDTH" "$HEIGHT" \
    "$THUMB_W" "$THUMB_H" "$FRAME_SKIP" "$SEED"

for id in $IDS; do
    short=${id%_gles3}
    bin="$BUILD/$id"
    TOTAL=$((TOTAL + 1))
    if [ ! -x "$bin" ]; then
        echo "$id	no-binary	$bin missing or not executable" >> "$FAILED"
        printf '  %-40s SKIP (no binary)\n' "$id"
        continue
    fi
    dump="$WORK/$id"
    rm -rf "$dump"
    mkdir -p "$dump"

    # Savers that declare a seed option take it on the command line; the
    # hyprsaver family declares none, so only NCZ_SEED applies there.
    case "$id" in
        blackhole_gles3|magmasimplex_gles3|fractasia_gles3|voronoi_gles3|\
        hexstrut_gles3|cubestorm_gles3|crackberg_gles3|cityflow_gles3|\
        noof_gles3|geodesic_gles3|datatowers_gles3)
            set -- --seed="$SEED" ;;
        *) set -- ;;
    esac

    NCZ_FRAME_DUMP="$dump" NCZ_FRAME_DUMP_EVERY="$FRAME_SKIP" NCZ_SEED="$SEED" \
        "$bin" "$@" >"$dump/log" 2>&1 &
    saver_pid=$!

    # Stop as soon as the frame we want exists, so a saver that runs forever
    # costs a second or two rather than the whole timeout. Kill only this pid.
    ticks=$((TIMEOUT * 10))
    waited=0
    while [ "$waited" -lt "$ticks" ]; do
        [ -f "$dump/$FRAME_FILE" ] && break
        kill -0 "$saver_pid" 2>/dev/null || break
        sleep 0.1
        waited=$((waited + 1))
    done
    kill "$saver_pid" 2>/dev/null || true
    wait "$saver_pid" 2>/dev/null || true

    png="$dump/$FRAME_FILE"
    if [ ! -f "$png" ]; then
        # Fall back to the newest frame the harness did write, so a saver that
        # renders slower than FRAME_SKIP still gets a thumbnail.
        png=$(ls -1 "$dump"/frame_*.png 2>/dev/null | tail -1 || true)
    fi
    if [ -z "$png" ] || [ ! -f "$png" ]; then
        echo "$id	no-frame	no frame written in ${TIMEOUT}s" >> "$FAILED"
        printf '  %-40s FAIL (no frame)\n' "$id"
        continue
    fi

    if ! python3 "$(dirname "$0")/render-thumbnails.py" frame \
            --in "$png" --out "$OUT/thumbs/$short.png" \
            --thumb "${THUMB_W}x${THUMB_H}" >>"$WORK/resize.log" 2>&1; then
        echo "$id	resize-failed	$png" >> "$FAILED"
        printf '  %-40s FAIL (resize)\n' "$id"
        continue
    fi
    if python3 "$(dirname "$0")/render-thumbnails.py" blank --in "$png" \
            >>"$WORK/resize.log" 2>&1; then
        BLANK="$BLANK $id"
    fi
    OK=$((OK + 1))
    printf '  %-40s ok\n' "$id"
done

# Fractasia compiles 34 shader families at startup, so on a cold shader cache
# it needs the longer timeout. If it still produced no frame, fall back to the
# in-tree GPU smoke test, which renders every family offscreen.
FRACTASIA_FAMILY=${FRACTASIA_FAMILY:-orbit_trap}
case "$IDS" in
    *fractasia_gles3*)
        if [ ! -f "$OUT/thumbs/fractasia.png" ] && \
           [ -x "$BUILD/test_fractasia_smoke" ] && [ -d "$SRCDIR/vendor/fractasia/glsl" ]; then
            fdir="$WORK/fractasia-smoke"
            mkdir -p "$fdir"
            (cd "$SRCDIR" && EGL_PLATFORM=surfaceless NCZ_FRACTASIA_OUT="$fdir" \
                "$BUILD/test_fractasia_smoke" >"$WORK/fractasia.log" 2>&1) || true
            if [ -f "$fdir/$FRACTASIA_FAMILY.frag.png" ]; then
                python3 "$(dirname "$0")/render-thumbnails.py" frame \
                    --in "$fdir/$FRACTASIA_FAMILY.frag.png" \
                    --out "$OUT/thumbs/fractasia.png" \
                    --thumb "${THUMB_W}x${THUMB_H}" >>"$WORK/resize.log" 2>&1 \
                    && printf '  %-40s ok (offscreen GPU smoke render, family %s)\n' \
                        fractasia_gles3 "$FRACTASIA_FAMILY"
            fi
        fi
        ;;
esac

# Contact sheets, one per catalogue category
python3 "$(dirname "$0")/render-thumbnails.py" sheets \
    --catalog assets/screensaver-chooser/hacks.tsv \
    --categories assets/screensaver-chooser/categories.tsv \
    --category-list assets/screensaver-chooser/category-list.tsv \
    --thumbs "$OUT/thumbs" --out "$OUT" --columns 5

printf 'render-thumbnails: %s/%s savers captured\n' "$OK" "$TOTAL"
if [ -n "$BLANK" ]; then
    printf 'render-thumbnails: blank frame(s), see docs/screenshots/README.md:%s\n' "$BLANK"
fi
if [ -s "$FAILED" ]; then
    printf 'render-thumbnails: %s saver(s) not captured (see the report):\n' "$(wc -l < "$FAILED" | tr -d ' ')"
    cat "$FAILED"
fi
[ "$OK" -eq "$TOTAL" ] || exit 1