#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# tools/build-hyprsaver.sh — build hyprsaver from the vendored vendor/hyprsaver
# source tree into build/hyprsaver/hyprsaver, for the ncz-screensavers
# HYPRSAVER-INTEGRATION-2026-08-20 work.
#
# This is deliberately NOT a meson target. hyprsaver is a Rust binary with its
# own toolchain (cargo) and its own dep graph (35+ crates including wayland-
# protocols-wlr, glow, smithay-client-toolkit, calloop, khronos-egl, glutin).
# Driving it from meson would mean importing cargo dependency resolution into
# meson and rerunning it whenever a Cargo.lock changes, which is the wrong
# abstraction. The vendor/hyprsaver/ tree is plain cargo; a tiny shell wrapper
# is the right boundary.
#
# Re-runnable; checks whether target/release/hyprsaver is fresher than the
# last-modified source file under vendor/hyprsaver/src/. If yes, skips rebuild
# (so a `meson compile -C build && tools/build-hyprsaver.sh` run is fast on
# cached builds).
#
# Output: build/hyprsaver/hyprsaver (aarch64 ELF, statically resizable).
#
# Pre-reqs: Rust toolchain (rustc 1.88+, cargo) on PATH. Debian/Ubuntu:
#   apt-get install -y rustc cargo
# On NCZ-OS / .66 this already exists; verified working 2026-08-20.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$REPO_ROOT/vendor/hyprsaver"
OUT_DIR="$REPO_ROOT/build/hyprsaver"
OUT_BIN="$OUT_DIR/hyprsaver"

if [ ! -d "$VENDOR_DIR" ]; then
    echo "build-hyprsaver.sh: missing vendored source at $VENDOR_DIR" >&2
    echo "  run: git clone --depth 1 https://github.com/maravexa/hyprsaver.git $VENDOR_DIR" >&2
    exit 1
fi
if [ ! -f "$VENDOR_DIR/Cargo.toml" ]; then
    echo "build-hyprsaver.sh: $VENDOR_DIR/Cargo.toml missing — vendored tree is corrupt" >&2
    exit 1
fi

if ! command -v cargo >/dev/null 2>&1; then
    echo "build-hyprsaver.sh: cargo not on PATH; install rustup + cargo" >&2
    exit 1
fi

# Cache invalidation: rebuild if any tracked source/config file is newer.
need_rebuild=1
if [ -f "$OUT_BIN" ]; then
    if [ -z "$(find "$VENDOR_DIR" \
        \( -path "$VENDOR_DIR/target" -o -path "$VENDOR_DIR/examples/palettes" \) -prune -o \
        -type f \( -name '*.rs' -o -name '*.toml' -o -name '*.lock' -o -name '*.frag' -o -name 'build.rs' \) -print -quit \
        -newer "$OUT_BIN" 2>/dev/null)" ]; then
        need_rebuild=0
    fi
fi

if [ "$need_rebuild" = "1" ]; then
    echo "build-hyprsaver.sh: building (release)"
    (
        cd "$VENDOR_DIR"
        # --locked refuses to update Cargo.lock unless deps are present in
        # cache; we accept that pulling deps from crates.io is part of the
        # first build.
        cargo build --release --jobs "$(nproc 2>/dev/null || echo 4)"
    )
fi

mkdir -p "$OUT_DIR"
cp -f "$VENDOR_DIR/target/release/hyprsaver" "$OUT_BIN"
chmod 0755 "$OUT_BIN"
echo "build-hyprsaver.sh: OK -> $OUT_BIN ($(stat -c%s "$OUT_BIN") bytes, $(file -b "$OUT_BIN" | cut -d, -f2-))"
