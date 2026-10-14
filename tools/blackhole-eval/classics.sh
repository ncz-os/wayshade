export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
if pgrep -f "host-test-agent" >/dev/null; then echo "BUSY host-test"; exit 1; fi
systemctl --user stop ncz-screensaver-idled 2>/dev/null; ncz-screensaver stop >/dev/null 2>&1; sleep 1
cd ~/x40; export NCZ_SHADER_DIR=$HOME/x40/usr/share/ncz-screensavers/shaders
for h in voronoi projectiveplane klein hypertorus cubestorm hexstrut crackberg cityflow geodesic gravitywell noof gibson; do
  o=$(timeout -k 2 12 nice -n 5 ./usr/lib/ncz-screensavers/${h}_gles3 2>&1 | grep '^\[stats\]' | tail -1)
  echo "$h $o" | sed 's/first_frame/ff/' | cut -c1-250
done
systemctl --user start ncz-screensaver-idled
