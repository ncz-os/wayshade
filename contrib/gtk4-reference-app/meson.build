# Reference GTK4 front end for the ncz-screensaver CLI (see README.md here).
# Built only with -Dgtk4-reference-app=true.

install_data('ncz-screensaver-settings',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('dev.ncz.screensaver.desktop',
  install_dir: get_option('datadir') / 'applications')

# Headless gates for the reference front end. They skip (exit 77) when
# PyGObject, GTK 4, libadwaita, glib-compile-schemas or a display server
# (Xvfb / Broadway) are not available to the test run.
python = find_program('python3')
catalog_env = {'NCZ_SCREENSAVER_CATALOG': meson.project_source_root() / 'assets/screensaver-chooser/hacks.tsv'}

test('gtk4-reference-app-dump', python,
  args: [files('tests/run_dump.py'), files('ncz-screensaver-settings')],
  env: catalog_env,
  suite: 'gtk4-reference-app')

# Builds the settings window and the per-screensaver options dialog on a
# throw-away display and asserts the shipped catalogue loaded. This is the
# fail-before/pass-after gate for the UI itself.
test('gtk4-reference-app-smoke', python,
  args: [files('tests/run_smoke.py'), files('ncz-screensaver-settings')],
  env: catalog_env,
  suite: 'gtk4-reference-app',
  timeout: 180)

# Model-level tests (GPU tiers, pool mapping, schema-derived choice lists,
# randomize). Skipped without PyGObject because they import the app to reach
# its pure helpers.
pytest = find_program('pytest', 'pytest-3', required: false)
if pytest.found()
  test('gtk4-reference-app-model', pytest,
    args: ['-q', files('tests/test_reference_app.py')],
    env: catalog_env,
    suite: 'gtk4-reference-app',
    timeout: 300)
endif