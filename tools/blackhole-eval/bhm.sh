#!/bin/bash
# runs on the host in ~/bhm : palette x flyby matrix with frame dumps
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
cd ~/bhm
if pgrep -u $(id -u) -f _gles3 >/dev/null; then echo "BUSY: another _gles3 process runs; not touching it (hold the host lock first)"; exit 9; fi
systemctl --user stop ncz-screensaver-idled 2>/dev/null; ncz-screensaver stop >/dev/null 2>&1; sleep 1
rm -rf m_*; 
DUR=${DUR:-24}
for fb in ${FLYBYS:-orbit slingshot}; do for pal in ${PALS:-stylized kipthorne faithful singularity slingshot whitehole eht}; do
  d=m_${pal}_${fb}
  NCZ_BLACKHOLE_PERF_LOG=1 NCZ_FRAME_DUMP=$d timeout -s TERM -k 3 $DUR ./blackhole_gles3 --palette=$pal --flyby=$fb --seed=42 --speed=${SPEED:-1.8} >$d.log 2>&1
  echo "$d frames=$(ls $d 2>/dev/null | grep -c png) err=$(grep -ci 'compile\|link:\|error:' $d.log)"
done; done
systemctl --user start ncz-screensaver-idled 2>/dev/null
