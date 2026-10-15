#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Headless test: the prompt suspends the saver and dismissing it resumes it.
#   * the feed advertises LFS_FLAG_PAUSE in HELLO;
#   * on PAUSE no frame reaches the lock and the renderer process is stopped
#     (state T): no GL work, no frame path;
#   * on RESUME frames stream again from the same renderer (same pid);
#   * a renderer killed while suspended is restarted on RESUME (the feed and
#     the lock never wait on a stopped or dead child).
# usage: test-feed-pause.sh SRC_DIR BIN_DIR [HACK_ID]
set -u
SRC=${1:?usage}; BIN=${2:?usage}; HACK=${3:-hyprsaver_starfield_gles3}
[ -x "$BIN/$HACK" ] || { echo "SKIP: $BIN/$HACK not built"; exit 77; }
[ -n "${TMPDIR:-}" ] && mkdir -p "$TMPDIR" 2>/dev/null
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/xdg" "$T/schemas" "$T/empty"; chmod 700 "$T/xdg"
cp "$SRC/config/dev.ncz.screensaver.gschema.xml" "$T/schemas/"; glib-compile-schemas "$T/schemas"
export XDG_RUNTIME_DIR="$T/xdg" XDG_CONFIG_HOME="$T" GSETTINGS_BACKEND=keyfile GSETTINGS_SCHEMA_DIR="$T/schemas"
export NCZ_ALLOW_SOFTWARE=1 LIBGL_ALWAYS_SOFTWARE=1 NCZ_FEED_GUARD_ROOT="$T/empty" NCZ_FEED_TEMP_MC=40000
export NCZ_SCREENSAVER_DIRS="$BIN" NCZ_SCREENSAVER_CATALOG="$SRC/assets/screensaver-chooser/hacks.tsv"
export NCZ_SCREENSAVER_LAUNCHER="$SRC/launcher/ncz-screensaver"
gsettings set dev.ncz.screensaver mode one
gsettings set dev.ncz.screensaver hack-id "$HACK"
gsettings set dev.ncz.screensaver verify-render false
FAIL=0
renderer_pids() { pgrep -f "^$BIN/$HACK" 2>/dev/null; }
run_case() { # name kill_while_paused(0|1)
    local name=$1 kill=$2 sock="$T/xdg/l-$1.sock" mark="$T/paused-$1"
    python3 "$SRC/launcher/tests/feed-mock-lock.py" --socket "$sock" --width 320 --height 240 \
        --frames 60 --pause-at 15 --pause-s 3 --pause-marker "$mark" \
        --result "$T/m-$name.json" --timeout 60 &
    local mock=$!
    for _ in $(seq 1 100); do [ -S "$sock" ] && break; sleep 0.05; done
    SINGULARITY_LOCK_RENDERER_SOCKET="$sock" NCZ_SAVER_LOCK_FPS=20 \
        timeout 60 python3 "$SRC/launcher/ncz-screensaver-feed" > "$T/f-$name.out" 2>&1 &
    local feed=$!
    for _ in $(seq 1 200); do [ -e "$mark" ] && break; sleep 0.05; done
    # The marker file is created when the mock lock SENDS LFS_PAUSE; the feed
    # delivers SIGSTOP to the renderer process group asynchronously, so the
    # marker can exist for a brief window before the renderer is actually in
    # state 'T'. Poll for the renderer state instead of relying on a fixed
    # sleep so the test races neither delivery of the signal nor expiry of
    # the pause window before the renderer is observed stopped.
    local p st before
    for _ in $(seq 1 200); do
        p=$(renderer_pids | head -1)
        if [ -n "$p" ]; then
            st=$(ps -o stat= -p "$p" 2>/dev/null | cut -c1)
            [ "$st" = T ] && break
        fi
        sleep 0.05
    done
    before=$p
    if [ -z "$p" ]; then echo "FAIL: $name: no renderer PID observed"; FAIL=1
    elif [ "$st" = T ]; then echo "PASS: $name: renderer $p is stopped while paused"
    else echo "FAIL: $name: renderer state '$st' while paused"; FAIL=1
    fi
    if [ "$kill" = 1 ] && [ -n "$p" ]; then kill -9 "$p"; echo "  killed renderer $p while paused"; fi
    wait "$mock"; local mrc=$?
    kill "$feed" 2>/dev/null; wait "$feed" 2>/dev/null
    local res; res=$(cat "$T/m-$name.json" 2>/dev/null)
    if [ "$mrc" = 0 ]; then echo "PASS: $name: no frames while paused, frames after RESUME"; else echo "FAIL: $name: $res"; FAIL=1; fi
    grep -aE "PAUSE|RESUME|started" "$T/f-$name.out" | sed 's/^/    /'
    if [ "$kill" = 0 ]; then
        grep -c "started $HACK" "$T/f-$name.out" | grep -qx 1 && echo "PASS: $name: resumed the same renderer (no restart)" || { echo "FAIL: $name: renderer was restarted"; FAIL=1; }
    else
        [ "$(grep -c "started $HACK" "$T/f-$name.out")" -ge 2 ] && echo "PASS: $name: dead renderer restarted on RESUME" || { echo "FAIL: $name: dead renderer not restarted"; FAIL=1; }
    fi
}
run_case pause-resume 0
run_case killed-while-paused 1
[ "$FAIL" = 0 ] && echo "RESULT: PASS" || echo "RESULT: FAIL"
exit $FAIL
