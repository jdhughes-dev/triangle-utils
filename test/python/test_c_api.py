"""
Tests the C API through ctypes.

The library is named by the TRIANGLE_UTILS_LIBRARY environment variable,
which meson test sets.

Cases:
  - test_version       : the version string matches the version numbers,
                         and null arguments are ignored.
  - test_triangle      : Triangle's version is 1.6 and its release is set.
  - test_license       : the license names CC0, and Triangle's copyright
                         names its author and its terms.
  - test_mesh          : a mesh of the unit square reports its counts and
                         clears the last error of an earlier failure.
  - test_empty_mesh    : a mesh with no vertices or triangles is valid.
  - test_mesh_error    : a clockwise triangle, a corner out of range, and a
                         vertex not finite are errors with their reason, and
                         the mesh is null.
  - test_neighbors     : a mesh takes neighbors across its edges and rejects
                         wrong ones and a wrong count, unchanged.
  - test_invalid       : null pointers and negative counts are invalid
                         arguments, and the mesh is null.
  - test_fixed_vertices : a mesh takes fixed vertices and returns them, and
                         rejects one out of range, a negative or wrong count,
                         and a null array, unchanged.
  - test_grid          : the Voronoi and triangle grids of a square report
                         their counts, clockwise cells, centers, and sources;
                         wrong counts, a bad center, and null pointers are
                         invalid arguments, and the grid is null.
  - test_grid_create   : a grid from arrays maps a line to its cells; bad
                         offsets and null pointers are invalid arguments, and
                         the result is null.
  - test_improve       : an interior vertex moves to the middle of a square
                         unless it is fixed; a bad smoothing and a null result
                         pointer are invalid arguments, and the result is null.
"""

import ctypes
import math

import pytest
from triangle_utils._library import (
    CELL_LENGTHS_POINTER,
    ERROR,
    GRID_POINTER,
    INVALID_ARGUMENT,
    MESH_POINTER,
    OK,
)


def not_null():
    """A mesh pointer that is not null, to check that a failure resets it."""
    return ctypes.cast(ctypes.c_void_p(1), MESH_POINTER)


def create_mesh(library, vertex_xy, triangle_corners):
    """Create a mesh and return its status and pointer."""
    vertices = (ctypes.c_double * len(vertex_xy))(*vertex_xy)
    corners = (ctypes.c_int * len(triangle_corners))(*triangle_corners)
    mesh = not_null()
    status = library.triangle_utils_mesh_create(
        len(vertex_xy) // 2,
        vertices,
        len(triangle_corners) // 3,
        corners,
        ctypes.byref(mesh),
    )
    return status, mesh


def test_version(library):
    numbers = [ctypes.c_int(-1) for _ in range(3)]
    library.triangle_utils_version_numbers(*[ctypes.byref(n) for n in numbers])
    version = library.triangle_utils_version().decode()
    assert version == ".".join(str(n.value) for n in numbers)
    library.triangle_utils_version_numbers(None, None, None)


def test_triangle(library):
    assert library.triangle_utils_triangle_version() == b"1.6"
    assert library.triangle_utils_triangle_release().startswith(b"1.6.")


def test_license(library):
    assert b"CC0 1.0" in library.triangle_utils_license()
    notice = library.triangle_utils_triangle_copyright()
    assert b"Jonathan Richard Shewchuk" in notice
    assert b"commercial system" in notice


def test_mesh(library):
    status, _ = create_mesh(library, [0.0, 0.0, 1.0, 0.0, 1.0, 1.0], [0, 2, 1])
    assert status == ERROR
    status, mesh = create_mesh(
        library, [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0], [0, 1, 2, 0, 2, 3]
    )
    try:
        assert status == OK
        assert library.triangle_utils_last_error() == b""
        vertex_count = ctypes.c_int(-1)
        triangle_count = ctypes.c_int(-1)
        status = library.triangle_utils_mesh_counts(
            mesh, ctypes.byref(vertex_count), ctypes.byref(triangle_count)
        )
        assert status == OK
        assert (vertex_count.value, triangle_count.value) == (4, 2)
    finally:
        library.triangle_utils_mesh_free(mesh)


def test_empty_mesh(library):
    status, mesh = create_mesh(library, [], [])
    try:
        assert status == OK
        vertex_count = ctypes.c_int(-1)
        triangle_count = ctypes.c_int(-1)
        status = library.triangle_utils_mesh_counts(
            mesh, ctypes.byref(vertex_count), ctypes.byref(triangle_count)
        )
        assert status == OK
        assert (vertex_count.value, triangle_count.value) == (0, 0)
    finally:
        library.triangle_utils_mesh_free(mesh)


def test_mesh_error(library):
    status, mesh = create_mesh(library, [0.0, 0.0, 1.0, 0.0, 1.0, 1.0], [0, 2, 1])
    assert status == ERROR
    assert not mesh
    assert b"triangle 0 is not counterclockwise" in library.triangle_utils_last_error()
    status, mesh = create_mesh(library, [0.0, 0.0, 1.0, 0.0, 1.0, 1.0], [0, 1, 3])
    assert status == ERROR
    assert not mesh
    assert b"refers to vertex 3" in library.triangle_utils_last_error()
    status, mesh = create_mesh(library, [0.0, 0.0, math.nan, 0.0], [])
    assert status == ERROR
    assert not mesh
    assert library.triangle_utils_last_error() == (
        b"vertex 1 has a coordinate that is not finite"
    )


def test_neighbors(library):
    status, mesh = create_mesh(
        library, [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0], [0, 1, 2, 0, 2, 3]
    )
    try:
        assert status == OK
        right = (ctypes.c_int * 6)(-1, 1, -1, -1, -1, 0)
        wrong = (ctypes.c_int * 6)(-1, -1, -1, -1, -1, 0)
        assert library.triangle_utils_mesh_set_neighbors(mesh, 2, right) == OK
        assert library.triangle_utils_mesh_set_neighbors(mesh, 2, wrong) == ERROR
        assert library.triangle_utils_last_error().startswith(
            b"triangle 0 has neighbor -1 opposite corner 1"
        )
        status = library.triangle_utils_mesh_set_neighbors(mesh, 1, right)
        assert status == INVALID_ARGUMENT
        assert library.triangle_utils_last_error() == (
            b"the triangle count is 1, but the mesh has 2 triangles"
        )
        status = library.triangle_utils_mesh_set_neighbors(mesh, 2, None)
        assert status == INVALID_ARGUMENT
    finally:
        library.triangle_utils_mesh_free(mesh)


def test_invalid(library):
    status = library.triangle_utils_mesh_create(0, None, 0, None, None)
    assert status == INVALID_ARGUMENT
    assert library.triangle_utils_last_error() == b"the mesh pointer is null"
    vertices = (ctypes.c_double * 6)(0.0, 0.0, 1.0, 0.0, 0.0, 1.0)
    for arguments, reason in (
        ((-1, None, 0, None), b"vertex count is negative (-1)"),
        ((0, None, -2, None), b"triangle count is negative (-2)"),
        ((3, None, 0, None), b"vertex coordinate pointer is null"),
        ((3, vertices, 1, None), b"triangle corner pointer is null"),
    ):
        mesh = not_null()
        status = library.triangle_utils_mesh_create(*arguments, ctypes.byref(mesh))
        assert status == INVALID_ARGUMENT
        assert not mesh
        assert reason in library.triangle_utils_last_error()
    count = ctypes.c_int()
    status = library.triangle_utils_mesh_counts(
        None, ctypes.byref(count), ctypes.byref(count)
    )
    assert status == INVALID_ARGUMENT
    assert library.triangle_utils_last_error() == b"the mesh pointer is null"
    status, mesh = create_mesh(library, [], [])
    try:
        status = library.triangle_utils_mesh_counts(mesh, None, ctypes.byref(count))
        assert status == INVALID_ARGUMENT
        assert b"vertex count pointer is null" in library.triangle_utils_last_error()
        status = library.triangle_utils_mesh_counts(mesh, ctypes.byref(count), None)
        assert status == INVALID_ARGUMENT
        assert b"triangle count pointer is null" in library.triangle_utils_last_error()
    finally:
        library.triangle_utils_mesh_free(mesh)
    library.triangle_utils_mesh_free(None)


def square_with_center(library, x, y):
    """A unit square with one interior vertex, in four triangles."""
    return create_mesh(
        library,
        [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0, x, y],
        [0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4],
    )


def fixed_of(library, mesh):
    count = ctypes.c_int(-1)
    assert (
        library.triangle_utils_mesh_fixed_vertex_count(mesh, ctypes.byref(count)) == OK
    )
    fixed = (ctypes.c_int * max(count.value, 1))()
    assert library.triangle_utils_mesh_fixed_vertices(mesh, count.value, fixed) == OK
    return list(fixed)[: count.value]


def test_fixed_vertices(library):
    status, mesh = square_with_center(library, 0.3, 0.4)
    try:
        assert status == OK
        assert fixed_of(library, mesh) == []
        fixed = (ctypes.c_int * 2)(4, 1)
        assert library.triangle_utils_mesh_set_fixed_vertices(mesh, 2, fixed) == OK
        assert fixed_of(library, mesh) == [4, 1]
        out_of_range = (ctypes.c_int * 1)(5)
        status = library.triangle_utils_mesh_set_fixed_vertices(mesh, 1, out_of_range)
        assert status == ERROR
        assert b"fixed vertex 5 is not a vertex of the mesh" in (
            library.triangle_utils_last_error()
        )
        status = library.triangle_utils_mesh_set_fixed_vertices(mesh, -1, fixed)
        assert status == INVALID_ARGUMENT
        status = library.triangle_utils_mesh_set_fixed_vertices(mesh, 1, None)
        assert status == INVALID_ARGUMENT
        assert fixed_of(library, mesh) == [4, 1]
        status = library.triangle_utils_mesh_fixed_vertices(mesh, 1, fixed)
        assert status == INVALID_ARGUMENT
    finally:
        library.triangle_utils_mesh_free(mesh)


def improved_center(library, mesh):
    improved = not_null()
    status = library.triangle_utils_mesh_improve(
        mesh, 0, 0.0, 0.0, 100, 1e-4, ctypes.byref(improved)
    )
    assert status == OK
    try:
        xy = (ctypes.c_double * 10)()
        assert library.triangle_utils_mesh_vertices(improved, 5, xy) == OK
        return xy[8], xy[9]
    finally:
        library.triangle_utils_mesh_free(improved)


def test_improve(library):
    status, mesh = square_with_center(library, 0.3, 0.4)
    try:
        assert status == OK
        x, y = improved_center(library, mesh)
        assert math.isclose(x, 0.5, abs_tol=1e-3)
        assert math.isclose(y, 0.5, abs_tol=1e-3)
        fixed = (ctypes.c_int * 1)(4)
        assert library.triangle_utils_mesh_set_fixed_vertices(mesh, 1, fixed) == OK
        assert improved_center(library, mesh) == (0.3, 0.4)
        improved = not_null()
        status = library.triangle_utils_mesh_improve(
            mesh, 2, 0.0, 0.0, 100, 1e-4, ctypes.byref(improved)
        )
        assert status == INVALID_ARGUMENT
        assert not improved
        assert b"the smoothing is 2" in library.triangle_utils_last_error()
        status = library.triangle_utils_mesh_improve(mesh, 0, 0.0, 0.0, 100, 1e-4, None)
        assert status == INVALID_ARGUMENT
    finally:
        library.triangle_utils_mesh_free(mesh)


def test_grid(library):
    status, mesh = square_with_center(library, 0.5, 0.5)
    try:
        assert status == OK
        for build in (
            lambda grid: library.triangle_utils_mesh_voronoi(mesh, grid),
            lambda grid: library.triangle_utils_mesh_triangle_grid(mesh, 1, grid),
        ):
            grid = ctypes.cast(ctypes.c_void_p(1), GRID_POINTER)
            assert build(ctypes.byref(grid)) == OK
            try:
                counts = [ctypes.c_int(-1) for _ in range(3)]
                status = library.triangle_utils_grid_counts(
                    grid, *[ctypes.byref(count) for count in counts]
                )
                assert status == OK
                vertex_count, cell_count, corner_count = (c.value for c in counts)
                xy = (ctypes.c_double * (2 * vertex_count))()
                assert (
                    library.triangle_utils_grid_vertices(grid, vertex_count, xy) == OK
                )
                offsets = (ctypes.c_int * (cell_count + 1))()
                corners = (ctypes.c_int * corner_count)()
                status = library.triangle_utils_grid_cells(
                    grid, cell_count, offsets, corner_count, corners
                )
                assert status == OK
                assert offsets[cell_count] == corner_count
                for cell in range(cell_count):
                    ring = corners[offsets[cell] : offsets[cell + 1]]
                    x = [xy[2 * v] for v in ring]
                    y = [xy[2 * v + 1] for v in ring]
                    twice = sum(
                        x[i] * y[(i + 1) % len(ring)] - x[(i + 1) % len(ring)] * y[i]
                        for i in range(len(ring))
                    )
                    assert twice < 0
                centers = (ctypes.c_double * (2 * cell_count))()
                assert (
                    library.triangle_utils_grid_centers(grid, cell_count, centers) == OK
                )
                sources = (ctypes.c_int * cell_count)()
                assert (
                    library.triangle_utils_grid_sources(grid, cell_count, sources) == OK
                )
                assert list(sources) == list(range(cell_count))
                count = ctypes.c_int(-1)
                status = library.triangle_utils_grid_attribute_count(
                    grid, ctypes.byref(count)
                )
                assert (status, count.value) == (OK, 0)
                assert library.triangle_utils_grid_attributes(grid, 0, None) == OK
                status = library.triangle_utils_grid_vertices(
                    grid, vertex_count + 1, xy
                )
                assert status == INVALID_ARGUMENT
                assert (
                    library.triangle_utils_last_error()
                    == (
                        f"the vertex count is {vertex_count + 1}, but the grid has "
                        f"{vertex_count}"
                    ).encode()
                )
                status = library.triangle_utils_grid_cells(
                    grid, cell_count, offsets, corner_count - 1, corners
                )
                assert status == INVALID_ARGUMENT
            finally:
                library.triangle_utils_grid_free(grid)
        # the Voronoi grid has a cell per vertex, the triangle grid one per
        # triangle
        grid = GRID_POINTER()
        assert library.triangle_utils_mesh_voronoi(mesh, ctypes.byref(grid)) == OK
        counts = [ctypes.c_int() for _ in range(3)]
        library.triangle_utils_grid_counts(grid, *[ctypes.byref(c) for c in counts])
        library.triangle_utils_grid_free(grid)
        assert counts[1].value == 5
        grid = ctypes.cast(ctypes.c_void_p(1), GRID_POINTER)
        status = library.triangle_utils_mesh_triangle_grid(mesh, 2, ctypes.byref(grid))
        assert status == INVALID_ARGUMENT
        assert not grid
        assert b"the center is 2" in library.triangle_utils_last_error()
        assert library.triangle_utils_mesh_voronoi(mesh, None) == INVALID_ARGUMENT
        grid = ctypes.cast(ctypes.c_void_p(1), GRID_POINTER)
        status = library.triangle_utils_mesh_voronoi(None, ctypes.byref(grid))
        assert status == INVALID_ARGUMENT
        assert not grid
    finally:
        library.triangle_utils_mesh_free(mesh)


def test_grid_create(library):
    # the unit square as two cells, split at x = 0.5
    xy = (ctypes.c_double * 12)(0, 0, 0.5, 0, 1, 0, 1, 1, 0.5, 1, 0, 1)
    offsets = (ctypes.c_int * 3)(0, 4, 8)
    corners = (ctypes.c_int * 8)(0, 5, 4, 1, 1, 4, 3, 2)
    centers = (ctypes.c_double * 4)(0.25, 0.5, 0.75, 0.5)
    grid = ctypes.cast(ctypes.c_void_p(1), GRID_POINTER)
    status = library.triangle_utils_grid_create(
        6, xy, 2, offsets, 8, corners, centers, ctypes.byref(grid)
    )
    assert status == OK
    try:
        line = (ctypes.c_double * 4)(0, 0.5, 1, 0.5)
        result = ctypes.cast(ctypes.c_void_p(1), CELL_LENGTHS_POINTER)
        status = library.triangle_utils_grid_line_cells(
            grid, 2, line, ctypes.byref(result)
        )
        assert status == OK
        try:
            count = ctypes.c_int(-1)
            assert (
                library.triangle_utils_cell_lengths_count(result, ctypes.byref(count))
                == OK
            )
            assert count.value == 2
            cells = (ctypes.c_int * 2)()
            starts = (ctypes.c_double * 2)()
            lengths = (ctypes.c_double * 2)()
            status = library.triangle_utils_cell_lengths(
                result, 2, cells, starts, lengths
            )
            assert status == OK
            assert list(cells) == [0, 1]
            assert list(starts) == pytest.approx([0.0, 0.5])
            assert list(lengths) == pytest.approx([0.5, 0.5])
            status = library.triangle_utils_cell_lengths(
                result, 3, cells, starts, lengths
            )
            assert status == INVALID_ARGUMENT
            status = library.triangle_utils_cell_lengths(
                result, 2, None, starts, lengths
            )
            assert status == INVALID_ARGUMENT
            assert (
                library.triangle_utils_cell_lengths_count(result, None)
                == INVALID_ARGUMENT
            )
            status = library.triangle_utils_cell_lengths_count(
                None, ctypes.byref(count)
            )
            assert status == INVALID_ARGUMENT
        finally:
            library.triangle_utils_cell_lengths_free(result)
        result = ctypes.cast(ctypes.c_void_p(1), CELL_LENGTHS_POINTER)
        status = library.triangle_utils_grid_line_cells(
            grid, 1, line, ctypes.byref(result)
        )
        assert status == ERROR
        assert not result
        assert b"the line has 1 points" in library.triangle_utils_last_error()
    finally:
        library.triangle_utils_grid_free(grid)
    bad = (ctypes.c_int * 3)(0, 4, 7)
    grid = ctypes.cast(ctypes.c_void_p(1), GRID_POINTER)
    status = library.triangle_utils_grid_create(
        6, xy, 2, bad, 8, corners, centers, ctypes.byref(grid)
    )
    assert status == INVALID_ARGUMENT
    assert not grid
    assert (
        b"must run from 0 to the corner count, 8" in library.triangle_utils_last_error()
    )
