project('galaxy-terminal', 'c', version: '0.2.0',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=true'])

gtk = dependency('gtk+-3.0', version: '>=3.24')
vte = dependency('vte-2.91', version: '>=0.80')
xapp = dependency('xapp', version: '>=2.8')
pcre = dependency('libpcre2-8')
deps = [gtk, vte, xapp, pcre]
inc = include_directories('.', 'src')
config = configuration_data()
config.set_quoted('GALAXY_VERSION', meson.project_version())
config.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config.set_quoted('LOCALEDIR', get_option('prefix') / get_option('localedir'))
configure_file(output: 'config.h', configuration: config)

core = static_library('galaxy-core',
  'src/app.c', 'src/commands.c', 'src/window.c', 'src/session.c',
  'src/appearance.c', 'src/preferences.c', 'src/settings.c', 'src/shortcuts.c',
  include_directories: inc, dependencies: deps)
executable('galaxy-terminal', 'src/main.c', link_with: core,
  include_directories: inc, dependencies: deps, install: true)
install_data('data/org.thespecterx.GalaxyTerminal.desktop',
  install_dir: get_option('datadir') / 'applications')
install_man('data/galaxy-terminal.1')

settings_test = executable('test-settings', 'tests/settings-test.c',
  'src/settings.c', 'src/shortcuts.c', include_directories: inc, dependencies: gtk)
test('settings', settings_test)
executable('test-integration', 'tests/integration.c', link_with: core,
  include_directories: inc, dependencies: deps)
subdir('po')
