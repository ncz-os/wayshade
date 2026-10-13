shared_module('screensaver',
  ['screensaver.vala', 'backend.vala', 'ncz_backend.vala'],
  # libsoup-3.0: the 2026-10-07 singularity-1.0.vapi uses Soup.Session but its .deps
  # and singularity-1.0.pc Requires: do not list libsoup-3.0, so a consumer that
  # does not name it fails with "The symbol `Soup' could not be found".
  dependencies: [dependency('gtk4'), libsingularity_dep, dependency('libpeas-2'), dependency('gee-0.8'), dependency('json-glib-1.0'), dependency('libsoup-3.0')],
  c_args: ['-DGETTEXT_PACKAGE="singularity-desktop"'],
  install: true,
  install_dir: plugin_dir / 'screensaver'
)

configure_file(
  input: 'screensaver.plugin',
  output: 'screensaver.plugin',
  copy: true,
  install: true,
  install_dir: plugin_dir / 'screensaver'
)

# The icon the Plugins page shows for the plugin (the Icon key of screensaver.plugin).
install_data('dev.ncz.screensaver.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps'
)
