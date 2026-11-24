# triangle-utils

Utilities that call [Triangle](https://github.com/MODFLOW-ORG/triangle) to
work with two-dimensional triangular meshes:

- **Refine** an existing mesh, using a maximum area for each triangle
- **Improve** mesh quality by moving vertices and triangulating again
- **Convert** a triangular mesh to a Voronoi mesh clipped to the domain, for
  MODFLOW 6 DISV grids

## Design

The utilities are a C++20 library with a C API, so they can be called in
memory from C, C++, Python (`ctypes`), and Fortran (`iso_c_binding`). A
command-line program reads Triangle's file formats (`.node`, `.poly`, `.ele`)
and calls the same API.

## C API

`triangle_utils.h` declares the API and documents each function. A function
that can fail returns a status code, and `triangle_utils_last_error()` gives
the reason, including Triangle's own when it cannot mesh the input. A mesh is
an opaque handle with create and free functions. The API also returns the
versions of triangle-utils and Triangle, the license, and Triangle's
copyright notice. Calls must not run concurrently, since Triangle keeps
global state.

## License

The code in this repository is released under CC0 1.0 (see [LICENSE](LICENSE)).
Triangle, which is built and linked into the utilities, is copyright Jonathan
Richard Shewchuk and is distributed under its own terms (see the
[Triangle license](https://github.com/MODFLOW-ORG/triangle/blob/main/LICENSE)):
it may be redistributed only free of charge, with its copyright notices, and
distribution as part of a commercial system requires an arrangement with the
author. These terms apply to any program or library built from this
repository.
