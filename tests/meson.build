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
