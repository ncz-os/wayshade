shared_module('screensaver',
  ['screensaver.vala', 'backend.vala', 'ncz_backend.vala'],
  dependencies: [dependency('gtk4'), libsingularity_dep, dependency('libpeas-2'), dependency('gee-0.8'), dependency('json-glib-1.0')],
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
