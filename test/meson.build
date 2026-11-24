foreach name : ['mesh', 'triangulate', 'version']
  test(
    name,
    executable(
      'test_' + name,
      'test_' + name + '.cpp',
      dependencies: [core_dep, gtest_dep],
    ),
  )
endforeach

# the C API header from C
test(
  'c_header',
  executable(
    'test_c_header',
    'test_c_header.c',
    dependencies: api_dep,
  ),
)

# the C API through ctypes; not under the sanitizers, whose runtime must be
# loaded before Python starts
python = import('python').find_installation(modules: ['pytest'])
if get_option('b_sanitize') == 'none'
  test(
    'c_api',
    python,
    args: ['-m', 'pytest', '-p', 'no:cacheprovider', meson.current_source_dir() / 'python'],
    env: {'TRIANGLE_UTILS_LIBRARY': api_lib.full_path(), 'PYTHONDONTWRITEBYTECODE': '1'},
    depends: api_lib,
  )
endif
