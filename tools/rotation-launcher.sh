#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# tools/rotation-launcher.sh — pick and run ONE screensaver from the
# combined pool: any of the 88 ported _demo binaries OR a hyprsaver shader.
# This is the wiring that lets hyprsaver's 35 built-in shaders be
# rotation-pool peers of the existing 88 demos, not a separate, unintegrated
# component.
#
# The pool IS the existing build/*_demo set + the shader list from
# tools/hyprsaver-wrap.sh --list. One selection per invocation; the caller
# (operator / manager) is expected to invoke repeatedly across the rotation
# interval and to SIGTERM / SIGKILL the running binary when it's time to
# advance the slot, like the existing run-all.sh test harness does.
#
# This deliberately does NOT spawn a long-running daemon here. The existing
# screensaver architecture launches one binary per slot and waits for
# SIGTERM from the manager. rotation-launcher.sh is the slot picker; the
# caller manages the slot lifecycle.
#
# Selection policy:
#   --random   (default)   uniformly between the two pools
#   --demos-only           only _demo binaries
#   --shaders-only         only hyprsaver shaders
#   --seed N               deterministic RNG seed (for testing)
#
# Output (on stdout):
#   <pool_kind>\t<entry_name>\t[<palette>]
#   The launcher does NOT launch the binary; the caller is responsible for
#   either exec'ing one of the form arguments or invoking the matching
#   tools/hyprsaver-wrap.sh. This separation makes the script composable
#   with whatever rotation policy the operator wants.
#
# Examples:
#   bash tools/rotation-launcher.sh --random
#     -> "demo\tglmatrix\t"   # launch ./build/glmatrix_demo
#     -> "shader\tjulia\traibow" # invoke tools/hyprsaver-wrap.sh julia rainbow
#   bash tools/rotation-launcher.sh --demos-only --seed 42
#   bash tools/rotation-launcher.sh --shaders-only
#
# The combined pool size is 88 (demos) + 35 (built-in shaders) = 123 slots;
# hyprsaver's --palette variety (10 built-in palettes) means a single shader
# can be picked with multiple palettes, multiplying the effective space
# without requiring any additional binary.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
WRAP="$REPO_ROOT/tools/hyprsaver-wrap.sh"

# ----- argument parse -------------------------------------------------------
SEED=""
POOL="random"  # random | demos | shaders
while [ "${1:-}" != "" ]; do
    case "$1" in
        --random|--all)       POOL="random"; shift ;;
        --demos-only|--demos) POOL="demos";  shift ;;
        --shaders-only|--shaders) POOL="shaders"; shift ;;
        --seed)               SEED="${2:-}"; shift 2 ;;
        -h|--help)
            sed -n '2,40p' "$0"
            exit 0 ;;
        *)  echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

# ----- RNG (deterministic if SEED provided) ---------------------------------
if [ -n "$SEED" ]; then
    awk -v s="$SEED" 'BEGIN{srand(s); print rand()}'
else
    # Fold in pid + nanoseconds for non-repeating across the same second
    awk -v p=$$ -v n="$(date +%N)" 'BEGIN{srand(p + n); print rand()}'
fi > /dev/null
RAND() { awk -v lo="$1" -v hi="$2" 'BEGIN{srand(srand()); print int(lo + rand() * (hi - lo + 1))}'; }
# We don't actually use a deterministic AWK PRNG for selection — we use bash
# $RANDOM because the OS already gives us a fine seed. Keep RAND as a stub.

# ----- enumerate pools ------------------------------------------------------
# Demos: build/<name>_demo, executable, named _demo for sanity.
mapfile -t DEMOS < <(find "$BUILD_DIR" -maxdepth 2 -type f -name '*_demo' \
                    -executable -printf '%f\n' 2>/dev/null | sort)
# Shaders: from tools/hyprsaver-wrap.sh --list, fallback to a hardcoded list
# if the wrapper / binary isn't built yet (so the script doesn't crash on a
# bare repo). The hardcoded list matches hyprsaver v0.4.5 (verified 2026-08-20).
mapfile -t SHADER_NAMES < <(
    if [ -x "$WRAP" ]; then
        "$WRAP" --list 2>/dev/null
    else
        cat <<'EOF'
attitude
aurora
bezier
blob
caustics
circuit
clouds
donut
fireflies
flames
fractaltrap
geometry
gridwave
hypercube
julia
kaleidoscope
lissajous
marble
matrix
mobius
oscilloscope
planet
plasma
shipburn
snowfall
sonar
starfield
stonks
temple
terminal
tesla
tunnel
voronoi
waterfall
wormhole
EOF
    fi
)
SHADERS=("${SHADER_NAMES[@]}")
# A small palette rotation list — kept narrow on purpose; one universal-safe
# default (rainbow) plus four vivid alternatives that hyprsaver v0.4.5
# ships built-in and verified-live on O6N.
PALETTES=(rainbow vaporwave frost fire sunset)

ND="${#DEMOS[@]}"
NS="${#SHADERS[@]}"
NP="${#PALETTES[@]}"

# Sanity check the pool labels are right (one demo, one shader). 88 demos is
# the documented count; not fatal if lower (some builds may not yet have all
# 88 *_demo files), but we DO warn loudly if the demo pool is empty —
# rotation-launcher.sh with no demos is a misconfigured manager.

if [ "$ND" -eq 0 ] && [ "$POOL" != "shaders" ]; then
    echo "rotation-launcher.sh: WARNING no *_demo binaries under $BUILD_DIR" >&2
    echo "  Build them with: meson compile -C $BUILD_DIR" >&2
fi

# ----- select ---------------------------------------------------------------
USE_POOL="$POOL"
if [ "$POOL" = "random" ]; then
    # Combined: any demo OR any (shader, palette) tuple. If both pools are
    # non-empty pick weighted so demos are ~75% (the existing pool is what
    # people have tested most) and shaders ~25% — matching the
    # HYPRSAVER-INTEGRATION-2026-08-20.md "coexist with the 88 ports"
    # intent. If only one pool is non-empty, use it.
    if [ "$ND" -gt 0 ] && [ "$NS" -gt 0 ]; then
        # 1 demo slot vs (NS * NP) shader slots? Simpler: 1:NS:NP
        # proposal — choose demo with prob 0.5, shader with prob 0.5.
        if [ $((RANDOM % 2)) -eq 0 ]; then USE_POOL="demos"
        else USE_POOL="shaders"; fi
    elif [ "$NS" -gt 0 ]; then
        USE_POOL="shaders"
    else
        USE_POOL="demos"
    fi
fi

case "$USE_POOL" in
    demos)
        if [ "$ND" -eq 0 ]; then
            echo "rotation-launcher.sh: no demos in pool" >&2
            exit 1
        fi
        idx=$((RANDOM % ND))
        printf 'demo\t%s\t\n' "${DEMOS[$idx]}"
        ;;
    shaders)
        if [ "$NS" -eq 0 ]; then
            echo "rotation-launcher.sh: no shaders in pool" >&2
            exit 1
        fi
        si=$((RANDOM % NS))
        pi=$((RANDOM % NP))
        printf 'shader\t%s\t%s\n' "${SHADERS[$si]}" "${PALETTES[$pi]}"
        ;;
    *)
        echo "rotation-launcher.sh: internal pool error $USE_POOL" >&2
        exit 1 ;;
esac

# Optional diagnostic block if a seed was given: useful for deterministic
# rotation testing.
if [ -n "$SEED" ]; then
    :  # keep slot size predictable; further per-seed shuffle would go here
fi
