# Plan

The order of work, the dependency on MODFLOW-ORG/triangle, and the decisions
that hold across issues. The issues hold the scope of each step; this file
does not repeat it. Read it before starting an issue or proposing a release,
and update it in the pull request that changes the plan.

## First release (0.1.0)

Every open issue, in this order; each depends only on those before it
(see its `Depends on` lines and comments). Done: #2 (build), #3 (CI), #4
(core mesh type and property checks).

1. #5 C API skeleton and ctypes tests
2. #6 Triangle files and the command-line program
3. #11 generate a mesh from geometry
4. #7 refine
5. #8 improve by moving vertices
6. #9 Voronoi and DISV
7. #12 map lines and boundaries to cells
8. #13 DISU connection data
9. #19 layers, DISV and DISU output
10. #23 ghost-node correction
11. #14 grid quality report
12. #18 model checks with MODFLOW 6, MODFLOW-USG, and MODFLOW-USG-Transport
13. #20 different refinement by layer
14. #15 structured grids, merging cells, and refinement by a field
15. #21 user guide
16. #22 Read the Docs
17. #24 release notes
18. #10 release and conda-forge package

## Triangle

- Triangle is built from MODFLOW-ORG/triangle as a static subproject
  (`subprojects/triangle.wrap`), now commit 5cee628 on Triangle's `main`
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

## Decisions

- **Compilers:** gcc 15 from conda-forge on Linux and Windows (the `gcc`
  pixi environment); Homebrew gcc on macOS, since programs the conda-forge
  g++ builds there link the conda-forge libc++ and corrupt `std::string`.
- **Floating point:** the C++ has gridgen's options (`meson.build`):
  `-ffp-contract=off`, and `-fp-model=precise` or `/fp:precise` for icx and
  MSVC. Triangle's C keeps its own, which are stricter (`/fp:strict` for
  icx on Windows), since its exact predicates need them.
- **Obtuse triangles:** refinement by maximum angle stops at about 95 to
  100 degrees (#7); moving vertices removes the rest (#8).
- **Corrections for triangle grids**, in order (#14, #21): a Voronoi grid
  or a triangle grid with circumcenters and no obtuse triangles needs none,
  so a grid with obtuse triangles is first refined by maximum angle (#7)
  and has its vertices moved (#8); then ghost-node correction (#23); then
  XT3D where accuracy matters more than run time. MODFLOW-USG has no XT3D,
  so there the choices end at GNC. `XT3D RHS` is not a cheaper XT3D.

## Open questions

- **Triangle's error text:** Triangle prints the reason for an error on
  standard output, so the C API's last-error message (#5) cannot include
  it without a change to Triangle. Decide in #5.
- **Python wrapper:** no issue covers one; #5 has only ctypes tests. Decide
  before #19, which returns arrays for FloPy.
