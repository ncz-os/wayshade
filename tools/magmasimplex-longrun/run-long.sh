#!/usr/bin/env bash
# run-long.sh - randomized magmasimplex slice driver.
#
#   run-long.sh <total_minutes> [base_seed]
#
# Every MAGMA_SLICE_SECONDS (default 300) the running saver is ended by exact
# PID (SIGTERM, then SIGKILL) and a new slice is started with a freshly drawn
# configuration, so colours and variables change every five minutes. The draw
# is a pure function of (base_seed, slice) via draw.py, so a run is replayable.
#
# Deliverable of item MAG (2026-10-04); categories added for item MAGREAL
# (2026-10-05). Source: tools/magmasimplex-longrun/ in ncz-screensavers;
# deployed under /home/user/magma-test/ on Orion O6N.
# Weights: MAGMA_LAMP_PCT MAGMA_SHOW_PCT MAGMA_FLOWERCHILD_PCT MAGMA_CANDY_PCT
# (defaults 70/15/15/0; candy is the glossy jelly look, opt-in only).
#   logs/<runid>/slices.tsv   one row per slice: start, every chosen value, cmd
#   logs/<runid>/s<NN>.log    the saver's stderr for that slice
#   logs/<runid>/samples.csv  CPU, RSS, GPU freq/load and harness frame time
#   logs/<runid>/summary.txt  slices, frame ms, GL errors, RSS, palettes
#   shots/<runid>/s<NN>-t<sec>.png + .1600.png
#
# Reproduce one slice:   run-slice.sh logs/<runid>/slices.tsv <slice_no>
# Contact sheet:         contact-sheet.sh <runid>
#
# It refuses to start while any saver from /usr/lib/ncz-screensavers or this
# test is running, and it disables the idle-timer saver for the duration with a
# trap-protected `gsettings set dev.ncz.screensaver mode off`, restoring the
# previous value on EXIT/INT/TERM.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
BIN="$ROOT/bin/magmasimplex_gles3"
SCHEMA="$ROOT/options/magmasimplex.tsv"
PRESETS="$ROOT/presets/magmasimplex"

TOTAL_MIN="${1:-}"
BASE_SEED="${2:-}"
if [ -z "$TOTAL_MIN" ]; then
    echo "usage: run-long.sh <total_minutes> [base_seed]" >&2
    exit 2
fi
case "$TOTAL_MIN" in (*[!0-9]*) echo "run-long.sh: total_minutes must be an integer" >&2; exit 2;; esac
[ "$TOTAL_MIN" -lt 1 ] && { echo "run-long.sh: total_minutes must be >= 1" >&2; exit 2; }
[ -n "$BASE_SEED" ] || BASE_SEED="$(date +%s)"

SLICE_SECONDS="${MAGMA_SLICE_SECONDS:-300}"
SHOT_OFFSETS="${MAGMA_SHOT_OFFSETS:-20 150 280}"
# Draw categories (docs/MAGMASIMPLEX-REALISM.md): lamp-faithful, the ink light
# show, the Flowerchild dye show, and the glossy candy look (opt-in: 0 unless
# MAGMA_CANDY_PCT is set). Logged per slice in slices.tsv (column "category").
LAMP_PCT="${MAGMA_LAMP_PCT:-70}"
SHOW_PCT="${MAGMA_SHOW_PCT:-15}"
FLOWERCHILD_PCT="${MAGMA_FLOWERCHILD_PCT:-${MAGMA_BUTTERFLY_PCT:-15}}"
CANDY_PCT="${MAGMA_CANDY_PCT:-0}"
TOTAL_SECONDS=$((TOTAL_MIN * 60))

# The Singularity session exports G_MESSAGES_DEBUG=all, which makes GLib tools
# (gsettings) print debug to stdout and corrupt captured values. Silence it,
# as the launcher's build_child_env does for its children.
unset G_MESSAGES_DEBUG GLIB_DEBUG 2>/dev/null || true

RUNID="$(date +%Y%m%d-%H%M%S)-seed${BASE_SEED}"
LOGDIR="$ROOT/logs/$RUNID"
SHOTDIR="$ROOT/shots/$RUNID"
mkdir -p "$LOGDIR" "$SHOTDIR" "$ROOT/config" "$ROOT/tmp"
SLICES="$LOGDIR/slices.tsv"
SAMPLES="$LOGDIR/samples.csv"
SUMMARY="$LOGDIR/summary.txt"
CURRENT_PID="$LOGDIR/current.pid"
CURRENT_SLICE="$LOGDIR/current.slice"
CURRENT_LOG="$LOGDIR/current.log"
STOP_FILE="$LOGDIR/.sampler_stop"
CUR_PID=""
SAMPLER_PID=""

LOG() { printf '%s %s\n' "$(date -Is)" "$*"; }

if [ ! -x "$BIN" ]; then
    echo "run-long.sh: $BIN is not executable" >&2
    exit 2
fi

# ---------------------------------------------------------------- runtime env
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}"

# Copy the compositor's GPU environment, exactly as the launcher's
# build_child_env does (labwc on Orion O6N pins the Mali EGL/Vulkan vendor).
comp_pid="$(pgrep -x labwc 2>/dev/null | head -1)"
if [ -n "$comp_pid" ] && [ -r "/proc/$comp_pid/environ" ]; then
    while IFS='=' read -r k v; do
        case "$k" in
            __EGL*|__GLX*|EGL_*|MESA_*|LIBGL_*|GBM_*|VK_*|NCZ_GPU*|GALLIUM_*|\
            WLR_DRM_NO_MODIFIERS|WLR_DRM_DEVICES|LD_LIBRARY_PATH)
                export "$k=$v" ;;
        esac
    done < <(tr '\0' '\n' < "/proc/$comp_pid/environ")
fi
export NCZ_PRESET_DIR="$PRESETS"

# NOTE: XDG_CONFIG_HOME is deliberately NOT exported in this shell. dconf
# locates its user database under $XDG_CONFIG_HOME, so overriding it here would
# make `gsettings` read (and restore into) an empty database instead of the
# operator's. The saver gets the empty test config only via ENV_PREFIX below.
ENV_PREFIX="XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR WAYLAND_DISPLAY=$WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS XDG_CONFIG_HOME=$ROOT/config NCZ_PRESET_DIR=$NCZ_PRESET_DIR"
for name in __EGL_VENDOR_LIBRARY_FILENAMES VK_DRIVER_FILES WLR_DRM_NO_MODIFIERS \
            WLR_DRM_DEVICES NCZ_GPU_BACKEND MESA_SHADER_CACHE_DIR; do
    eval "val=\${$name:-}"
    [ -n "$val" ] && ENV_PREFIX="$ENV_PREFIX $name=$val"
done

# ------------------------------------------------------------- idle-timer off
HAVE_GS=0
PREV_MODE=""
if command -v gsettings >/dev/null 2>&1; then
    HAVE_GS=1
    # Take the quoted value line, never a stray GLib debug line.
    PREV_MODE="$(gsettings get dev.ncz.screensaver mode 2>/dev/null | grep -m1 -E "^'" | tr -d "'")"
    case "$PREV_MODE" in off|one|random|playlist) ;; *) PREV_MODE="" ;; esac
fi

CLEANED=0
cleanup() {
    [ "$CLEANED" = 1 ] && return 0
    CLEANED=1
    if [ -n "${CUR_PID:-}" ] && kill -0 "$CUR_PID" 2>/dev/null; then
        kill -TERM "$CUR_PID" 2>/dev/null
        sleep 1
        kill -KILL "$CUR_PID" 2>/dev/null
    fi
    touch "$STOP_FILE" 2>/dev/null
    if [ "$HAVE_GS" = 1 ]; then
        if [ -n "$PREV_MODE" ]; then
            gsettings set dev.ncz.screensaver mode "$PREV_MODE" 2>/dev/null
            LOG "restore: gsettings set dev.ncz.screensaver mode $PREV_MODE"
        else
            gsettings reset dev.ncz.screensaver mode 2>/dev/null
            LOG "restore: gsettings reset dev.ncz.screensaver mode (previous value not readable)"
        fi
    fi
}
trap 'cleanup; exit 0' EXIT INT TERM

running_savers() {
    ps -eo pid=,args= | awk -v me="$$" '
        $0 ~ /awk / { next }
        /\/usr\/lib\/ncz-screensavers\/[A-Za-z0-9_.+-]+_gles3([ \t]|$)/ ||
        /magma-test\/bin\/magmasimplex_gles3([ \t]|$)/ { if ($1 != me) print $1 }'
}

LOG "run-long: root=$ROOT total=${TOTAL_MIN}min base_seed=$BASE_SEED slice_seconds=$SLICE_SECONDS shots='$SHOT_OFFSETS' runid=$RUNID categories lamp/show/flowerchild/candy=$LAMP_PCT/$SHOW_PCT/$FLOWERCHILD_PCT/$CANDY_PCT"
if [ "$HAVE_GS" = 1 ]; then
    LOG "idle-timer mode was '$PREV_MODE'; disabling with: gsettings set dev.ncz.screensaver mode off"
    gsettings set dev.ncz.screensaver mode off 2>/dev/null
    sleep 2
else
    LOG "gsettings not found; idle-timer not disabled"
fi

rs="$(running_savers)"
if [ -n "$rs" ]; then
    LOG "refusing to start: saver already running: $rs"
    echo "run-long.sh: refusing to start, saver already running: $rs" >&2
    exit 3
fi

# ------------------------------------------------------------------- sampler
sampler() {
    local prev_j="" prev_t="" prev_pid="" prev_frame="" prev_frame_t=""
    printf 'time_iso,slice,pid,cpu_pct,rss_kb,gpu_freq_hz,gpu_load,cpu_jiffies,frame_ms\n' > "$SAMPLES"
    while [ ! -f "$STOP_FILE" ]; do
        local t_pid t_slice t_log now_t j rss cpu freq load frame d fr
        t_pid="$(cat "$CURRENT_PID" 2>/dev/null)"
        t_slice="$(cat "$CURRENT_SLICE" 2>/dev/null)"
        t_log="$LOGDIR/$(cat "$CURRENT_LOG" 2>/dev/null)"
        # A new slice is a new process: its jiffies and frame counter restart.
        if [ "$t_pid" != "$prev_pid" ]; then
            prev_j=""; prev_t=""; prev_frame=""; prev_frame_t=""
        fi
        j=""; rss=""
        if [ -n "$t_pid" ] && [ -r "/proc/$t_pid/stat" ]; then
            j="$(awk '{print $14+$15}' "/proc/$t_pid/stat" 2>/dev/null)"
            rss="$(awk '/VmRSS/{print $2}' "/proc/$t_pid/status" 2>/dev/null)"
        fi
        now_t="$(date +%s)"; cpu=""
        if [ -n "$prev_j" ] && [ -n "$j" ] && [ "$now_t" -gt "$prev_t" ]; then
            cpu="$(awk -v dj=$((j-prev_j)) -v dt=$((now_t-prev_t)) -v hz="$(getconf CLK_TCK)" \
                   'BEGIN{printf "%.1f", (dj/hz)/dt*100}')"
        fi
        freq=""; load=""
        for d in /sys/class/devfreq/*/; do
            [ -d "$d" ] || continue
            freq="${freq:+$freq;}$(basename "$d")=$(cat "$d/cur_freq" 2>/dev/null)"
            if [ -r "$d/load" ]; then
                load="${load:+$load;}$(basename "$d")=$(cat "$d/load" 2>/dev/null)"
            fi
        done
        # The harness prints "[diag] frame #N" every 60th frame and "[stats]"
        # (steady p50/p95) at exit; there is no per-frame dt on the goo path.
        # Frame time here is the 10 s interval divided by the frame-count delta.
        frame=""; fr=""
        if [ -n "$t_log" ] && [ -r "$t_log" ]; then
            fr="$(tail -n 3000 "$t_log" 2>/dev/null | grep -o 'frame #[0-9]*' | tail -1 | cut -d'#' -f2)"
        fi
        if [ -n "$fr" ]; then
            if [ -n "$prev_frame" ] && [ "$fr" -gt "$prev_frame" ] && [ "$now_t" -gt "$prev_frame_t" ]; then
                frame="$(awk -v df=$((fr - prev_frame)) -v dt=$((now_t - prev_frame_t)) \
                         'BEGIN{printf "%.2f", dt/df*1000}')"
                prev_frame="$fr"; prev_frame_t="$now_t"
            elif [ -z "$prev_frame" ]; then
                prev_frame="$fr"; prev_frame_t="$now_t"
            fi
        fi
        printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$(date -Is)" "${t_slice:-}" "${t_pid:-}" \
            "$cpu" "${rss:-}" "$freq" "$load" "${j:-}" "$frame" >> "$SAMPLES"
        prev_j="$j"; prev_t="$now_t"; prev_pid="$t_pid"
        sleep 10
    done
}
sampler &
SAMPLER_PID=$!

# ----------------------------------------------------------------- slice loop
printf '# start_iso\tslice\tmode\tcategory\tseed\tpattern-seed\tpreset\tpalette\tpalette-era\tworld\tview\tmaterial\tglitter\tglitter-amount\tcolourway\tkaleidoscope\ttexture\tmotes\tglass\tdepth\tdrift\tchapter\tspeed\tviscosity\theat\twax\tcmd\n' > "$SLICES"

RUN_START="$(date +%s)"
RUN_END=$((RUN_START + TOTAL_SECONDS))
slice=0
while :; do
    nowt="$(date +%s)"
    [ "$nowt" -ge "$RUN_END" ] && break
    slice=$((slice + 1))
    row="$(python3 "$ROOT/draw.py" --tsv "$SCHEMA" --presets "$PRESETS" --binary "$BIN" \
           --base-seed "$BASE_SEED" --slice "$slice" --env-prefix "$ENV_PREFIX" \
           --lamp-pct "$LAMP_PCT" --show-pct "$SHOW_PCT" --flowerchild-pct "$FLOWERCHILD_PCT" --candy-pct "$CANDY_PCT")" || {
        LOG "draw failed for slice $slice"; break; }
    printf '%s\t%s\n' "$(date -Is)" "$row" >> "$SLICES"
    cmd="$(printf '%s' "$row" | awk -F'\t' '{print $NF}')"
    slog="$LOGDIR/s$(printf '%02d' "$slice").log"
    LOG "slice $slice start: $(printf '%s' "$row" | cut -f2-12 | tr '\t' ' ')"
    ( cd "$ROOT" || exit 1; exec $cmd ) >> "$slog" 2>&1 &
    CUR_PID=$!
    printf '%s' "$CUR_PID" > "$CURRENT_PID"
    printf '%s' "$slice" > "$CURRENT_SLICE"
    printf '%s' "$(basename "$slog")" > "$CURRENT_LOG"

    for off in $SHOT_OFFSETS; do
        [ "$off" -lt "$SLICE_SECONDS" ] || continue
        (
            sleep "$off"
            img="$SHOTDIR/s$(printf '%02d' "$slice")-t${off}.png"
            if command -v grim >/dev/null 2>&1; then grim "$img" 2>/dev/null; fi
            if [ -s "$img" ]; then
                if command -v magick >/dev/null 2>&1; then
                    magick "$img" -resize '1600x1600>' "${img%.png}.1600.png" 2>/dev/null
                elif command -v convert >/dev/null 2>&1; then
                    convert "$img" -resize '1600x1600>' "${img%.png}.1600.png" 2>/dev/null
                fi
            fi
        ) &
    done

    slice_end=$(( $(date +%s) + SLICE_SECONDS ))
    while [ "$(date +%s)" -lt "$slice_end" ] && [ "$(date +%s)" -lt "$RUN_END" ]; do sleep 1; done

    kill -TERM "$CUR_PID" 2>/dev/null
    for _ in $(seq 1 20); do kill -0 "$CUR_PID" 2>/dev/null || break; sleep 0.5; done
    if kill -0 "$CUR_PID" 2>/dev/null; then kill -KILL "$CUR_PID" 2>/dev/null; fi
    wait "$CUR_PID" 2>/dev/null
    CUR_PID=""
    LOG "slice $slice end"
done

if [ -n "$CUR_PID" ] && kill -0 "$CUR_PID" 2>/dev/null; then
    kill -TERM "$CUR_PID" 2>/dev/null; sleep 1; kill -KILL "$CUR_PID" 2>/dev/null
fi
touch "$STOP_FILE"
wait "$SAMPLER_PID" 2>/dev/null

# ------------------------------------------------------------------- summary
python3 - "$SLICES" "$SAMPLES" "$LOGDIR" "$SUMMARY" "$BASE_SEED" "$SLICE_SECONDS" <<'PY'
import os, re, sys, statistics
slices, samples, logdir, summary, base_seed, slice_seconds = sys.argv[1:7]

rows = []
with open(slices) as f:
    for line in f:
        if line.startswith("#") or not line.strip():
            continue
        rows.append(line.rstrip("\n").split("\t"))

frames, glerr, bad, palettes, steadies = [], 0, [], [], []
stat_re = re.compile(r"steady n=(\d+) p50=([0-9.]+) p95=([0-9.]+) p99=([0-9.]+) max=([0-9.]+) ms")
for fn in sorted(os.listdir(logdir)):
    if not fn.endswith(".log"):
        continue
    with open(os.path.join(logdir, fn), errors="replace") as f:
        txt = f.read()
    for m in re.finditer(r"dt_ms=([0-9.]+)", txt):
        frames.append(float(m.group(1)))
    ms = stat_re.search(txt)
    if ms:
        steadies.append((int(ms.group(1)), float(ms.group(2)), float(ms.group(3))))
    glerr += len(re.findall(r"gl_error=0x(?!0\b)[0-9a-fA-F]+", txt))
    for pat in ("cannot locate", "compile:", "link ", "invalid option", "unknown option"):
        if pat in txt:
            bad.append("%s: %s" % (fn, pat))
    pm = re.search(r"engine=goo[^\n]*?palette=([A-Za-z0-9_.-]+)", txt)
    if pm:
        palettes.append(pm.group(1))

rss = []
cpu = []
with open(samples) as f:
    hdr = f.readline()
    for line in f:
        c = line.rstrip("\n").split(",")
        if len(c) < 9:
            continue
        if c[4]:
            try:
                rss.append(int(c[4]))
            except ValueError:
                pass
        if c[3]:
            try:
                cpu.append(float(c[3]))
            except ValueError:
                pass

preset_n = sum(1 for r in rows if len(r) > 2 and r[2] == "preset")
cats = {}
for r in rows:
    if len(r) > 3:
        cats[r[3]] = cats.get(r[3], 0) + 1
mean_f = statistics.mean(frames) if frames else 0.0
p95_f = (sorted(frames)[min(len(frames) - 1, int(len(frames) * 0.95))] if frames else 0.0)

with open(summary, "w") as out:
    out.write("run summary (item MAG)\n")
    out.write("base_seed=%s slice_seconds=%s\n" % (base_seed, slice_seconds))
    out.write("slices=%d preset_slices=%d\n" % (len(rows), preset_n))
    out.write("categories=%s\n" % ",".join("%s:%d" % kv for kv in sorted(cats.items())))
    if steadies:
        p50s = [s[1] for s in steadies]
        p95s = [s[2] for s in steadies]
        out.write("harness_steady_frames=%d mean_p50=%.2f mean_p95=%.2f ms\n" % (
            sum(s[0] for s in steadies), statistics.mean(p50s), statistics.mean(p95s)))
        out.write("harness_steady_per_slice=%s\n" % ";".join(
            "n=%d p50=%.2f p95=%.2f" % s for s in steadies))
    else:
        out.write("harness_steady=none\n")
    out.write("frames_sampled=%d frame_ms(dt) mean=%.2f p95=%.2f\n" % (len(frames), mean_f, p95_f))
    out.write("gl_errors=%d\n" % glerr)
    out.write("rss_kb min=%s max=%s mean=%.0f\n" % (
        min(rss) if rss else "-", max(rss) if rss else "-",
        statistics.mean(rss) if rss else 0))
    out.write("cpu_pct mean=%.1f max=%.1f\n" % (
        statistics.mean(cpu) if cpu else 0, max(cpu) if cpu else 0))
    out.write("abnormal_slice_logs=%d %s\n" % (len(bad), bad if bad else ""))
    out.write("palettes_shown(%d)=%s\n" % (len(palettes), ",".join(palettes)))
    out.write("slices.tsv=%s\n" % slices)
    out.write("samples.csv=%s\n" % samples)
print(open(summary).read())
PY

LOG "summary written: $SUMMARY"
LOG "run-long done: $SLICES"
