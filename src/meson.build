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
