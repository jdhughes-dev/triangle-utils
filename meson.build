project(
  'triangle-utils',
  ['c', 'cpp'],
  version: '0.1.0',
  license: 'CC0-1.0',
  meson_version: '>= 1.3.1',
  default_options: [
    'cpp_std=c++20',
    'buildtype=release',
  ],
)

cpp = meson.get_compiler('cpp')
cpp_id = cpp.get_id()

# keep floating-point results the same across compilers, as gridgen does: no
# value-unsafe optimizations and no fused multiply-adds
compile_args = []
if cpp_id == 'intel-llvm'
  compile_args += ['-fp-model=precise']
elif cpp_id in ['intel-llvm-cl', 'msvc']
  # MSVC does not contract with /fp:precise unless /fp:contract is given
  compile_args += ['/fp:precise']
endif
if cpp_id in ['gcc', 'clang', 'intel-llvm']
  compile_args += ['-ffp-contract=off']
elif cpp_id in ['clang-cl', 'intel-llvm-cl']
  compile_args += ['/clang:-ffp-contract=off']
endif

# global so GoogleTest is built with the same options; Triangle sets its own
add_global_arguments(cpp.get_supported_arguments(compile_args), language: 'cpp')

# Triangle from MODFLOW-ORG/triangle (subprojects/triangle.wrap), linked
# statically
triangle_dep = dependency(
  'triangle',
  default_options: ['default_library=static'],
)

python_package = get_option('python')

subdir('src')

if python_package
  # the module, beside the library src/meson.build installs, with the
  # licenses of triangle-utils and Triangle and the notice of Triangle's
  # source
  python = import('python').find_installation()
  python.install_sources(
    'python/triangle_utils/__init__.py',
    'python/triangle_utils/_features.py',
    'python/triangle_utils/_library.py',
    pure: false,
    subdir: 'triangle_utils',
  )
  install_data(
    'LICENSE',
    'LICENSE-Triangle',
    'NOTICE',
    install_dir: python.get_install_dir(pure: false) / 'triangle_utils',
    install_tag: 'runtime',
  )
else
  # GoogleTest is built as a static library, as in gridgen
  gtest_dep = dependency(
    'gtest_main',
    version: '>=1.17',
    default_options: ['default_library=static'],
  )

  subdir('test')
endif
