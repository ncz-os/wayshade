#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# tools/build-artifact-gate.sh - validate the build tree holds the installable
# artefacts the ncz-screensavers and ncz-screensavers-plugin arm64 packages
# need. The gate is a meaningful post-build test that the integration branch
# was missing: it enforces the linkable shape (the hack binaries, calibrator,
# idled, and the libscreensaver.so the Singularity Settings plugin loads when
# that plugin is enabled) and the installable shape (the installed file tree
# plus the plugin .deb) without needing a live Wayland session or an actual
# package install.
#
# usage: tools/build-artifact-gate.sh [BUILD_DIR] [WANT_MACHINE] [WANT_PLUGIN]
#   BUILD_DIR      defaults to ./build
#   WANT_MACHINE   the ELF machine the build tree is expected to hold, as the
#                  `file` spelling: "ARM aarch64" or "x86-64". Defaults to the
#                  machine this script is running on, so an x86 test run and
#                  the arm64 packaging run on the arm64 build host each check their own tree.
#   WANT_PLUGIN    "required" (default) or "skipped". Pass "skipped" when the
#                  build was configured with -Dsingularity-plugin=disabled, so
#                  the gate validates the savers and launcher without demanding
#                  a plugin that was never asked for.
#
# The ELF-class and machine checks are what catch a half-finished or
# mismatched build (a stale x86 object left in the arm64 tree, a 32-bit
# link). Anchoring them to the *expected* machine rather than a hard-coded
# aarch64 keeps that protection on both architectures instead of making the
# gate unrunnable on the one it was written for.
# exit status: 0 on success, non-zero on the first failure.
#
# Two halves:
#   1. linkable shape — binaries the harness / launcher / plugin load at run
#      time. ELF class must be 64-bit and the machine must be the one this
#      build was configured for; the plugin .so must be dynamically
#      linked and depend on libsingularity (skipped when the plugin option
#      is disabled).
#   2. installable shape — `meson install --destdir` produces a tree that
#      matches what the .deb packages ship.
#
# The "per-GPU weak/strong labels" and "PRIME explanation" UX paths in the
# launcher's command output are exercised separately by
# tools/tests/test_gpu_class.py; this gate is the build-system counterpart.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-$ROOT/build}
DESTDIR=${DESTDIR:-}

case $(uname -m) in
    aarch64|arm64) SELF_MACHINE="ARM aarch64" ;;
    x86_64|amd64)  SELF_MACHINE="x86-64" ;;
    *)             SELF_MACHINE=$(uname -m) ;;
esac
WANT_MACHINE=${2:-$SELF_MACHINE}
WANT_PLUGIN=${3:-required}

case "$WANT_MACHINE" in
    *aarch64*|*arm64*|*x86-64*|*amd64*) ;;
    *) printf 'build-artifact-gate: unrecognised WANT_MACHINE %s\n' "$WANT_MACHINE" >&2; exit 2 ;;
esac
case "$WANT_PLUGIN" in
    required|skipped) ;;
    *) printf 'build-artifact-gate: WANT_PLUGIN must be required or skipped\n' >&2; exit 2 ;;
esac
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

if [ ! -d "$BUILD" ]; then
    echo "build-artifact-gate: no build directory at $BUILD" >&2
    exit 2
fi

fail=0
note() {
    printf '  - %s\n' "$*"
}

arch() {
    # EI_CLASS: 1 = 32-bit, 2 = 64-bit. The class byte is at offset 4 of e_ident.
    # `od` zero-pads single-byte fields, so a direct string compare with "2"
    # never matches; we strip the pad and compare the trailing byte.
    s=$(od -An -N1 -j4 "$1" | tr -d ' \n')
    case "$s" in
        2|02|002|0002|00002|000002) echo 2 ;;
        1|01|001|0001|00001|000001) echo 1 ;;
        *) echo "$s" ;;
    esac
}

check_linkable_bin() {
    label=$1; path=$2
    if [ ! -f "$path" ]; then
        printf 'FAIL %s: missing %s\n' "$label" "$path" >&2
        fail=$((fail + 1))
        return
    fi
    a=$(arch "$path")
    if [ "$a" != "2" ]; then
        printf 'FAIL %s: %s is not a 64-bit ELF (class=%s)\n' "$label" "$path" "$a" >&2
        fail=$((fail + 1))
        return
    fi
    if ! file "$path" | grep -q "$WANT_MACHINE"; then
        printf 'FAIL %s: %s is not %s\n' "$label" "$path" "$WANT_MACHINE" >&2
        fail=$((fail + 1))
        return
    fi
}

# The ship set comes from ncz_ship_bins in the top-level meson.build; the
# install tree contains exactly that many <hack>_gles3 executables plus
# their corresponding wrapper shims. Extract the count from meson.build so
# the gate does not need a separate constant.
SHIP_COUNT=$(awk '
    /ncz_ship_bins = \[/ { inlist = 1; next }
    inlist && /^\s*\],?$/ { inlist = 0 }
    inlist && /_gles3/ { n++ }
    END { print n + 0 }
' "$ROOT/meson.build")
if [ "$SHIP_COUNT" -lt 74 ]; then
    # 74 is the operator-ratified minimum after 0.8.5 added magmasimplex to the
    # 73-entry Cygnus 0.8.2 cohort (itself 79 minus universeball, gravitywell,
    # gridwave, iridescence, stratus and the duplicate neongravity-0). The
    # 2026-10-05 neongravity supersession then swapped the shipped third-party
    # neongravity-1 for the in-house neongravity scene, one for one, so the
    # floor is unchanged. We refuse to ship below this so the gate cannot be
    # silently weakened.
    printf 'FAIL ship set: %s, expected >= 74\n' "$SHIP_COUNT" >&2
    fail=$((fail + 1))
fi
note "ship set declared in meson.build: $SHIP_COUNT binaries"
check_linkable_bin "calibrator" "$BUILD/launcher/ncz-screensaver-calibrate"
note "calibrator: OK"
check_linkable_bin "idled"      "$BUILD/launcher/ncz-screensaver-idled"
note "idled: OK"
check_linkable_bin "blackhole"  "$BUILD/blackhole_gles3"
note "blackhole_gles3: OK"
check_linkable_bin "hyprsaver"  "$BUILD/hyprsaver_aurora_gles3"
note "hyprsaver_aurora_gles3: OK"
check_linkable_bin "xshadertoy" "$BUILD/xshadertoy_starnest_gles3"
note "xshadertoy_starnest_gles3: OK"

PLUGIN_SO=$BUILD/plugin/screensaver/libscreensaver.so
if [ "$WANT_PLUGIN" = "skipped" ]; then
    note "plugin checks skipped (build configured with -Dsingularity-plugin=disabled)"
elif [ ! -f "$PLUGIN_SO" ]; then
    printf 'FAIL plugin: libscreensaver.so missing\n' >&2
    fail=$((fail + 1))
elif ! file "$PLUGIN_SO" | grep -q "$WANT_MACHINE"; then
    printf 'FAIL plugin: libscreensaver.so is not %s\n' "$WANT_MACHINE" >&2
    fail=$((fail + 1))
elif ! file "$PLUGIN_SO" | grep -q "dynamically linked"; then
    printf 'FAIL plugin: libscreensaver.so is not dynamically linked\n' >&2
    fail=$((fail + 1))
elif ! ldd "$PLUGIN_SO" 2>/dev/null | grep -q "libsingularity"; then
    printf 'FAIL plugin: libscreensaver.so does not link libsingularity\n' >&2
    fail=$((fail + 1))
else
    note "libscreensaver.so: linked, $WANT_MACHINE, depends on libsingularity"
fi

# ---- installable shape ------------------------------------------------------
echo "== installable shape =="
if [ -z "$DESTDIR" ]; then
    DESTDIR="$WORK/dest"
    meson install -C "$BUILD" --destdir "$DESTDIR" >"$WORK/install.log" 2>&1 \
        || { cat "$WORK/install.log" >&2; exit 3; }
fi
# The Debian rules build with -Dprefix=/usr; meson-lint shows the build was
# configured with /usr/local by default. Probe both layouts so the gate is
# reusable from a Fedora arm64 build host and from a Debian/Ubuntu arm64 host.
find_path() {
    for p in "$@"; do
        if [ -e "$p" ]; then
            printf '%s' "$p"
            return 0
        fi
    done
    return 1
}
LIB_DIR=$(find_path \
    "$DESTDIR/usr/local/lib/ncz-screensavers" \
    "$DESTDIR/usr/lib/ncz-screensavers") \
    || { printf 'FAIL install tree: no ncz-screensavers hack lib dir under %s\n' "$DESTDIR" >&2; fail=$((fail + 1)); LIB_DIR=""; }
BIN_DIR=$(find_path \
    "$DESTDIR/usr/local/bin" \
    "$DESTDIR/usr/bin")
LIBEXEC_DIR=$(find_path \
    "$DESTDIR/usr/local/libexec" \
    "$DESTDIR/usr/libexec")
SYSTEMD_DIR=$(find_path \
    "$DESTDIR/usr/local/lib/systemd/user" \
    "$DESTDIR/usr/lib/systemd/user")
SHARE_DIR=$(find_path \
    "$DESTDIR/usr/local/share/ncz-screensavers" \
    "$DESTDIR/usr/share/ncz-screensavers")

if [ -n "$LIB_DIR" ]; then
    SHIPPED_BINS=$(find "$LIB_DIR" -maxdepth 1 -type f -executable | wc -l)
    if [ "$SHIPPED_BINS" -lt "$SHIP_COUNT" ]; then
        printf 'FAIL install tree: shipped %s, expected %s\n' "$SHIPPED_BINS" "$SHIP_COUNT" >&2
        fail=$((fail + 1))
    else
        note "install tree shipped binaries: $SHIPPED_BINS (>= ship set $SHIP_COUNT)"
    fi
fi
if [ -f "$BIN_DIR/ncz-screensaver" ]; then
    note "ncz-screensaver script: installed"
else
    printf 'FAIL ncz-screensaver: not installed in %s\n' "$BIN_DIR" >&2; fail=$((fail + 1))
fi
if [ -x "$LIBEXEC_DIR/ncz-screensavers/ncz-screensaver-calibrate" ]; then
    note "calibrate: OK"
else
    printf 'FAIL calibrate: not installed\n' >&2; fail=$((fail + 1))
fi
if [ -x "$LIBEXEC_DIR/ncz-screensaver-idled" ]; then
    note "idled (installed): OK"
else
    printf 'FAIL idled: not installed in %s\n' "$LIBEXEC_DIR" >&2; fail=$((fail + 1))
fi
if [ -f "$SYSTEMD_DIR/ncz-screensaver-idled.service" ]; then
    note "idled service unit: installed"
else
    printf 'FAIL idled.service: not installed in %s\n' "$SYSTEMD_DIR" >&2; fail=$((fail + 1))
fi
if [ "$WANT_PLUGIN" = "skipped" ]; then
    note "plugin install checks skipped (-Dsingularity-plugin=disabled)"
elif [ -f "$DESTDIR/opt/singularity/lib/singularity/plugins/screensaver/libscreensaver.so" ]; then
    note "plugin .so: OK"
else
    printf 'FAIL plugin .so: not installed\n' >&2; fail=$((fail + 1))
fi
if [ "$WANT_PLUGIN" = "skipped" ]; then
    :
elif [ -f "$DESTDIR/opt/singularity/lib/singularity/plugins/screensaver/screensaver.plugin" ]; then
    note "screensaver.plugin: OK"
else
    printf 'FAIL screensaver.plugin: missing\n' >&2; fail=$((fail + 1))
fi
if [ -d "$SHARE_DIR/shaders" ]; then
    note "shader install dir: present"
else
    printf 'FAIL shader install dir: missing in %s\n' "$SHARE_DIR" >&2; fail=$((fail + 1))
fi

# Plugin DEB: rebuild via tools/make-plugin-deb.sh, confirm the .deb contains
# the plugin. Only meaningful when the plugin was actually built, so it is
# skipped alongside the other plugin checks otherwise. (The engine .deb is
# what the Debian rules build; this stage covers the plugin package only.)
if [ "$WANT_PLUGIN" = "skipped" ]; then
    note "plugin .deb check skipped (-Dsingularity-plugin=disabled)"
else
    DEB_ARCH=$(printf '%s' "$WANT_MACHINE" | grep -qi aarch64 && echo arm64 || echo amd64)
    DEB_OUT=$WORK/ncz-screensavers-plugin_$DEB_ARCH.deb
    sh "$ROOT/tools/make-plugin-deb.sh" "$DEB_ARCH" \
        "$BUILD/plugin/screensaver" "$DEB_OUT" >"$WORK/deb.log" 2>&1 \
        || { cat "$WORK/deb.log" >&2; fail=$((fail + 1)); }
    if [ -f "$DEB_OUT" ]; then
        if dpkg-deb --contents "$DEB_OUT" 2>/dev/null | grep -q "libscreensaver.so"; then
            note "plugin .deb: contains libscreensaver.so ($DEB_ARCH)"
        else
            printf 'FAIL plugin .deb: libscreensaver.so missing in %s\n' "$DEB_OUT" >&2
            fail=$((fail + 1))
        fi
    fi
fi

# ---- verdict ---------------------------------------------------------------
if [ "$fail" -gt 0 ]; then
    printf 'build-artifact-gate: %s failure(s)\n' "$fail" >&2
    exit 1
fi
echo "build-artifact-gate: PASS"
exit 0