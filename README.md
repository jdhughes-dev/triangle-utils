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
minimum angle, a distance within which points are merged, and conforming
Delaunay triangles if a Voronoi grid will follow. The mesh covers the polygons,
less everything inside a hole polygon, including polygons, lines, and rings
there. A region's attribute and limit spread only up to segments, so a line
across a polygon, or a ring, needs a region on each side. Where segments
repeat, the first one's marker is kept, and segments with marker 0 on the
mesh's boundary get marker 1, as Triangle gives them. The API also returns the
versions of triangle-utils and Triangle, the license, and Triangle's copyright
notice. Calls must not run concurrently, since Triangle keeps global state.

## Command line

```shell
triangle-utils generate [options] <base>  # mesh <base>.poly to <base>.1.*
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

The program exits with 0 on success, 1 on an error, and 2 on a usage error.

## License

The code in this repository is released under CC0 1.0 (see [LICENSE](LICENSE)).
Triangle, which is built and linked into the utilities, is copyright Jonathan
Richard Shewchuk and is distributed under its own terms (see the
[Triangle license](https://github.com/MODFLOW-ORG/triangle/blob/main/LICENSE)):
it may be redistributed only free of charge, with its copyright notices, and
distribution as part of a commercial system requires an arrangement with the
author. These terms apply to any program or library built from this
repository.
