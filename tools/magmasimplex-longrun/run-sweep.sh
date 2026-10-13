#!/usr/bin/env bash
# run-sweep.sh - run a list of fixed magmasimplex configurations one after
# another, screenshot each with grim, and record the harness [stats] line.
#
#   run-sweep.sh <configs.tsv> <runid> [seconds] [shot_offsets]
#
# configs.tsv: one row per run, "name<TAB>category<TAB>KEY=VALUE KEY=VALUE ...".
# Output: shots/<runid>/<name>-t<sec>.png (+ .1600.png), logs/<runid>/<name>.log,
# logs/<runid>/sweep.tsv (name, category, steady p50/p95, env).
# Same safety as run-long.sh: idle-timer mode off for the duration and
# restored by trap; refuses to start while another saver runs; stops only the
# PIDs it started.
set -u
ROOT="$(cd "$(dirname "$0")" && pwd)"
BIN="${MAGMA_BIN:-$ROOT/bin/magmasimplex_gles3}"
CFG="${1:-}"; RUNID="${2:-}"; SECS="${3:-25}"; SHOTS="${4:-20}"
[ -n "$CFG" ] && [ -n "$RUNID" ] || { echo "usage: run-sweep.sh <configs.tsv> <runid> [seconds] [shot_offsets]" >&2; exit 2; }
[ -r "$CFG" ] || { echo "cannot read $CFG" >&2; exit 2; }
[ -x "$BIN" ] || { echo "$BIN not executable" >&2; exit 2; }
unset G_MESSAGES_DEBUG GLIB_DEBUG 2>/dev/null || true
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}"
comp_pid="$(pgrep -x labwc 2>/dev/null | head -1)"
if [ -n "$comp_pid" ] && [ -r "/proc/$comp_pid/environ" ]; then
    while IFS='=' read -r k v; do
        case "$k" in
            __EGL*|__GLX*|EGL_*|MESA_*|LIBGL_*|GBM_*|VK_*|NCZ_GPU*|GALLIUM_*|WLR_DRM_NO_MODIFIERS|WLR_DRM_DEVICES|LD_LIBRARY_PATH) export "$k=$v" ;;
        esac
    done < <(tr '\0' '\n' < "/proc/$comp_pid/environ")
fi
ENV_PREFIX="XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR WAYLAND_DISPLAY=$WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS XDG_CONFIG_HOME=$ROOT/config NCZ_PRESET_DIR=$ROOT/presets/magmasimplex"
for name in __EGL_VENDOR_LIBRARY_FILENAMES VK_DRIVER_FILES WLR_DRM_NO_MODIFIERS WLR_DRM_DEVICES NCZ_GPU_BACKEND MESA_SHADER_CACHE_DIR; do
    eval "val=\${$name:-}"; [ -n "$val" ] && ENV_PREFIX="$ENV_PREFIX $name=$val"
done
LOGDIR="$ROOT/logs/$RUNID"; SHOTDIR="$ROOT/shots/$RUNID"; mkdir -p "$LOGDIR" "$SHOTDIR" "$ROOT/config"
LOG() { printf '%s %s\n' "$(date -Is)" "$*"; }
PREV_MODE=""; HAVE_GS=0
if command -v gsettings >/dev/null 2>&1; then
    HAVE_GS=1
    PREV_MODE="$(gsettings get dev.ncz.screensaver mode 2>/dev/null | grep -m1 -E "^'" | tr -d "'")"
    case "$PREV_MODE" in off|one|random|playlist) ;; *) PREV_MODE="" ;; esac
fi
CUR_PID=""; CLEANED=0
cleanup() {
    [ "$CLEANED" = 1 ] && return 0; CLEANED=1
    if [ -n "${CUR_PID:-}" ] && kill -0 "$CUR_PID" 2>/dev/null; then kill -TERM "$CUR_PID" 2>/dev/null; sleep 1; kill -KILL "$CUR_PID" 2>/dev/null; fi
    if [ "$HAVE_GS" = 1 ]; then
        if [ -n "$PREV_MODE" ]; then gsettings set dev.ncz.screensaver mode "$PREV_MODE" 2>/dev/null; LOG "restore: gsettings set dev.ncz.screensaver mode $PREV_MODE"
        else gsettings reset dev.ncz.screensaver mode 2>/dev/null; LOG "restore: gsettings reset dev.ncz.screensaver mode"; fi
    fi
}
trap 'cleanup; exit 0' EXIT INT TERM
running_savers() {
    ps -eo pid=,args= | awk -v me="$$" '$0 ~ /awk / { next }
        /\/usr\/lib\/ncz-screensavers\/[A-Za-z0-9_.+-]+_gles3([ \t]|$)/ || /magma-test\/bin\/[A-Za-z0-9_.+-]*magmasimplex_gles3([ \t]|$)/ { if ($1 != me) print $1 }'
}
LOG "run-sweep: cfg=$CFG runid=$RUNID secs=$SECS shots='$SHOTS' bin=$BIN"
if [ "$HAVE_GS" = 1 ]; then LOG "idle-timer mode was '$PREV_MODE'; setting off"; gsettings set dev.ncz.screensaver mode off 2>/dev/null; sleep 2; fi
rs="$(running_savers)"; [ -n "$rs" ] && { LOG "refusing to start: saver running: $rs"; exit 3; }
SWEEP="$LOGDIR/sweep.tsv"
printf '# name\tcategory\tsteady_n\tp50_ms\tp95_ms\tdiag\tenv\n' > "$SWEEP"
while IFS=$'\t' read -r name cat envs; do
    case "$name" in ''|'#'*) continue;; esac
    slog="$LOGDIR/$name.log"
    cmd="env $ENV_PREFIX $envs NCZ_MAGMASIMPLEX_PERF_LOG=1 $BIN"
    LOG "run $name [$cat]: $envs"
    ( cd "$ROOT" || exit 1; exec $cmd ) > "$slog" 2>&1 &
    CUR_PID=$!
    for off in $SHOTS; do
        ( sleep "$off"; img="$SHOTDIR/$name-t${off}.png"; grim "$img" 2>/dev/null
          [ -s "$img" ] && { command -v magick >/dev/null 2>&1 && magick "$img" -resize '1600x1600>' "${img%.png}.1600.png" 2>/dev/null || convert "$img" -resize '1600x1600>' "${img%.png}.1600.png" 2>/dev/null; }
          # MAGMA_KEEP_FULL=0 keeps only the 1600 px copy (long strips).
          [ "${MAGMA_KEEP_FULL:-1}" = 0 ] && [ -s "${img%.png}.1600.png" ] && rm -f "$img" ) &
    done
    end=$(( $(date +%s) + SECS )); while [ "$(date +%s)" -lt "$end" ]; do sleep 1; done
    kill -TERM "$CUR_PID" 2>/dev/null
    for _ in $(seq 1 20); do kill -0 "$CUR_PID" 2>/dev/null || break; sleep 0.5; done
    kill -0 "$CUR_PID" 2>/dev/null && kill -KILL "$CUR_PID" 2>/dev/null
    wait "$CUR_PID" 2>/dev/null; CUR_PID=""
    wait 2>/dev/null
    st="$(grep -o 'steady n=[0-9]* p50=[0-9.]* p95=[0-9.]*' "$slog" | tail -1)"
    n="$(printf '%s' "$st" | sed -n 's/.*n=\([0-9]*\).*/\1/p')"; p50="$(printf '%s' "$st" | sed -n 's/.*p50=\([0-9.]*\).*/\1/p')"; p95="$(printf '%s' "$st" | sed -n 's/.*p95=\([0-9.]*\).*/\1/p')"
    dg="$(grep -m1 'magmasimplex engine=goo' "$slog" | sed 's/.*first-view=//')"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$cat" "${n:--}" "${p50:--}" "${p95:--}" "$dg" "$envs" >> "$SWEEP"
done < "$CFG"
LOG "run-sweep done: $SWEEP"
