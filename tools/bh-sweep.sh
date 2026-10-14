#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# tools/bh-sweep.sh — per-launch capture sweep on build-host.
#
# Runs N seeded launches of blackhole_gles3, each captured to its own
# per-launch directory with the [diag] stderr beside the PNGs. Frames
# 4, 60, 120, 180 land (≈3s of runtime at 60fps) which is enough to
# see the trajectory change visibly between frames.
#
# Usage (run ON build-host):
#   bash tools/bh-sweep.sh <runs_root> <seed1> [<seed2> ...]
#
# Output layout per launch:
#   <runs_root>/launch_<seed>/
#       launch.stderr        # [diag] lines including the launch params
#       frame_00000004.png   # ~t=0.067s
#       frame_0000003c.png   # ~t=1.0s
#       frame_00000078.png   # ~t=2.0s
#       frame_000000b4.png   # ~t=3.0s
#
# Cleanup gate at the end verifies nothing is left running.

set -u
RUNS_ROOT="${1:?usage: bh-sweep.sh RUNS_ROOT SEED [SEED...]}"
shift
SEEDS=("$@")
if [ ${#SEEDS[@]} -lt 1 ]; then
  echo "usage: bh-sweep.sh RUNS_ROOT SEED [SEED...]" >&2
  exit 2
fi

BIN="${HOME}/ncz-screensavers/build/blackhole_gles3"
if [ ! -x "$BIN" ]; then
  echo "bh-sweep: missing binary at $BIN" >&2
  exit 3
fi

mkdir -p "$RUNS_ROOT"

# Disk headroom check.
HEADROOM_KB=$(df -k "$HOME/build-tmp" | awk 'NR==2 {print $4}')
if [ "${HEADROOM_KB:-0}" -lt 524288 ]; then
  echo "bh-sweep: WARNING low disk on ~/build-tmp: ${HEADROOM_KB}KB free" >&2
fi

# Wayland session.
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

CLEAN=0
for SEED in "${SEEDS[@]}"; do
  LAUNCH_DIR="$RUNS_ROOT/launch_${SEED}"
  mkdir -p "$LAUNCH_DIR"
  echo "=== bh-sweep: seed=$SEED dir=$LAUNCH_DIR ===" >&2
  # Hard timeout on the harness: 12s wall. The harness exits cleanly on
  # SIGTERM via on_signal. Floor hardware (Intel UHD) renders at well
  # below 60fps; at ~25fps we get ~300 frames in 12s, which spans at
  # least frames 4, 60, 120, 180, 240 of the harness's "report every
  # 60th frame" cadence. That is enough to see the trajectory move.
  NCZ_BLACKHOLE_SEED="$SEED" NCZ_FRAME_DUMP="$LAUNCH_DIR" \
    timeout --kill-after=2 12 "$BIN" >"$LAUNCH_DIR/launch.stderr" 2>&1
  rc=$?
  echo "    exit=$rc" >&2
  # Verify the launch actually ran: must have a [diag] line AND at least
  # one PNG.
  if [ ! -s "$LAUNCH_DIR/launch.stderr" ]; then
    echo "    FAIL: no stderr captured" >&2
    CLEAN=$((CLEAN+1))
    continue
  fi
  PNG_COUNT=$(find "$LAUNCH_DIR" -maxdepth 1 -name 'frame_*.png' -size +0c | wc -l)
  echo "    pngs=$PNG_COUNT" >&2
  if [ "$PNG_COUNT" -lt 1 ]; then
    echo "    FAIL: no PNGs written" >&2
    CLEAN=$((CLEAN+1))
  fi
done

# Cleanup gate.
REMAINING=$(pgrep -af '_gles3' | grep -v '[p]grep' || true)
if [ -n "$REMAINING" ]; then
  echo "bh-sweep: WARNING residual _gles3 processes:" >&2
  echo "$REMAINING" >&2
else
  echo "bh-sweep: clean — no _gles3 processes remaining" >&2
fi
exit "$CLEAN"
