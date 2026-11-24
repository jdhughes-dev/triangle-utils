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
  - test_invalid       : null pointers and negative counts are invalid
                         arguments, and the mesh is null.
"""

import ctypes
import math
import os

import pytest

OK = 0
ERROR = 1
INVALID_ARGUMENT = 2


class Mesh(ctypes.Structure):
    """The opaque mesh."""


MESH_POINTER = ctypes.POINTER(Mesh)


@pytest.fixture(scope="module")
def library():
    library = ctypes.CDLL(os.environ["TRIANGLE_UTILS_LIBRARY"])
    for name in (
        "last_error",
        "version",
        "triangle_version",
        "triangle_release",
        "license",
        "triangle_copyright",
    ):
        function = getattr(library, f"triangle_utils_{name}")
        function.restype = ctypes.c_char_p
        function.argtypes = []
    library.triangle_utils_version_numbers.restype = None
    library.triangle_utils_version_numbers.argtypes = [ctypes.POINTER(ctypes.c_int)] * 3
    library.triangle_utils_mesh_create.restype = ctypes.c_int
    library.triangle_utils_mesh_create.argtypes = [
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_double),
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(MESH_POINTER),
    ]
    library.triangle_utils_mesh_free.restype = None
    library.triangle_utils_mesh_free.argtypes = [MESH_POINTER]
    library.triangle_utils_mesh_counts.restype = ctypes.c_int
    library.triangle_utils_mesh_counts.argtypes = [
        MESH_POINTER,
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
    ]
    return library


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
