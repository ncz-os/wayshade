# Reference GTK4/libadwaita front end for the ncz-screensaver CLI (see README.md here).
# Built only with -Dgtk4-reference-app=true.

install_data('ncz-screensaver-settings',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('dev.ncz.screensaver.desktop',
  install_dir: get_option('datadir') / 'applications')

# Headless checks (--dump); they skip (exit 77) when PyGObject, libadwaita or the
# dev.ncz.screensaver schema are not available to the test run.
test('gtk4-reference-app-dump', find_program('python3'),
  args: [files('tests/run_dump.py'), files('ncz-screensaver-settings')],
  env: {'NCZ_SCREENSAVER_CATALOG': meson.project_source_root() / 'assets/screensaver-chooser/hacks.tsv'},
  suite: 'gtk4-reference-app')
