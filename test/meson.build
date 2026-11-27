foreach name : ['connections', 'features', 'geometry', 'grid', 'improve', 'layers', 'mesh', 'refine', 'shapefile', 'triangulate', 'version']
  test(
    name,
    executable(
      'test_' + name,
      'test_' + name + '.cpp',
      dependencies: [core_dep, gtest_dep],
    ),
  )
endforeach

test(
  'triangle_files',
  executable(
    'test_triangle_files',
    'test_triangle_files.cpp',
    dependencies: [files_dep, gtest_dep],
  ),
)

# the C API header from C
test(
  'c_header',
  executable(
    'test_c_header',
    'test_c_header.c',
    dependencies: api_dep,
  ),
)

# the C API through ctypes, and the program; not under the sanitizers, whose
# runtime must be loaded before Python starts
python = import('python').find_installation(modules: ['pytest'])
python_env = environment()
python_env.set('TRIANGLE_UTILS_LIBRARY', api_lib.full_path())
python_env.set('TRIANGLE_UTILS_PROGRAM', program.full_path())
python_env.set('PYTHONDONTWRITEBYTECODE', '1')
# where Windows finds the library's DLL for the program
python_env.prepend('PATH', meson.project_build_root() / 'src')
if get_option('b_sanitize') == 'none'
  test(
    'python',
    python,
    args: ['-m', 'pytest', '-p', 'no:cacheprovider', meson.current_source_dir() / 'python'],
    env: python_env,
    depends: [api_lib, program],
    # the tests took 21 to 28 s on the macOS runners, against the default of
    # 30 s, and one run of the layers pull request (#60) exceeded it
    timeout: 120,
  )
endif
