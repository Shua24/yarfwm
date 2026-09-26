# Unit tests. Only built when the `test` option is enabled:
#   meson setup build -Dtest=true && ninja -C build && meson test -C build
#
# Everything here must run without a Wayland connection, so only pure logic is
# tested (no river_* proxies, no Display, no Seat).

gtest = dependency('gtest', required: true)
gtest_main = dependency('gtest_main', required: true)

placement_test = executable(
  'placement_test',
  'PlacementTest.cpp',
  '../src/Placement.cpp',
  include_directories: [include_directories('../include')],
  dependencies: [gtest, gtest_main],
)

test('placement', placement_test)

placement_helpers_test = executable(
  'placement_helpers_test',
  'PlacementHelpersTest.cpp',
  '../src/Placement.cpp',
  include_directories: [include_directories('../include')],
  dependencies: [gtest, gtest_main],
)

test('placement_helpers', placement_helpers_test)

# Config::load reads $HOME, so every case redirects it to a scratch directory
# (see the header comment in ConfigTest.cpp). default_config_header provides
# the embedded default the fallback path writes.
config_test = executable(
  'config_test',
  'ConfigTest.cpp',
  '../src/Config.cpp',
  default_config_header,
  include_directories: [
    include_directories('../include'),
    include_directories('../src'),
  ],
  dependencies: [gtest, gtest_main, jsoncpp],
)

test('config', config_test)

# The parser half of the key binding engine: KeybindParse.cpp is
# self-contained (no Keybind object is constructed), so it links without the
# rest of the engine.
keybind_parse_test = executable(
  'keybind_parse_test',
  'KeybindParseTest.cpp',
  '../src/KeybindParse.cpp',
  river_window_management_client_header,
  include_directories: [
    include_directories('../include'),
    protocols_include_directories,
  ],
  dependencies: [gtest, gtest_main, xkbcommon, jsoncpp],
)

test('keybind_parse', keybind_parse_test)

# The session lock guard lives in View and its handlers reference the whole
# View/Seat closure, so this test links the core whole (minus main.cpp). The
# regression it guards against: session_unlocked failing to clear the flag,
# which silently refuses every key binding for the rest of the run.
view_lock_test = executable(
  'view_lock_test',
  'ViewLockTest.cpp',
  yarfwm_core_sources,
  river_window_management_client_header,
  river_window_management_protocol_source,
  river_layer_shell_client_header,
  river_layer_shell_protocol_source,
  river_xkb_bindings_client_header,
  river_xkb_bindings_protocol_source,
  default_config_header,
  include_directories: [
    include_directories('../include'),
    include_directories('../src'),
    protocols_include_directories,
  ],
  dependencies: [
    gtest,
    gtest_main,
    wayland_client,
    xkbcommon,
    pixman,
    math,
    jsoncpp,
    cairo,
    pangocairo,
  ],
)

test('view_lock', view_lock_test)

# The registry classification: the two globals a decoration renderer needs
# (wl_compositor, wl_shm) must be bound, and nothing else may be.
display_registry_test = executable(
  'display_registry_test',
  'DisplayRegistryTest.cpp',
  '../src/RegistryInterfaces.cpp',
  include_directories: [include_directories('../include')],
  dependencies: [gtest, gtest_main],
)

test('display_registry', display_registry_test)

# The pure titlebar arithmetic: button layout backwards from the right edge,
# the pruning rule, the text band, hit testing and the offset. No Wayland, no
# cairo -- this is labwc's geometry reduced to what a titlebar needs.
decoration_geometry_test = executable(
  'decoration_geometry_test',
  'DecorationGeometryTest.cpp',
  '../src/DecorationGeometry.cpp',
  include_directories: [include_directories('../include')],
  dependencies: [gtest, gtest_main],
)

test('decoration_geometry', decoration_geometry_test)

# The painter. Pure memory in, pure memory out: no Wayland connection, no
# wl_shm, so the whole renderer is unit testable. cairo and pango are linked
# here and nowhere else except the executable itself.
decoration_renderer_test = executable(
  'decoration_renderer_test',
  'DecorationRendererTest.cpp',
  '../src/DecorationRenderer.cpp',
  '../src/DecorationGeometry.cpp',
  include_directories: [include_directories('../include')],
  dependencies: [gtest, gtest_main, cairo, pangocairo],
)

test('decoration_renderer', decoration_renderer_test)

# The decoration settings file: colour parsing, the fallbacks, the generated
# file, a partial file, malformed input, and a missing HOME. Every case
# redirects HOME to a scratch directory, because load() writes the embedded
# default when no candidate parses.
window_decorations_config_test = executable(
  'window_decorations_config_test',
  'WindowDecorationsConfigTest.cpp',
  '../src/WindowDecorationsConfig.cpp',
  default_decorations_config_header,
  include_directories: [
    include_directories('../include'),
    include_directories('../src'),
  ],
  dependencies: [gtest, gtest_main, jsoncpp],
)

test('window_decorations_config', window_decorations_config_test)

