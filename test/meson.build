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
