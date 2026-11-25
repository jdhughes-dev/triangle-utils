# triangle-utils

Utilities that call [Triangle](https://github.com/MODFLOW-ORG/triangle) to
work with two-dimensional triangular meshes:

- **Refine** an existing mesh, using a maximum area for each triangle
- **Improve** mesh quality by moving vertices and triangulating again
- **Convert** a triangular mesh to a Voronoi mesh clipped to the domain, for
  MODFLOW 6 DISV grids
- **Generate** a mesh from geometry in memory: polygons, holes, regions,
  points, lines the mesh follows, and rings of finer triangles around wells

## Design

The utilities are a C++20 library with a C API, so they can be called in
memory from C, C++, Python (`ctypes`), and Fortran (`iso_c_binding`). A
command-line program reads Triangle's file formats (`.node`, `.poly`, `.ele`,
`.neigh`) and calls the same API.

## C API

`triangle_utils.h` declares the API and documents each function. A function
that can fail returns a status code, and `triangle_utils_last_error()` gives
the reason, which includes Triangle's own when it cannot mesh the input. A mesh
is an opaque handle with create and free functions, and getters for its arrays.
Geometry is a handle that polygons, hole polygons, holes, regions, points,
lines, and rings are added to, each with its own spacing and marker where it
has segments; `triangle_utils_mesh_generate()` meshes it with a maximum area, a
minimum angle, a distance within which points are merged and a point near a
segment splits it (by default, 1e-12 of the largest coordinate, so only
coordinates that differ in their last digits match), and conforming Delaunay
triangles if a Voronoi grid will follow. The mesh covers the polygons, less
everything inside a hole polygon, including polygons, lines, and rings there. A
region's attribute and limit spread only up to segments, so a line across a
polygon, or a ring, needs a region on each side. Where segments repeat, the
first one's marker is kept, and segments with marker 0 on the mesh's boundary
get marker 1, as Triangle gives them. `triangle_utils_mesh_refine()` refines a
mesh by its area limits, a maximum area, and a minimum and maximum angle,
keeping the segments, triangle attributes, and area limits set by
`triangle_utils_mesh_set_segments()`, `_set_triangle_attributes()`, and
`_set_area_limits()`; its triangles must not overlap, and each segment must be
an edge. Triangle can reach a maximum angle of about 95 to 100 degrees, not 90,
so a set number of splits stops it, and
`triangle_utils_mesh_count_angles_over()` tells what is left. Refining gives
boundary segments with marker 0 the marker 1, as generating does. Refining
keeps triangle attributes and per-triangle area limits exact only where
segments bound them, since Triangle averages them across the edges it flips.
`triangle_utils_mesh_improve()` moves the vertices that are not fixed and flips
edges, toward a maximum angle such as 90 degrees, without making the smallest
angle smaller unless a smaller minimum angle is given; a mesh's fixed vertices,
which `generate` sets to its point features and ring centers, do not move.
`triangle_utils_mesh_voronoi()` builds the Voronoi grid of a conforming
Delaunay mesh: a cell around each vertex, centered on it, so the line between
two cells' centers is perpendicular to their shared face; a cell on the
boundary is centered on its edge, and is not convex where the boundary turns
inward; a cell on a segment inside the mesh, such as a lake's shore, spans both
sides and takes the attributes of the side with most of its area.
`triangle_utils_mesh_triangle_grid()` makes the triangles the cells, centered
on their centroids or circumcenters. Either grid lists each vertex once and
each cell's corners clockwise, as MODFLOW 6 DISV does.
`triangle_utils_grid_line_cells()` gives the cells along a line, such as a
river, in order, with the length of line in each, for stream reaches; a part
along a face is in the cell on the line's left, or on its right where none is
on the left. `triangle_utils_grid_segment_cells()` gives the cells on a mesh's
segments with a marker, such as a boundary, each once, with the length of
segment in each, for boundary conductances, in the order the segments were
digitized; a segment between two cells, as inside a triangle grid, is in only
one of them. `triangle_utils_grid_point_cells()` gives the cell containing each
point, such as a well. `triangle_utils_grid_connections()` gives, for each face
two cells share at two common corners, the distance from each center to the face along its normal,
the face's width, and the direction of its normal, for MODFLOW DISU (CL12,
HWVA, and ANGLDEGX); a center on a face or beyond it, as an obtuse triangle's
circumcenter, is an error. `triangle_utils_grid_create()` makes a grid from a
caller's arrays. The API also returns the versions of triangle-utils and
Triangle, the license, and Triangle's copyright notice. Calls must not run
concurrently, since Triangle keeps global state.

## User guide

The user guide in `doc/UserGuide/` describes the utilities for groundwater
modelers, with examples; `pixi run -e doc build-guide` builds it, and CI
uploads the PDF with each run of its workflow. The Python module in
`python/triangle_utils/` calls the library from Python, with meshes as NumPy
arrays.

## Command line

```shell
triangle-utils generate [options] <base>  # mesh <base>.poly to <base>.1.*
triangle-utils refine [options] <base>    # refine <base>.node and .ele
triangle-utils improve [options] <base>   # move its vertices
triangle-utils disv [options] <base>      # write <base>.disv for MODFLOW 6
triangle-utils cells [options] <base>     # cells of points, a line, a boundary
triangle-utils check <base>   # check <base>.node, .ele, and .neigh
triangle-utils --version      # the versions of triangle-utils and Triangle,
                              # and the MODFLOW-ORG/triangle release
triangle-utils --license      # the license, and Triangle's copyright
```

`generate` takes Triangle's maximum area (`-a`) and minimum angle (`-q`),
a spacing for points added along the segments (`-s`), a distance within
which points are merged (`-m`), conforming Delaunay triangles (`-D`), and
an output base name (`-o`); `triangle-utils generate -h` describes them.

```shell
triangle-utils generate -a 0.5 -q 30 -s 1 domain   # writes domain.1.*
```

`generate`, `refine`, and `improve` write `x.1` for input `x`, and `x.2` for
`x.1`, as Triangle names its output, unless `-o` names it; `refine` and
`improve` also read `<base>.poly` and `<base>.area` if they exist. A vertex
that improving must not move, such as a well, has marker 1 in the `.node`
files they write, and a vertex with any nonzero marker in a `.node` file they
read is kept in place; no vertex attributes are written. The program exits
with 0 on success, 1 on an error, and 2 on a usage error.

`disv` writes the Voronoi grid of the mesh in `<base>.node` and `.ele`, which
must be conforming Delaunay (`-D`), or its triangles (`-g centroid` or
`-g circumcenter`), as a one-layer MODFLOW 6 DISV file, `<base>.disv`, and
each cell's mesh vertex or triangle and attributes to `<base>.cells`.
`pixi run get-mf6` downloads MODFLOW 6 into the pixi environment.

`cells` prints the cells of the same grid that points contain (`-p`, a file
of x and y), or that a line (`-l`) or the segments of `<base>.poly` with a
marker (`-m`) pass through, with where each part starts and its length;
cells are numbered from 1, as in the DISV file.

## License

The code in this repository is released under CC0 1.0 (see [LICENSE](LICENSE)).
Triangle, which is built and linked into the utilities, is copyright Jonathan
Richard Shewchuk and is distributed under its own terms (see the
[Triangle license](https://github.com/MODFLOW-ORG/triangle/blob/main/LICENSE)):
it may be redistributed only free of charge, with its copyright notices, and
distribution as part of a commercial system requires an arrangement with the
author. These terms apply to any program or library built from this
repository.
