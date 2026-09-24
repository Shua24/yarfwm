project(
  'yarfwm',
  ['cpp', 'c'],
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>=0.59.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
  ],
)

add_project_arguments(
  [
    '-D_GNU_SOURCE',
  ],
  language: 'cpp',
)

compiler = meson.get_compiler('cpp')
add_project_arguments(compiler.get_supported_arguments([
  '-Wall',
  '-Wextra',
  '-Wpedantic',
  '-Wundef',
  '-Wpointer-arith',
  '-Wstrict-prototypes',
  '-Wmissing-prototypes',
  '-Wunused-parameter',
  '-Wshadow',
  '-Wformat=2',
  '-Wimplicit-fallthrough=2',
  '-Wcast-align',
  '-Wstrict-aliasing=2',
  '-Wswitch-default',
  '-Wconversion',
  '-Wzero-as-null-pointer-constant',
  '-Wdouble-promotion',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wnon-virtual-dtor',
  '-Woverloaded-virtual',
  '-Wold-style-cast',
  '-Wnull-dereference',
  '-Wmisleading-indentation',
  '-Wduplicated-cond',
  '-Wduplicated-branches',
  '-Wlogical-op',
  '-Wrestrict',
  '-Waggregate-return',
  '-Walloc-zero',
  '-Wvla',
  '-Wno-unused-parameter',
]), language: 'cpp')

fs = import('fs')

wayland_client = dependency('wayland-client', version: '>=1.22.90')
wayland_cursor = dependency('wayland-cursor', required: false)
xkbcommon = dependency('xkbcommon')
pixman = dependency('pixman-1')
math = compiler.find_library('m')
jsoncpp = dependency('jsoncpp', required: false)

configuration_data_values = configuration_data()
configuration_data_values.set10('HAVE_WAYLAND_CURSOR', wayland_cursor.found())
configuration_data_values.set10('HAVE_JSONCPP', jsoncpp.found())

if not jsoncpp.found()
  warning('jsoncpp not found: falling back to minimal built-in JSON parser for config loading')
endif

install_data('data/icons/yarfwm.svg', install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')

# No desktop entry is installed. Yarfwm is a window manager *client*, not a
# compositor and not a standalone application: it only runs while river is
# already up and advertising river_window_manager_v1. River starts it from its
# init script (`exec yarfwm` in $XDG_CONFIG_HOME/river/init), which is river's
# own mechanism for choosing a window manager. An `Exec=river -c yarfwm`
# session file would bypass that init script, and a bare `Exec=yarfwm`
# application entry would start a client with no compositor to connect to.

subdir('protocols')
subdir('src')

if get_option('test')
  subdir('tests')
endif
