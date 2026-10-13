#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Build the Singularity screensaver plugin against a Singularity SDK.
# usage: build-plugin.sh SDK_PREFIX OUT_DIR   (SDK_PREFIX has lib/pkgconfig/singularity-1.0.pc and share/vala/vapi)
# Produces OUT_DIR/libscreensaver.so and OUT_DIR/screensaver.plugin.
set -eu
SDK=${1:?usage: $0 SDK_PREFIX OUT_DIR}
OUT=${2:?usage: $0 SDK_PREFIX OUT_DIR}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$SDK/lib/pkgconfig/singularity-1.0.pc" ] || { echo "build-plugin: no singularity-1.0.pc under $SDK/lib/pkgconfig" >&2; exit 1; }
[ -d "$SDK/share/vala/vapi" ] || { echo "build-plugin: no share/vala/vapi under $SDK" >&2; exit 1; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
printf "project('ncz-screensaver-plugin', ['c', 'vala'], version: '0.1.0')\nadd_languages('vala', required: true)\nadd_project_arguments('--pkg=posix', '--vapidir=' + get_option('singularity-vapidir'), language: 'vala')\nsubdir('plugin')\n" > "$T/meson.build"
cp "$ROOT/meson_options.txt" "$T/meson_options.txt"
ln -s "${NCZ_PLUGIN_SRC:-$ROOT/plugin}" "$T/plugin"
PKG_CONFIG_PATH="$SDK/lib/pkgconfig" meson setup "$T/b" "$T" -Dsingularity-plugin=enabled \
    -Dsingularity-vapidir="$SDK/share/vala/vapi" -Db_lundef=false >/dev/null
if ! PKG_CONFIG_PATH="$SDK/lib/pkgconfig" ninja -C "$T/b" >"$T/ninja.log" 2>&1; then
    grep -E "error|FAILED" "$T/ninja.log" >&2 || tail -n 20 "$T/ninja.log" >&2
    echo "build-plugin: compile failed" >&2
    exit 1
fi
[ -f "$T/b/plugin/screensaver/libscreensaver.so" ] || { echo "build-plugin: no libscreensaver.so produced" >&2; exit 1; }
mkdir -p "$OUT"
cp "$T/b/plugin/screensaver/libscreensaver.so" "$ROOT/plugin/screensaver/screensaver.plugin" "$OUT/"
