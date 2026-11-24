"""
Tests meshing geometry through the C API.

Cases:
  - test_generate      : a square with a hole polygon, a region, a line, a
                         point, and a ring meshes to the domain's area, with
                         the region's attribute and the mesh's arrays.
  - test_triangle_reason : two points give Triangle's own reason as the last
                         error, and no mesh.
  - test_geometry_errors : a polygon with two points is an error, and a null
                         geometry, a wrong count, and a mesh without
                         neighbors are invalid or errors.
"""

import ctypes

import pytest
from triangle_utils_api import (
    ERROR,
    GEOMETRY_POINTER,
    INVALID_ARGUMENT,
    MESH_POINTER,
    OK,
    doubles,
)

SQUARE = [0.0, 0.0, 10.0, 0.0, 10.0, 10.0, 0.0, 10.0]
HOLE = [4.0, 4.0, 6.0, 4.0, 6.0, 6.0, 4.0, 6.0]


@pytest.fixture
def geometry(library):
    created = GEOMETRY_POINTER()
    assert library.triangle_utils_geometry_create(ctypes.byref(created)) == OK
    yield created
    library.triangle_utils_geometry_free(created)


def generate(library, geometry, maximum_area=0.0, minimum_angle=0.0):
    """Mesh the geometry and return the status and mesh."""
    mesh = ctypes.cast(ctypes.c_void_p(1), MESH_POINTER)
    status = library.triangle_utils_mesh_generate(
        geometry, maximum_area, minimum_angle, 0.0, 0, ctypes.byref(mesh)
    )
    return status, mesh


def test_generate(library, geometry):
    add = library
    assert (
        add.triangle_utils_geometry_add_polygon(geometry, 4, doubles(SQUARE), 0.0, 1)
        == OK
    )
    assert (
        add.triangle_utils_geometry_add_hole_polygon(geometry, 4, doubles(HOLE), 0.0, 2)
        == OK
    )
    assert add.triangle_utils_geometry_add_region(geometry, 1.0, 1.0, 3.0, 0.0) == OK
    line = doubles([1.0, 8.0, 3.0, 9.0])
    assert add.triangle_utils_geometry_add_line(geometry, 2, line, 0.5, 4) == OK
    assert (
        add.triangle_utils_geometry_add_points(geometry, 1, doubles([8.0, 2.0])) == OK
    )
    assert (
        add.triangle_utils_geometry_add_ring(geometry, 8.0, 8.0, 1.0, 8, 5.0, 0.05, 6)
        == OK
    )
    status, mesh = generate(library, geometry, maximum_area=2.0, minimum_angle=20.0)
    try:
        assert status == OK
        vertex_count = ctypes.c_int()
        triangle_count = ctypes.c_int()
        library.triangle_utils_mesh_counts(
            mesh, ctypes.byref(vertex_count), ctypes.byref(triangle_count)
        )
        xy = (ctypes.c_double * (2 * vertex_count.value))()
        corners = (ctypes.c_int * (3 * triangle_count.value))()
        assert library.triangle_utils_mesh_vertices(mesh, vertex_count, xy) == OK
        assert (
            library.triangle_utils_mesh_triangles(mesh, triangle_count, corners) == OK
        )

        area = 0.0
        areas = []
        for triangle in range(triangle_count.value):
            a, b, c = corners[3 * triangle : 3 * triangle + 3]
            signed = 0.5 * (
                (xy[2 * b] - xy[2 * a]) * (xy[2 * c + 1] - xy[2 * a + 1])
                - (xy[2 * c] - xy[2 * a]) * (xy[2 * b + 1] - xy[2 * a + 1])
            )
            assert 0.0 < signed <= 2.0 + 1e-9
            areas.append(signed)
            area += signed
        assert area == pytest.approx(96.0)
        points = {(xy[2 * v], xy[2 * v + 1]) for v in range(vertex_count.value)}
        assert (8.0, 2.0) in points
        assert (8.0, 8.0) in points

        attribute_count = ctypes.c_int()
        library.triangle_utils_mesh_triangle_attribute_count(
            mesh, ctypes.byref(attribute_count)
        )
        assert attribute_count.value == 1
        attributes = (ctypes.c_double * triangle_count.value)()
        status = library.triangle_utils_mesh_triangle_attributes(
            mesh, triangle_count, attributes
        )
        assert status == OK
        assert set(attributes) == {3.0, 5.0}
        for value, signed in zip(attributes, areas, strict=True):
            if value == 5.0:
                assert signed <= 0.05 + 1e-12

        neighbors = (ctypes.c_int * (3 * triangle_count.value))()
        assert (
            library.triangle_utils_mesh_neighbors(mesh, triangle_count, neighbors) == OK
        )
        segment_count = ctypes.c_int()
        library.triangle_utils_mesh_segment_count(mesh, ctypes.byref(segment_count))
        ends = (ctypes.c_int * (2 * segment_count.value))()
        markers = (ctypes.c_int * segment_count.value)()
        assert (
            library.triangle_utils_mesh_segments(mesh, segment_count, ends, markers)
            == OK
        )
        assert {1, 2, 4, 6} <= set(markers)
    finally:
        library.triangle_utils_mesh_free(mesh)


def test_triangle_reason(library, geometry):
    assert (
        library.triangle_utils_geometry_add_points(geometry, 2, doubles(SQUARE[:4]))
        == OK
    )
    status, mesh = generate(library, geometry)
    assert status == ERROR
    assert not mesh
    assert library.triangle_utils_last_error() == (
        b"Triangle could not mesh the input: Input must have at least three input "
        b"vertices."
    )


def test_geometry_errors(library, geometry):
    assert (
        library.triangle_utils_geometry_add_polygon(
            geometry, 2, doubles(SQUARE[:4]), 0.0, 0
        )
        == OK
    )
    status, mesh = generate(library, geometry)
    assert status == ERROR
    assert not mesh
    assert library.triangle_utils_last_error() == (
        b"polygon 0 has 2 points, but at least 3 are needed"
    )

    status, mesh = generate(library, None)
    assert status == INVALID_ARGUMENT
    assert library.triangle_utils_geometry_add_hole(None, 0.0, 0.0) == INVALID_ARGUMENT
    status = library.triangle_utils_geometry_add_points(geometry, 1, None)
    assert status == INVALID_ARGUMENT

    # a created mesh has no neighbors, and a count must be the mesh's
    created = MESH_POINTER()
    corners = (ctypes.c_int * 3)(0, 1, 2)
    status = library.triangle_utils_mesh_create(
        3, doubles([0.0, 0.0, 1.0, 0.0, 0.0, 1.0]), 1, corners, ctypes.byref(created)
    )
    assert status == OK
    try:
        neighbors = (ctypes.c_int * 3)()
        assert library.triangle_utils_mesh_neighbors(created, 1, neighbors) == ERROR
        assert library.triangle_utils_last_error() == b"the mesh has no neighbors"
        xy = (ctypes.c_double * 6)()
        assert library.triangle_utils_mesh_vertices(created, 2, xy) == INVALID_ARGUMENT
        assert library.triangle_utils_last_error() == (
            b"the vertex count is 2, but the mesh has 3"
        )
    finally:
        library.triangle_utils_mesh_free(created)
