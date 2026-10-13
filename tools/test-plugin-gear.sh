#!/bin/sh
# Headless GTK test for the per-screensaver Options ("gear") button.
# Builds and runs plugin/screensaver/tests/gear_options_test.vala against the real
# engine in a debian:forky container (never natively on the build host).
# usage: test-plugin-gear.sh REPO_DIR ENGINE_DEB SINGDEB RUNTIME [SELINUX_Z]
#   REPO_DIR   host path to the ncz-screensavers worktree
#   ENGINE_DEB host path to the matching ncz-screensavers .deb
#   SINGDEB    host path to the matching ncz-singularity-desktop .deb
#   RUNTIME    podman | docker
#   SELINUX_Z  1 to append :Z to bind mounts (Fedora/SELinux hosts)
set -eu
REPO=${1:?repo}
ENGDEB=${2:?engine deb}
SINGDEB=${3:?singularity deb}
RUNTIME=${4:-podman}
SELINUX=${5:-0}
Z=""; ZS=""
# :Z is a podman-only SELinux mount flag; docker rejects it.
[ "$SELINUX" = "1" ] && [ "$RUNTIME" = "podman" ] && { Z=":Z"; ZS=",Z"; }
SCRATCH="$(dirname "$REPO")/ncz-plugin-gear-test-scratch"
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"

$RUNTIME run --rm --network host \
  -v "$REPO":/src$Z \
  -v "$ENGDEB":/in/eng.deb:ro$ZS \
  -v "$SINGDEB":/in/sing.deb:ro$ZS \
  -v "$SCRATCH":/work$Z \
  -e DEBIAN_FRONTEND=noninteractive \
  debian:forky bash -c '
    set -e
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends \
      build-essential valac pkgconf ca-certificates \
      libgtk-4-dev libpeas-2-dev libgee-0.8-dev libjson-glib-dev \
      libgtksourceview-5-dev libgtk4-layer-shell-dev \
      xvfb xauth libgl1-mesa-dri libglib2.0-bin python3 \
      libgtk-4-1 libpeas-2-0 libgee-0.8-2 libjson-glib-1.0-0 \
      libgtksourceview-5-0 libgtk4-layer-shell0 libenchant-2-2 >/dev/null
    dpkg-deb -x /in/eng.deb /
    dpkg-deb -x /in/sing.deb /
    glib-compile-schemas /usr/share/glib-2.0/schemas
    export PKG_CONFIG_PATH=/opt/singularity/lib/pkgconfig
    export PATH=/usr/local/bin:/usr/bin:/bin
    # The [ModuleInit] Peas entry point makes Vala register every class through a
    # GTypeModule, which needs the plugin loader. The widget classes under test do
    # not, so compile a copy of screensaver.vala with the plugin class stripped: that
    # switches the whole compilation back to ordinary static type registration.
    awk "BEGIN{s=0} /^[[:space:]]*\[ModuleInit\]/{s=1} s && /^public class OptionBinding/{s=0} !s{print}" \
        /src/plugin/screensaver/screensaver.vala > /work/screensaver_test.vala
    if grep -qE "^[[:space:]]*\[ModuleInit\]|peas_register_types" /work/screensaver_test.vala; then
        echo "test-plugin-gear: Peas entry point still present after stripping" >&2; exit 1
    fi
    if ! grep -q "^public class OptionBinding" /work/screensaver_test.vala; then
        echo "test-plugin-gear: could not strip the Peas plugin class" >&2; exit 1
    fi
    echo "=== valac compile ==="
    valac --pkg posix --pkg gtk4 --pkg libpeas-2 --pkg gee-0.8 --pkg json-glib-1.0 \
          --pkg cairo --pkg graphene-gobject-1.0 --pkg singularity-1.0 \
          --vapidir=/opt/singularity/share/vala/vapi \
          -X -I/opt/singularity/include -X -L/opt/singularity/lib \
          -X -lsingularity -X -Wl,-rpath,/opt/singularity/lib \
          -o /work/gear_test \
          /work/screensaver_test.vala \
          /src/plugin/screensaver/backend.vala \
          /src/plugin/screensaver/ncz_backend.vala \
          /src/plugin/screensaver/tests/gear_options_test.vala
    echo "=== run (headless) ==="
    xvfb-run -a /work/gear_test
  '
echo "SCRATCH=$SCRATCH"
