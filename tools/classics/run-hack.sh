#!/bin/bash
# runnew.sh bindir outdir secs "extra args" tag hack...   (takes HOST-LOCK.d, waits up to 10 min)
bin=$1; out=$2; secs=$3; args=$4; tag=$5; shift 5
L=$HOME/HOST-LOCK.d
for i in $(seq 1 60); do mkdir "$L" 2>/dev/null && break; sleep 10; done
[ -d "$L" ] || { echo "LOCK not acquired"; exit 9; }
now=$(date +%s)
printf 'mode=test\nwho=LEAD11 classics-shader-port\nwhat=shader-port capture/perf under timeout\nstart=%s\nexpected_end=%s\n' $now $((now+900)) > $L/note
trap 'rm -rf "$L"' EXIT
export XDG_RUNTIME_DIR=/run/user/$(id -u) WAYLAND_DISPLAY=wayland-0
# use the compositor's own GPU environment (never software rendering)
cp=$(pgrep -u $(id -u) -x 'labwc|Hyprland|sway|weston' | head -1)
if [ -n "$cp" ] && [ -r /proc/$cp/environ ]; then
  while IFS= read -r -d '' kv; do case $kv in __EGL_VENDOR_LIBRARY_FILENAMES=*|LD_LIBRARY_PATH=*|MESA_*|GBM_*|LIBGL_*) export "$kv";; esac; done < /proc/$cp/environ
fi
for h in "$@"; do
  d=$out/$h-$tag; rm -rf $d; mkdir -p $d
  if [[ "$tag" == perf* ]]; then unset NCZ_FRAME_DUMP; else export NCZ_FRAME_DUMP=$d; fi
  timeout -k 3 $secs $bin/${h}_gles3 $args > $d/log.txt 2>&1
  rc=$?
  rm -f $d/*.rgba
  echo "$h $tag rc=$rc $(grep '^\[stats\]' $d/log.txt | tail -1 | cut -c1-230)"
  grep -E "^\[classics\]|^\[guard\]|Segmentation" $d/log.txt | head -3
done
