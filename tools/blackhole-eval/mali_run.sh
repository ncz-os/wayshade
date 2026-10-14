#!/bin/sh
# runs ON a Mali host (O6N or MS-R1): palette matrix + presets, under the host lock
# env: RUN (dir with the hack binaries, default $HOME/a50/run), SHD (shader dir), PRE (preset dir)
uid=$(id -u)
loginctl list-sessions --no-legend 2>/dev/null | awk -v u="$uid" '$2==u && $4=="seat0"' | grep -q . || { echo "no seat0 session; skipping"; exit 8; }
sh $HOME/host-lock.sh acquire test lead1 "Black Hole palettes and presets renders (0.5.x)" --expect 20 --wait ${LOCKWAIT:-900} || exit 9
trap 'sh $HOME/host-lock.sh release' EXIT INT TERM
cd ${RUN:-$HOME/a50/run} || exit 1
for v in $(cat /proc/$(pgrep -u $(id -u) -o labwc)/environ | tr '\0' '\n' | grep -E '^(__EGL|VK_|LD_LIBRARY|LIBGL|MESA|GBM|EGL_|GALLIUM|PAN|MALI|CIX)' | sed 's/ /%20/g'); do export "$(echo $v | sed 's/%20/ /g')"; done
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 NCZ_PRESET_DIR=${PRE:-$HOME/a50/usr/share/ncz-screensavers/presets/blackhole} NCZ_SHADER_DIR=${SHD:-$HOME/a50/usr/share/ncz-screensavers/shaders}
{ echo "cmdline: $(grep -o 'module_blacklist=[^ ]*' /proc/cmdline)"; echo "modules: $(lsmod | grep -oE 'mali_kbase|panthor' | tr '\n' ' ')"; echo "output: $(wlr-randr | grep current | head -1)"; echo "version: $(./blackhole_gles3 --version)"; } > mali_info.txt
rm -rf m_* p_*
for fb in orbit slingshot; do for pal in stylized kipthorne faithful singularity slingshot whitehole eht; do
  d=m_${pal}_${fb}
  NCZ_BLACKHOLE_PERF_LOG=2 NCZ_FRAME_DUMP=$d timeout -k 3 22 nice -n 5 ./blackhole_gles3 --palette=$pal --flyby=$fb --seed=42 --speed=1.8 > $d.log 2>&1
  echo "$d frames=$(ls $d 2>/dev/null | grep -c png)" >> mali_info.txt
  sh $HOME/host-lock.sh refresh >/dev/null 2>&1
done; done
for p in $(./blackhole_gles3 --list-presets | cut -f1); do
  d=p_$p
  NCZ_BLACKHOLE_PERF_LOG=2 NCZ_FRAME_DUMP=$d timeout -k 3 22 nice -n 5 ./blackhole_gles3 --preset=$p --seed=42 --speed=1.5 > $d.log 2>&1
  echo "$p frames=$(ls $d 2>/dev/null | grep -c png) $(grep '\[stats\]' $d.log | tail -1 | grep -o 'steady.*')" >> mali_info.txt
  sh $HOME/host-lock.sh refresh >/dev/null 2>&1
done
echo DONE >> mali_info.txt
