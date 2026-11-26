# Plan

The order of work, the dependency on MODFLOW-ORG/triangle, and the decisions
that hold across issues. The issues hold the scope of each step; this file
does not repeat it. Read it before starting an issue or proposing a release,
and update it in the pull request that changes the plan.

## First release (0.1.0)

Every open issue, in this order; each depends only on those before it
(see its `Depends on` lines and comments). Done: #2 (build), #3 (CI), #4
(core mesh type and property checks), #5 (C API skeleton and ctypes
tests), #6 (Triangle files and the command-line program), #11 (generate a
mesh from geometry), #7 (refine), #40 (start the user guide), #42 (a point
on an edge splits it), #8 (improve by moving vertices), #9 (Voronoi and
triangle grids for DISV), #12 (the cells of points, lines, and boundaries),
#13 (DISU connection data), #46 (the Python package's wheels), #55 (a
tributary in the guide's example), #50 (features from GeoDataFrames and
shapely geometries in Python).

1. #19 layers, DISV and DISU output
2. #51 read shapefiles in the C++ library with shapelib
3. #52 evaluate reading GeoPackage and FlatGeobuf in the C++ library
4. #23 ghost-node correction
5. #43 hybrid grid, Voronoi cells around features and triangles elsewhere
6. #44 a point feature centered in a triangle cell, evaluated before the
   release: shipped as an option, or closed with its findings
7. #14 grid quality report
8. #18 model checks with MODFLOW 6, MODFLOW-USG, and MODFLOW-USG-Transport
9. #20 different refinement by layer
10. #15 structured grids, merging cells, and refinement by a field
11. #21 user guide
12. #22 Read the Docs
13. #24 release notes
14. #10 release and conda-forge package

## Triangle

- Triangle is built from MODFLOW-ORG/triangle as a static subproject
  (`subprojects/triangle.wrap`), now commit 7890e27 on Triangle's `main`
  (after v1.6.2).
- A Triangle change triangle-utils needs is a pull request to Triangle's
  `main`, with no Triangle release (#10). The wrap then points at that
  commit, through GitHub's archive of it (`archive/<sha>.tar.gz`) with its
  hash.
- One Triangle release is cut just before #10, with every change made
  since 1.6.2, and the wrap moves to its `triangle_source.zip`. Do not run
  that release's workflow again once the wrap uses it: it rebuilds the
  archive, whose hash then changes (Releases in Triangle's `AGENTS.md`).
- Before proposing that release, check every open issue here for what it
  needs from Triangle.
- Changes on Triangle's `main` for triangle-utils:
  - MODFLOW-ORG/triangle#59 (merged in #60, 5cee628): build with icx on
    Windows, with `/fp:strict`; the meshes match the MSVC build.
  - MODFLOW-ORG/triangle#43 (in 1.6.2): the `u` switch's test,
    `triangle_set_unsuitable()`, which refining by maximum angle uses (#7).
  - MODFLOW-ORG/triangle#61 (merged in #62, fd28514): the library keeps
    Triangle's messages (`triangle_messages()`) and can stop printing them
    (`triangle_set_print()`).
  - MODFLOW-ORG/triangle#63 (merged in #64, 7890e27): the exact
    arithmetic no longer reads past the end of an array, which stopped the
    sanitizer tests of #42; results and speed are unchanged.

## Decisions

- **Compilers:** gcc 15 from conda-forge on Linux and Windows (the `gcc`
  pixi environment); Homebrew gcc on macOS, since programs the conda-forge
  g++ builds there link the conda-forge libc++ and corrupt `std::string`.
- **Floating point:** the C++ has gridgen's options (`meson.build`):
  `-ffp-contract=off`, and `-fp-model=precise` or `/fp:precise` for icx and
  MSVC. Triangle's C keeps its own, which are stricter (`/fp:strict` for
  icx on Windows), since its exact predicates need them.
- **Triangle's errors:** the core calls Triangle through `run_triangle()`,
  which turns off Triangle's printing and throws its reason, so the C API's
  last error includes it (#5, MODFLOW-ORG/triangle#61).
- **Obtuse triangles:** refinement by maximum angle stops at about 95 to
  100 degrees (#7); moving vertices removes the rest away from the
  boundary (#8). A boundary point where two triangles meet keeps an obtuse
  angle if it has one, since its two angles sum to 180 degrees; removing it
  needs a third triangle there, which only refinement can add.
- **Cells on interior boundaries:** a Voronoi cell on a segment inside the
  mesh, such as a lake's shore, spans both sides, centered on its vertex,
  and takes the attributes of the side with most of its area (#9). Every
  connection stays perpendicular to its face; splitting the cell along the
  segment would need centers off the vertex and a correction, and faces on
  the shore need mirrored points, as in #43.
- **Corrections for triangle grids**, in order (#14, #21): a Voronoi grid
  or a triangle grid with circumcenters and no obtuse triangles needs none,
  so a grid with obtuse triangles is first refined by maximum angle (#7)
  and has its vertices moved (#8); then ghost-node correction (#23); then
  XT3D where accuracy matters more than run time. MODFLOW-USG has no XT3D,
  so there the choices end at GNC. `XT3D RHS` is not a cheaper XT3D.

## Release

- **Windows runtime DLLs:** the Windows DLL depends on its compiler's
  runtime DLLs (the Intel build failed to load from Python without them in
  #38), which Python since 3.8 does not look for on `PATH`; the tests load
  it with `winmode=0`. #46 links the runtimes statically in the wheels
  (`b_vscrt=mt`, and `-static-libstdc++` and `-static-libgcc` with gcc), so
  a wheel needs nothing beside it; the release (#10) does the same for its
  archives.
- **PyPI:** #46 builds and tests the wheels in CI but publishes nothing. The
  wheels go to PyPI, as `triangle-utils`, only with the first release
  (#10), from the same tag as the release archives and the conda-forge
  package. #10 also decides how a wheel's Triangle source is made available:
  `NOTICE` gives the wrap's archive URL, and there is no sdist yet.
- **Wheel platforms:** Linux (manylinux) x86_64 and aarch64, macOS arm64,
  and Windows x86_64, each an abi3 wheel for CPython 3.11 and later. macOS
  on Intel is not supported (decided in #46): Triangle's CI does not test it.

## User guide

- The guide (`doc/UserGuide/`) grows with the features: each feature's
  pull request adds its section and figures (#40, `AGENTS.md`), so the
  meshes are looked at as each is built. #21 keeps what needs every
  feature: the layered DISU grid, the model checks, and the release notes.

## Open questions

- **Python wrapper:** `python/triangle_utils/` (#40) is a minimal module,
  meshes in and out as NumPy arrays, packaged with the library inside each
  wheel (#46); #19 decides what it returns for FloPy.
