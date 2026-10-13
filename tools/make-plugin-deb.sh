#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# make-plugin-deb.sh - package the Singularity screensaver plugin.
# usage: make-plugin-deb.sh ARCH BUILD_DIR OUT.deb [VERSION]
#   BUILD_DIR holds libscreensaver.so and screensaver.plugin (tools/build-plugin.sh output).
# The package installs into the plugin directory of the Singularity desktop next to the
# sibling plugins. Its postinst adds "screensaver" to the shell's default enabled-plugins
# (a gschema override that extends the distribution default, never replaces a user's list);
# ncz-screensaver ensure-plugin does the same once per existing user at login.
set -eu
ARCH=${1:?arch (amd64|arm64)}
SRC=${2:?build dir}
OUT=${3:?output .deb}
VER=${4:-0.3.16}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
D=$W/opt/singularity/lib/singularity/plugins/screensaver
mkdir -p "$D" "$W/DEBIAN"
install -m 0755 "$SRC/libscreensaver.so" "$D/libscreensaver.so"
install -m 0644 "$SRC/screensaver.plugin" "$D/screensaver.plugin"
cat > "$W/DEBIAN/control" <<EOC
Package: ncz-screensavers-plugin
Version: $VER
Architecture: $ARCH
Maintainer: Jason Perlow <jperlow@gmail.com>
Depends: ncz-singularity-desktop, ncz-screensavers (>= 0.8.5), libglib2.0-bin
Section: x11
Priority: optional
Description: Screensaver and Lockscreen page for the Singularity desktop
 A Singularity plugin (libsingularity widgets only) that adds the "Screensaver and
 Lockscreen" page to Settings > Plugins: chooser, previews, GPU class flags, offload,
 render quality, idle and lock timers and per-hack options including the Black Hole scenes.
 It is the settings UI of the ncz-screensavers engine on NCZ-OS.
EOC
cat > "$W/DEBIAN/postinst" <<'EOC'
#!/bin/sh
set -e
S=/opt/singularity/share/glib-2.0/schemas
OV=$S/99-zz-ncz-screensaver-plugin.gschema.override
if { [ "$1" = configure ] || [ "$1" = triggered ]; } && [ -d "$S" ]; then
    # the distribution default: last enabled-plugins under [dev.sinty.desktop] in any
    # override except our own (so a later change to the base list is followed)
    list=""
    for f in "$S"/*.override; do
        [ -e "$f" ] || continue
        [ "$f" = "$OV" ] && continue
        v=$(awk '/^\[/{g=$0} g=="[dev.sinty.desktop]" && sub(/^enabled-plugins=/,"") {print}' "$f" | tail -n 1)
        [ -n "$v" ] && list=$v
    done
    if [ -n "$list" ]; then
        case "$list" in
            "[]"|"@as []") list="['screensaver']" ;;
            *"'screensaver'"*) ;;
            *) list=$(printf '%s' "$list" | sed "s/][[:space:]]*$/, 'screensaver']/") ;;
        esac
        printf '[dev.sinty.desktop]\nenabled-plugins=%s\n' "$list" > "$OV"
        if ! glib-compile-schemas "$S" 2>/var/log/ncz-screensavers-plugin-schema.log; then
            echo "ncz-screensavers-plugin: schema compile failed, see /var/log/ncz-screensavers-plugin-schema.log" >&2
            rm -f "$OV"
        fi
    fi
fi
exit 0
EOC
cat > "$W/DEBIAN/postrm" <<'EOC'
#!/bin/sh
set -e
S=/opt/singularity/share/glib-2.0/schemas
if [ "$1" = remove ] || [ "$1" = purge ]; then
    if [ -e "$S/99-zz-ncz-screensaver-plugin.gschema.override" ]; then
        rm -f "$S/99-zz-ncz-screensaver-plugin.gschema.override"
        glib-compile-schemas "$S" 2>/dev/null || true
    fi
fi
exit 0
EOC
printf 'interest-noawait /opt/singularity/share/glib-2.0/schemas\n' > "$W/DEBIAN/triggers"
chmod 0755 "$W/DEBIAN/postinst" "$W/DEBIAN/postrm"
dpkg-deb --root-owner-group -b "$W" "$OUT" >/dev/null
echo "$OUT"
