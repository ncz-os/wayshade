#!/bin/sh
# clean timing (no frame dump, no readbacks): default and every preset, 26 s each
sh $HOME/host-lock.sh acquire test lead1 "Black Hole weak-class timing" --expect 8 --wait 600 || exit 9
trap 'sh $HOME/host-lock.sh release; systemctl --user start ncz-screensaver-idled 2>/dev/null' EXIT INT TERM
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
systemctl --user stop ncz-screensaver-idled 2>/dev/null; ncz-screensaver stop >/dev/null 2>&1; sleep 1
rm -rf $HOME/bhw && tar -C $HOME -xf /tmp/bhw.tar && cd $HOME/bhw || exit 1
export NCZ_PRESET_DIR=$HOME/bhw/presets
run() { n=$1; shift; o=$(timeout -k 3 26 nice -n 5 ./blackhole_gles3 --seed=42 "$@" 2>&1); echo "$n | $(echo "$o" | grep -o 'gpu class [a-z]*' | head -1) | $(echo "$o" | grep -o 'render size [0-9x]* ([^)]*' | head -1) | $(echo "$o" | grep '^\[stats\]' | tail -1 | grep -o 'steady.*')"; sh $HOME/host-lock.sh refresh >/dev/null 2>&1; }
run default
for p in $(./blackhole_gles3 --list-presets | cut -f1); do run $p --preset=$p; done
