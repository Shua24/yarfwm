yarfwm_sources = files(
  'main.cpp',
  'Yarfwm.cpp',
  'Server.cpp',
  'Display.cpp',
  'Seat.cpp',
  'View.cpp',
  'ViewEvents.cpp',
  'Config.cpp',
  'Keybind.cpp',
  'LayerShell.cpp',
  'Output.cpp',
)

# The default configuration is embedded in the executable so that first
# startup can write config.json without any installed data files.
default_config_header = configure_file(
  input: 'default-config.h.in',
  output: 'default-config.h',
  configuration: {'DEFAULT_CONFIG_JSON': fs.read('../data/config.json.in')},
)

executable(
  'yarfwm',
  yarfwm_sources,
  river_window_management_client_header,
  river_window_management_protocol_source,
  river_layer_shell_client_header,
  river_layer_shell_protocol_source,
  river_xkb_bindings_client_header,
  river_xkb_bindings_protocol_source,
  default_config_header,
  dependencies: [
    wayland_client,
    xkbcommon,
    pixman,
    math,
    jsoncpp,
  ],
  include_directories: [
    include_directories('../include'),
    protocols_include_directories,
  ],
  install: true,
  install_dir: get_option('bindir'),
)
