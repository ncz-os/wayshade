# NCZ-OS screensaver launcher and idle daemon. The settings UX is the Singularity
# plugin in plugin/; there is no standalone settings application.
# Included from the top-level meson.build with subdir('launcher'); it must not
# call project(). tools/launcher-meson-check.sh builds it from a scratch root.

wl_client_dep = dependency('wayland-client')
glib_dep = dependency('glib-2.0', version: '>=2.70')
gio_dep = dependency('gio-2.0')
gio_unix_dep = dependency('gio-unix-2.0')
wayland_scanner = find_program('wayland-scanner')

ncz_proto_dir = meson.project_source_root() / 'protocols'
ncz_proto_files = []
foreach p : ['ext-idle-notify-v1', 'wlr-output-power-management-unstable-v1']
  xml = ncz_proto_dir / p + '.xml'
  ncz_proto_files += custom_target(p + '-c',
    input: xml, output: p + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
  ncz_proto_files += custom_target(p + '-h',
    input: xml, output: p + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
endforeach

ncz_screensaver_idled = executable('ncz-screensaver-idled',
  ['ncz-screensaver-idled.c', ncz_proto_files],
  dependencies: [wl_client_dep, glib_dep, gio_dep, gio_unix_dep],
  c_args: ['-D_GNU_SOURCE'],
  install: true,
  install_dir: get_option('prefix') / get_option('libexecdir'))

gl_deps = [dependency('egl'), dependency('glesv2'), dependency('dl'),
           meson.get_compiler('c').find_library('m')]
executable('ncz-screensaver-calibrate',
  ['ncz-screensaver-calibrate.c'],
  dependencies: gl_deps,
  c_args: ['-D_GNU_SOURCE'],
  install: true,
  install_dir: get_option('prefix') / get_option('libexecdir') / 'ncz-screensavers')

install_data('ncz-screensaver',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
# The producer half of the lock-hosted screensaver: renders the configured hack
# off-screen (each hack binary's NCZ_FEED_SOCKET mode) and hands frames to the
# NCZ lock client over its frame-source socket. Installed in libexec like the
# idle daemon; the lock's SINGULARITY_LOCK_RENDERER points at this path.
install_data('ncz-screensaver-feed',
  install_dir: get_option('prefix') / get_option('libexecdir'),
  install_mode: 'rwxr-xr-x')
install_data('../include/lock_framesrc_proto.h',
  install_dir: get_option('datadir') / 'ncz-screensavers')
install_data('ncz-screensaver-run-compat',
  rename: 'ncz-screensaver-run',
  install_dir: get_option('prefix') / get_option('libexecdir'),
  install_mode: 'rwxr-xr-x')
install_data('ncz-screensaver-idled.service',
  install_dir: get_option('prefix') / 'lib/systemd/user')

# The schema and the compatibility catalog copy under
# share/ncz-screensaver-chooser are installed by the top-level meson.build.
install_data('../assets/screensaver-chooser/hacks.tsv',
  install_dir: get_option('datadir') / 'ncz-screensavers')

# ---------------------------------------------------------------------------
# Idle-daemon tests.
#
# The exit-status test needs a compositor that advertises wl_seat but not
# ext-idle-notify-v1; this fixture provides it and is never installed. The
# lint test skips itself when systemd-analyze is unavailable.
# ---------------------------------------------------------------------------
wayland_server_dep = dependency('wayland-server', required: false)
if wayland_server_dep.found()
  no_idle_compositor = executable('test-no-idle-compositor',
    ['tests/no-idle-compositor.c'],
    dependencies: wayland_server_dep,
    install: false)
  test('idled-exit-status',
    find_program('tests/test-idled-exit-status.sh'),
    args: [ncz_screensaver_idled, no_idle_compositor],
    timeout: 30)
endif

test('idled-unit-lint',
  find_program('tests/test-idled-unit-lint.sh'),
  args: [ncz_screensaver_idled],
  timeout: 30)
test('idled-dryrun',
  find_program('tests/test-idled-dryrun.sh'),
  args: [ncz_screensaver_idled],
  timeout: 30)
