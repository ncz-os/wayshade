#!/bin/sh
sh $HOME/host-lock.sh acquire test lead1 "render cap check (5 s)" --expect 2 --wait 900 || exit 9
trap 'sh $HOME/host-lock.sh release' EXIT INT TERM
cd $HOME/a52/run || exit 1
for v in $(cat /proc/$(pgrep -u $(id -u) -o labwc)/environ | tr '\0' '\n' | grep -E '^(__EGL|VK_|LD_LIBRARY|LIBGL|MESA|GBM|EGL_|GALLIUM|PAN|MALI|CIX)' | sed 's/ /%20/g'); do export "$(echo $v | sed 's/%20/ /g')"; done
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
timeout -k 2 6 ./blackhole_gles3 --seed=1 2>&1 | grep -E "gpu class|render size" | cut -c1-200
