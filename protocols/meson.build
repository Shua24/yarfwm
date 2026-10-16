# The river protocol XML definitions are NOT vendored: they are read from the
# river installation, discovered through river's own pkg-config file. River
# installs them under its pkgdatadir (`pkg-config --variable=pkgdatadir
# river-protocols`, normally /usr/share/river-protocols) and ships a version
# with them, so the protocol definitions and the compositor that speaks them can
# never drift apart. A stale hand-copied XML is exactly the failure this avoids.
#
# Consequence, deliberately accepted: building yarfwm requires the river package
# installed. That is also why the version floor below exists -- the window
# manager calls river-window-management-v1 version-6 members, and 0.5.0 is the
# release whose XML carries them. Building against an older river's XML would
# fail confusingly at compile time instead of clearly here.
#
# cursor-shape-v1 and tablet-v2 ARE vendored, in this directory. They are
# wayland-protocols interfaces, not river protocols, so they do not come from
# river-protocols.pc; vendoring them keeps wayland-protocols out of the build
# requirements.

wayland_scanner = find_program('wayland-scanner')

river_protocols = dependency('river-protocols', version: '>=0.5.0')
# The .pc exposes only pkgdatadir (/usr/share/river-protocols); the XMLs
# themselves live one level down, in its `stable/` subdirectory. That layout is
# river's, and river's own packaging installs the same way.
river_protocols_directory = river_protocols.get_variable(pkgconfig: 'pkgdatadir') / 'stable'

protocols_include_directories = include_directories('.')

river_window_management_client_header = custom_target(
  'river-window-management-v1-client-header',
  input: river_protocols_directory / 'river-window-management-v1.xml',
  output: 'river-window-management-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

river_window_management_protocol_source = custom_target(
  'river-window-management-v1-protocol-source',
  input: river_protocols_directory / 'river-window-management-v1.xml',
  output: 'river-window-management-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

river_layer_shell_client_header = custom_target(
  'river-layer-shell-v1-client-header',
  input: river_protocols_directory / 'river-layer-shell-v1.xml',
  output: 'river-layer-shell-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

river_layer_shell_protocol_source = custom_target(
  'river-layer-shell-v1-protocol-source',
  input: river_protocols_directory / 'river-layer-shell-v1.xml',
  output: 'river-layer-shell-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

river_xkb_bindings_client_header = custom_target(
  'river-xkb-bindings-v1-client-header',
  input: river_protocols_directory / 'river-xkb-bindings-v1.xml',
  output: 'river-xkb-bindings-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

river_xkb_bindings_protocol_source = custom_target(
  'river-xkb-bindings-v1-protocol-source',
  input: river_protocols_directory / 'river-xkb-bindings-v1.xml',
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
