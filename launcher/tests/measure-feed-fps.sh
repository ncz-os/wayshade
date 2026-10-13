#!/bin/bash
# Measure the feed's steady-state frame rate (freeze=never) to confirm the
# lock fps cap is honoured by the child renderer.
set -u
SRC=${1:?}; BIN=${2:?}; HACK=${3:-blackhole_gles3}
[ -n "${TMPDIR:-}" ] && mkdir -p "$TMPDIR" 2>/dev/null
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/xdg" "$T/schemas" "$T/empty"; chmod 700 "$T/xdg"
cp "$SRC/config/dev.ncz.screensaver.gschema.xml" "$T/schemas/"
glib-compile-schemas "$T/schemas"
export XDG_RUNTIME_DIR="$T/xdg" XDG_CONFIG_HOME="$T/xdg/cfg"
export GSETTINGS_BACKEND=keyfile GSETTINGS_SCHEMA_DIR="$T/schemas"
export NCZ_ALLOW_SOFTWARE=1 LIBGL_ALWAYS_SOFTWARE=1 NCZ_FEED_GUARD_ROOT="$T/empty"
export NCZ_SCREENSAVER_DIRS="$BIN" NCZ_SCREENSAVER_CATALOG="$SRC/assets/screensaver-chooser/hacks.tsv"
export NCZ_SCREENSAVER_LAUNCHER="$SRC/launcher/ncz-screensaver"
gsettings set dev.ncz.screensaver mode one
gsettings set dev.ncz.screensaver hack-id "$HACK"
gsettings set dev.ncz.screensaver verify-render false

S=$T/xdg/l.sock
python3 "$SRC/launcher/tests/feed-mock-lock.py" --socket "$S" --width 320 --height 240 \
    --frames 9999 --result "$T/r.json" --timeout 5 >/dev/null 2>&1 &
mock=$!
for _ in $(seq 1 100); do [ -S "$S" ] && break; sleep 0.05; done
start=$(date +%s.%N)
NCZ_SAVER_LOCK_FPS=8 NCZ_SAVER_RENDER_DIV=2 NCZ_SAVER_FREEZE=never \
  SINGULARITY_LOCK_RENDERER_SOCKET="$S" timeout 12 python3 "$SRC/launcher/ncz-screensaver-feed" >/dev/null 2>&1
wait $mock 2>/dev/null
end=$(date +%s.%N)
python3 - "$T/r.json" "$start" "$end" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
dt=float(sys.argv[3])-float(sys.argv[2])
print("frames=%d elapsed=%.2fs fps=%.1f errors=%s" % (r["frames"], dt, r["frames"]/dt, r["errors"]))
PY
exit 0
