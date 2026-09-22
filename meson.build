project(
  'yarfwm',
  'cpp',
  version: '0.1.0',
  license: 'MIT',
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

cc = meson.get_compiler('cpp')
add_project_arguments(cc.get_supported_arguments([
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

wayland_server = dependency('wayland-server', version: '>=1.22.90')
wayland_client = dependency('wayland-client', version: '>=1.22.90')
wayland_protos = dependency('wayland-protocols', version: '>=1.39')
wayland_cursor = dependency('wayland-cursor', required: false)
wlroots = dependency('wlroots-0.20', version: ['>=0.20.1', '<0.21.0'], default_options: ['examples=false'])
xkbcommon = dependency('xkbcommon')
pixman = dependency('pixman-1')
math = cc.find_library('m')
drm = dependency('libdrm', required: false)
udev = dependency('libudev', required: false)
systemd = dependency('libsystemd', required: false)
jsoncpp = dependency('jsoncpp', required: false)

conf_data = configuration_data()
conf_data.set10('HAVE_WAYLAND_CURSOR', wayland_cursor.found())
conf_data.set10('HAVE_DRM', drm.found())
conf_data.set10('HAVE_UDEV', udev.found())
conf_data.set10('HAVE_SYSTEMD', systemd.found())
conf_data.set10('HAVE_JSONCPP', jsoncpp.found())

if not jsoncpp.found()
  warning('jsoncpp not found: falling back to minimal built-in JSON parser for config loading')
endif

config_dir = get_option('sysconfdir') / 'yarfwm'
configure_file(
  input: 'data/config.json.in',
  output: 'config.json',
  configuration: conf_data,
  copy: true,
)

install_data('data/yarfwm.desktop', install_dir: get_option('datadir') / 'applications')
install_data('data/yarfwm-session', install_dir: get_option('datadir') / 'xsessions')

subdir('protocols')
subdir('include')
subdir('src')
