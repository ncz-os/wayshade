export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
sh $HOME/host-lock.sh acquire test lead1 "Black Hole preset renders" --expect 8 --wait 300 || exit 9; trap "sh $HOME/host-lock.sh release" EXIT
systemctl --user stop ncz-screensaver-idled 2>/dev/null; ncz-screensaver stop >/dev/null 2>&1; sleep 1
rm -rf ~/bhp; tar -C ~ -xf /tmp/bhp.tar; cd ~/bhp
./blackhole_gles3 --list-presets | cut -f1-3
for p in $(./blackhole_gles3 --list-presets | cut -f1); do
  d=p_$p; rm -rf $d
  NCZ_BLACKHOLE_PERF_LOG=2 NCZ_FRAME_DUMP=$d timeout -k 3 ${DUR:-22} ./blackhole_gles3 --preset=$p --seed=42 --speed=${SPEED:-1.5} > $d.log 2>&1
  echo "$p frames=$(ls $d 2>/dev/null | grep -c png) $(grep -c 'opts' $d.log) warn; $(grep '\[stats\]' $d.log | tail -1 | grep -o 'steady.*')"
done
systemctl --user start ncz-screensaver-idled
