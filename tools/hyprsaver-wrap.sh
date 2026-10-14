#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# tools/hyprsaver-wrap.sh — launch ONE hyprsaver session, pinned to a single
# shader + palette, fullscreen on the live Wayland session.
#
# This is the "rotation slot" companion to *_demo: it spawns the rust binary
# as a daemon-mode overlay (wlr-layer-shell LAYER_OVERLAY, all anchors, with
# keyboard interactivity = Exclusive so input dismisses the screensaver like
# every other screensaver binary does — matches the demo harness's
# wl-screenhack.c LAYER_OVERLAY semantics). It is the singular *additive*
# addition this script makes vs. the systemd upstream; everything else
# (the unmodified Rust binary, wlr-layer-shell protocol selection, OpenGL ES
# 2 + glow rendering pipeline, frame-callback loop, dismiss-on-key / -mouse
# handling, fade-in / fade-out) is hyprsaver's stock code.
#
# Why a wrapper rather than patching main.rs:
#   * Patches to hyprsaver's source would diverge from upstream and lose the
#     ability to re-vendor. None of the Hyprland-specific references in
#     hyprsaver's source are *behavioral* — they live in:
#       - src/main.rs:39-68  --help / about strings
#       - src/shaders.rs:208 default shaders dir ($XDG_CONFIG_HOME/hypr/...)
#       - src/preview.rs:1005 "$XDG_CONFIG_HOME/hypr/hyprsaver.toml"
#     None of those resolve to actual Hyprland IPC or wlroots extensions
#     beyond the standard wlr-layer-shell / xdg-shell protocols. Patching
#     help text is churn, not behavior.
#   * The PID-file guard at $XDG_RUNTIME_DIR/hyprsaver.pid exists because
#     hyprsaver is designed to be a long-running "hypridle -> hyprsaver"-
#     managed daemon. We want to launch-and-replace like _demo binaries;
#     a wrapper that does `rm -f` of a stale lock before exec is the
#     correct fit.
#
# Usage:
#   tools/hyprsaver-wrap.sh <shader> [palette]
#       Launch hyprsaver pinned to <shader> with the given palette name
#       (must be one of hyprsaver --list-palettes; "rainbow" is the safe
#       universal choice when unsure).
#
#   tools/hyprsaver-wrap.sh --list
#       Print "<shader> <palette>" pairs (one per line) of (shader, palette)
#       combinations that are known-good for live compositors that lack
#       the per-shader uniform set hyprsaver's preview path uses.
#
#   tools/hyprsaver-wrap.sh --quit
#       Send SIGTERM to any running hyprsaver instance and exit
#       (hyprsaver's own --quit does the same).
#
# Configuration:
#   HYPRSAVER_BIN  override the binary path (default: build/hyprsaver/hyprsaver)
#
# Environment requirements on the running compositor session:
#   WAYLAND_DISPLAY         a Wayland socket (set by the session)
#   XDG_RUNTIME_DIR         runtime dir (set by the session, /run/user/UID)
#   EGL/GLES libs reachable -- Mali on the O6N; labwc on .66.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HYPRSAVER_BIN="${HYPRSAVER_BIN:-$REPO_ROOT/build/hyprsaver/hyprsaver}"

# PID file hyprsaver creates. Drop any stale lock from a crashed prior
# instance so a rotation slot can always acquire one. (hyprsaver's own
# startup creates this fresh on every successful run; --quit is also
# graceful.) We use the runtime dir as it does:
PIDDIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
PIDFILE="$PIDDIR/hyprsaver.pid"

# Operator commands
if [ "${1:-}" = "--quit" ]; then
    if [ -x "$HYPRSAVER_BIN" ]; then
        "$HYPRSAVER_BIN" --quit 2>/dev/null || true
    fi
    rm -f "$PIDFILE"
    exit 0
fi
if [ "${1:-}" = "--list" ]; then
    if [ ! -x "$HYPRSAVER_BIN" ]; then
        echo "hyprsaver-wrap.sh: cannot list -- $HYPRSAVER_BIN missing; build with tools/build-hyprsaver.sh first" >&2
        exit 1
    fi
    HYPRSAVER_SHADERS=$("$HYPRSAVER_BIN" --list-shaders 2>/dev/null \
        | awk '/^Built-in shaders:/{flag=1;next} /^User shaders/{flag=0} flag && NF{print $1}' \
        | sed '/^$/d')
    echo "$HYPRSAVER_SHADERS"
    exit 0
fi

if [ -z "${1:-}" ]; then
    echo "hyprsaver-wrap.sh: missing <shader> argument" >&2
    echo "  usage: $0 <shader> [palette]" >&2
    echo "         $0 --list" >&2
    echo "         $0 --quit" >&2
    exit 2
fi

SHADER="$1"
PALETTE="${2:-rainbow}"

if [ ! -x "$HYPRSAVER_BIN" ]; then
    echo "hyprsaver-wrap.sh: binary missing at $HYPRSAVER_BIN" >&2
    echo "  build with: tools/build-hyprsaver.sh" >&2
    exit 1
fi

# Reset any stale PID lock from a crashed run.
rm -f "$PIDFILE"

# Launch detached so the rotation script can fork-and-forget without holding
# a shell pipe open. Hyprsaver accepts SIGTERM cleanly (it installs
# signal-hook handlers) and tears down all wl_surface / EGL / GL resources
# in the right order.
exec "$HYPRSAVER_BIN" -c /dev/null \
    --shader "$SHADER" --palette "$PALETTE" \
    >/dev/null 2>"$PIDDIR/hyprsaver-${SHADER}-${PALETTE}.log"
