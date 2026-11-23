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
  'mesh.cpp',
  dependencies: triangle_dep,
)
core_dep = declare_dependency(
  include_directories: include_directories('.'),
  link_with: core_lib,
  dependencies: [triangle_dep, version_dep],
)
