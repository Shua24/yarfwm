# The protocol XML definitions vendored in this directory are the single source
# of truth. Headers and sources are generated at build time with wayland-scanner
# so that a fresh checkout always builds from these files, with nothing
# generated checked in.

wayland_scanner = find_program('wayland-scanner')

protocols_include_directories = include_directories('.')

river_window_management_client_header = custom_target(
  'river-window-management-v1-client-header',
  input: '../river-window-management-v1.xml',
  output: 'river-window-management-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

river_window_management_protocol_source = custom_target(
  'river-window-management-v1-protocol-source',
  input: '../river-window-management-v1.xml',
  output: 'river-window-management-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

river_layer_shell_client_header = custom_target(
  'river-layer-shell-v1-client-header',
  input: 'river-layer-shell-v1.xml',
  output: 'river-layer-shell-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

river_layer_shell_protocol_source = custom_target(
  'river-layer-shell-v1-protocol-source',
  input: 'river-layer-shell-v1.xml',
  output: 'river-layer-shell-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

river_xkb_bindings_client_header = custom_target(
  'river-xkb-bindings-v1-client-header',
  input: 'river-xkb-bindings-v1.xml',
  output: 'river-xkb-bindings-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

river_xkb_bindings_protocol_source = custom_target(
  'river-xkb-bindings-v1-protocol-source',
  input: 'river-xkb-bindings-v1.xml',
  output: 'river-xkb-bindings-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
