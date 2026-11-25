# the version as macros, for a caller to check at compile time
version_parts = meson.project_version().split('.')
version_data = configuration_data()
version_data.set_quoted('TRIANGLE_UTILS_VERSION', meson.project_version())
version_data.set('TRIANGLE_UTILS_VERSION_MAJOR', version_parts[0])
version_data.set('TRIANGLE_UTILS_VERSION_MINOR', version_parts[1])
version_data.set('TRIANGLE_UTILS_VERSION_PATCH', version_parts[2])
version_header = configure_file(
  input: 'triangle_utils_version.h.in',
  output: 'triangle_utils_version.h',
  configuration: version_data,
)
install_headers(version_header)

version_dep = declare_dependency(
  include_directories: include_directories('.'),
  sources: version_header,
)

# the core, which works on meshes in memory; the C API and the command line
# are built on it
core_lib = static_library(
  'triangle_utils_core',
  'connections.cpp',
  'features.cpp',
  'geometry.cpp',
  'grid.cpp',
  'improve.cpp',
  'mesh.cpp',
  'refine.cpp',
  'run_triangle.cpp',
  dependencies: triangle_dep,
)
core_dep = declare_dependency(
  include_directories: include_directories('.'),
  link_with: core_lib,
  dependencies: [triangle_dep, version_dep],
)

# the C API as a shared library that exports only the API; Windows uses
# triangle_utils.def
export_args = []
export_files = []
if host_machine.system() == 'darwin'
  export_args = ['-Wl,-exported_symbols_list,' + meson.current_source_dir() / 'triangle_utils.exports']
  export_files = ['triangle_utils.exports']
elif host_machine.system() != 'windows'
  export_args = ['-Wl,--version-script=' + meson.current_source_dir() / 'triangle_utils.map']
  export_files = ['triangle_utils.map']
endif
api_lib = shared_library(
  'triangle_utils',
  'triangle_utils.cpp',
  dependencies: core_dep,
  name_prefix: 'lib',
  version: meson.project_version(),
  soversion: version_parts[0],
  vs_module_defs: 'triangle_utils.def',
  link_args: export_args,
  link_depends: export_files,
  install: true,
)
install_headers('triangle_utils.h')
api_dep = declare_dependency(
  include_directories: include_directories('.'),
  link_with: api_lib,
  dependencies: version_dep,
)

# Triangle's files and the command line, built on the C API
triangle_headers_dep = triangle_dep.partial_dependency(includes: true)
files_lib = static_library(
  'triangle_utils_files',
  'modflow_files.cpp',
  'triangle_files.cpp',
  dependencies: triangle_headers_dep,
)
files_dep = declare_dependency(
  include_directories: include_directories('.'),
  link_with: files_lib,
  dependencies: triangle_headers_dep,
)
program_rpath = host_machine.system() == 'darwin' ? '@loader_path/../lib' : '$ORIGIN/../lib'
program = executable(
  'triangle-utils',
  'main.cpp',
  dependencies: [api_dep, files_dep],
  install: true,
  install_rpath: program_rpath,
)
