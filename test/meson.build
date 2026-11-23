foreach name : ['triangulate', 'version']
  test(
    name,
    executable(
      'test_' + name,
      'test_' + name + '.cpp',
      dependencies: [triangle_dep, gtest_dep, version_dep],
    ),
  )
endforeach
