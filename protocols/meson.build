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

# cursor-shape-v1 is not a river protocol: it is the wayland-protocols
# interface a client uses to ask for a cursor shape by name instead of
# supplying its own image. River advertises wp_cursor_shape_manager_v1, and it
# is how the window manager sets the cursor it owns while a pointer operation
# is in progress (river_seat_v1's own version-4 note). Vendored here for the
# same reason as the river XMLs: the build must not depend on the
# wayland-protocols package version installed on the build machine.
cursor_shape_client_header = custom_target(
  'cursor-shape-v1-client-header',
  input: 'cursor-shape-v1.xml',
  output: 'cursor-shape-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

cursor_shape_protocol_source = custom_target(
  'cursor-shape-v1-protocol-source',
  input: 'cursor-shape-v1.xml',
  output: 'cursor-shape-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

# tablet-v2 is not used by the window manager. It is here because
# cursor-shape-v1's generated interface table references
# zwp_tablet_tool_v2_interface (the manager has a get_tablet_tool_v2 request),
# so the generated C needs the symbol to link. Generating it is cheaper and
# more honest than hand-stubbing the reference.
tablet_v2_client_header = custom_target(
  'tablet-v2-client-header',
  input: 'tablet-v2.xml',
  output: 'tablet-v2-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

tablet_v2_protocol_source = custom_target(
  'tablet-v2-protocol-source',
  input: 'tablet-v2.xml',
  output: 'tablet-v2-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
