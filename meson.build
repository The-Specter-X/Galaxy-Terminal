project('galaxy-terminal', 'c',
  version: '0.1.0',
  default_options: ['c_std=c11', 'warning_level=2'])

gtk = dependency('gtk+-3.0', version: '>=3.24')
vte = dependency('vte-2.91', version: '>=0.62')
xapp = dependency('xapp', version: '>=2.6')

executable('galaxy-terminal',
  'src/app.c', 'src/settings.c', 'src/preferences.c',
  dependencies: [gtk, vte, xapp],
  install: true)

install_data('data/org.thespecterx.GalaxyTerminal.desktop',
  install_dir: get_option('datadir') / 'applications')

test_exe = executable('test-settings', 'tests/settings-test.c', 'src/settings.c',
  include_directories: include_directories('src'),
  dependencies: [gtk],
  install: false)
test('settings', test_exe)

