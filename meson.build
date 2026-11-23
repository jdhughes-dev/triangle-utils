project(
  'triangle-utils',
  ['cpp'],
  version: '0.1.0',
  license: 'CC0-1.0',
  meson_version: '>= 1.3.1',
  default_options: [
    'cpp_std=c++20',
    'buildtype=release',
  ],
)

subdir('src')

# Triangle from MODFLOW-ORG/triangle (subprojects/triangle.wrap), linked
# statically
triangle_dep = dependency(
  'triangle',
  default_options: ['default_library=static'],
)

# GoogleTest is built as a static library, as in gridgen
gtest_dep = dependency(
  'gtest_main',
  version: '>=1.17',
  default_options: ['default_library=static'],
)

subdir('test')
