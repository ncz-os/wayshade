#!/bin/sh
# Runs ON the Sky1 host (O6N or MS-R1): fps vs render size for the shader hacks at the real operating points
# of a 4K panel with output scale 1.75, emulated with NCZ_TEST_SURFACE_SIZE when the attached panel is smaller.
#
#   usage: o6sweep3.sh BINDIR SHADERDIR [SURFACE_WxH ...]
#   env:   HOSTLOCK (path of host-lock.sh), LOCKWAIT, SECS (default 10), HACKS (space list)
#
# Points, per hack (S = the surface the compositor configures, logical pixels):
#   native4k : S=3840x2160  max-render-height=0     (scale 1.0, output pixels; only if output scale is 1)
#   default  : S=2194x1234  max-render-height=0     (what a hack renders with output scale 1.75, no cap)
#   capped   : S=2194x1234  no explicit cap         (the Mali platform default: 1080 lines)
# Slow hacks (< ~52 fps at a point) are re-run at scale 0.75, 0.5, 0.35 for that point.
#
# Never kills by name: only the PID started here is signalled. Needs an active user session (loginctl), the
# host lock (host-lock.sh) and the compositor's GPU environment.
BIN=$1; SHD=$2; shift 2
LOCKSH=${HOSTLOCK:-$HOME/host-lock.sh}
SECS=${SECS:-10}
uid=$(id -u)
loginctl list-sessions --no-legend 2>/dev/null | awk -v u="$uid" '$2==u && $4=="seat0"' | grep -q . || { echo "no seat0 session for uid $uid; skipping"; exit 8; }
LABWC=$(pgrep -u "$uid" -o labwc) || { echo "no user labwc; skipping"; exit 8; }
sh "$LOCKSH" acquire test lead1 "render-size sweep of shader hacks at 4K operating points" --expect 120 --wait "${LOCKWAIT:-900}" || exit 9
CUR=
cleanup() { [ -n "$CUR" ] && kill -TERM "$CUR" 2>/dev/null; sh "$LOCKSH" release; }
trap cleanup EXIT INT TERM
for v in $(tr '\0' '\n' < /proc/$LABWC/environ | grep -E '^(__EGL|VK_|LD_LIBRARY|LIBGL|MESA|GBM|EGL_|GALLIUM|PAN|MALI|CIX)' | sed 's/ /%20/g'); do export "$(echo $v | sed 's/%20/ /g')"; done
export XDG_RUNTIME_DIR=/run/user/$uid WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0} NCZ_SHADER_DIR=$SHD
cd "$BIN" || exit 1
OUT=${OUT:-$PWD/sweep3.txt}
{ echo "date: $(date -u +%FT%TZ)"; echo "output: $(wlr-randr | grep -E 'current|Scale' | head -2 | tr '\n' ' ')"
  echo "modules: $(lsmod | grep -oE 'mali_kbase|panthor' | tr '\n' ' ')"; echo "seconds: $SECS"; } > "$OUT"
run() { # bin point surface capargs scale
  rm -f "$OUT.one"
  env NCZ_TEST_SURFACE_SIZE=$3 timeout -k 2 $((SECS+2)) nice -n 5 ./$1 $4 --render-scale=$5 --render-scale-mode=fixed > "$OUT.one" 2>&1 &
  CUR=$!; wait $CUR; CUR=
  echo "$1 $2 $5 $(grep '^\[diag\] gles3_harness: render size' "$OUT.one" | tail -1 | sed 's/.*render size \([0-9x]*\).*/\1/') $(grep '^\[stats\]' "$OUT.one" | tail -1)" >> "$OUT"
}
if [ -n "${PAIRS:-}" ]; then   # PAIRS=file of "hack point scale..." lines: only the lower scales for those (hack, point) pairs
  while read -r h p scales; do
    case $p in native4k) s=3840x2160; c="--max-render-height=0";; default) s=2194x1234; c="--max-render-height=0";; capped) s=2194x1234; c="";; esac
    for sc2 in $scales; do
      run $h $p $s "$c" $sc2
      n2=$(tail -1 "$OUT" | sed -n 's/.*steady n=\([0-9]*\).*/\1/p'); [ "${n2:-0}" -ge 385 ] && break
    done
    sh "$LOCKSH" refresh
  done < "$PAIRS"
  echo DONE >> "$OUT"; exit 0
fi
hacks=${HACKS:-$(ls | grep _gles3 | grep -E '^(blackhole|hyprsaver_|xshadertoy_)')}
SURF=${*:-"3840x2160 2194x1234"}
n=0
for h in $hacks; do
  for s in $SURF; do
    case $s in 3840x2160) run $h native4k $s "--max-render-height=0" 1;; *) run $h default $s "--max-render-height=0" 1; run $h capped $s "" 1;; esac
  done
  n=$((n+1)); [ $((n % 8)) -eq 0 ] && sh "$LOCKSH" refresh
done
# phase two: every slow (point, hack) at lower scales; steady n counts frames in the ~7 s window (55 fps = 385)
grep -E ' (native4k|default|capped) 1 ' "$OUT" | while read -r h p sc rs rest; do
  n=$(echo "$rest" | sed -n 's/.*steady n=\([0-9]*\).*/\1/p'); [ -z "$n" ] && n=0
  [ "$n" -lt 385 ] || continue
  case $p in native4k) s=3840x2160; c="--max-render-height=0";; default) s=2194x1234; c="--max-render-height=0";; capped) s=2194x1234; c="";; esac
  for sc2 in 0.75 0.5 0.35; do
    run $h $p $s "$c" $sc2
    n2=$(tail -1 "$OUT" | sed -n 's/.*steady n=\([0-9]*\).*/\1/p'); [ "${n2:-0}" -ge 385 ] && break   # 55 fps reached: stop stepping down
  done
  sh "$LOCKSH" refresh
done
echo DONE >> "$OUT"
