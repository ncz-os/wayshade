export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
cd ~/bhm
pgrep -af "host-test|_gles3" | grep -v pgrep | grep -v "chperf" && echo BUSY
systemctl --user stop ncz-screensaver-idled 2>/dev/null; ncz-screensaver stop >/dev/null 2>&1; sleep 1
run() { n=$1; envs=$2; shift; shift
  ( for i in $(seq 1 22); do echo "$(cat /sys/class/drm/card1/device/gpu_busy_percent) $(grep '\*' /sys/class/drm/card1/device/pp_dpm_sclk | tr -d '\n')"; sleep 2; done > perf_$n.gpu ) &
  env $envs NCZ_BLACKHOLE_PERF_LOG=2 timeout -s TERM -k 3 45 ./blackhole_gles3 --seed=42 "$@" > perf_$n.log 2>&1
  wait
  echo "== $n"; grep "pace frames" perf_$n.log | tail -2 | cut -c1-220; grep "hitches" perf_$n.log | head -3; grep -c "adapt" perf_$n.log; sort perf_$n.gpu | uniq -c | sort -rn | head -3
}
run before_diag60 NCZ_HARNESS_DIAG=1 --adaptive=false
run static_nodiag X=1 --adaptive=false

