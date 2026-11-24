"""
Tests refining a mesh through the C API.

Cases:
  - test_refine        : a square of two triangles, with segments around and
                         between them, attributes, and an area limit on one
                         triangle, refines to that limit, keeping the area,
                         segments, and attributes.
  - test_maximum_angle : the count of angles over a limit falls to 0 when a
                         mesh is refined to it.
  - test_refine_errors : setters with a wrong count or vertex are invalid or
                         errors, leave the mesh unchanged, and a bad maximum
                         angle gives no mesh.
"""

import ctypes

import pytest
from triangle_utils_api import ERROR, INVALID_ARGUMENT, MESH_POINTER, OK, doubles

SQUARE = [0.0, 0.0, 4.0, 0.0, 4.0, 4.0, 0.0, 4.0]


def ints(values):
    """A ctypes array of ints."""
    return (ctypes.c_int * len(values))(*values)


@pytest.fixture
def square(library):
    mesh = MESH_POINTER()
    status = library.triangle_utils_mesh_create(
        4, doubles(SQUARE), 2, ints([0, 1, 2, 0, 2, 3]), ctypes.byref(mesh)
    )
    assert status == OK
    yield mesh
    library.triangle_utils_mesh_free(mesh)


def arrays(library, mesh):
    """The mesh's vertices, triangles, attributes, and segments as lists."""
    vertex_count = ctypes.c_int()
    triangle_count = ctypes.c_int()
    library.triangle_utils_mesh_counts(
        mesh, ctypes.byref(vertex_count), ctypes.byref(triangle_count)
    )
    xy = (ctypes.c_double * (2 * vertex_count.value))()
    corners = (ctypes.c_int * (3 * triangle_count.value))()
    attributes = (ctypes.c_double * triangle_count.value)()
    assert library.triangle_utils_mesh_vertices(mesh, vertex_count, xy) == OK
    assert library.triangle_utils_mesh_triangles(mesh, triangle_count, corners) == OK
    status = library.triangle_utils_mesh_triangle_attributes(
        mesh, triangle_count, attributes
    )
    assert status == OK
    segment_count = ctypes.c_int()
    library.triangle_utils_mesh_segment_count(mesh, ctypes.byref(segment_count))
    ends = (ctypes.c_int * (2 * segment_count.value))()
    markers = (ctypes.c_int * segment_count.value)()
    status = library.triangle_utils_mesh_segments(mesh, segment_count, ends, markers)
    assert status == OK
    return list(xy), list(corners), list(attributes), list(ends), list(markers)


def triangle_areas(xy, corners):
    """The signed area of each triangle."""
    areas = []
    for triangle in range(len(corners) // 3):
        a, b, c = corners[3 * triangle : 3 * triangle + 3]
        areas.append(
            0.5
            * (
                (xy[2 * b] - xy[2 * a]) * (xy[2 * c + 1] - xy[2 * a + 1])
                - (xy[2 * c] - xy[2 * a]) * (xy[2 * b + 1] - xy[2 * a + 1])
            )
        )
    return areas


def test_refine(library, square):
    # the diagonal is a segment, since Triangle averages attributes across an
    # edge it flips that is not one
    ends = ints([0, 1, 1, 2, 2, 3, 3, 0, 0, 2])
    status = library.triangle_utils_mesh_set_segments(square, 5, ends, ints([5] * 5))
    assert status == OK
    status = library.triangle_utils_mesh_set_triangle_attributes(
        square, 2, 1, doubles([1.0, 2.0])
    )
    assert status == OK
    assert (
        library.triangle_utils_mesh_set_area_limits(square, 2, doubles([0.1, 0.0]))
        == OK
    )
    refined = MESH_POINTER()
    status = library.triangle_utils_mesh_refine(
        square, 0.0, 0.0, 0.0, 0, 0, ctypes.byref(refined)
    )
    assert status == OK
    try:
        xy, corners, attributes, _, markers = arrays(library, refined)
        areas = triangle_areas(xy, corners)
        assert sum(areas) == pytest.approx(16.0)
        assert set(attributes) == {1.0, 2.0}
        assert sum(
            a for a, value in zip(areas, attributes, strict=True) if value == 1.0
        ) == (pytest.approx(8.0))
        for area, value in zip(areas, attributes, strict=True):
            if value == 1.0:
                assert area <= 0.1 + 1e-12
        assert len(corners) // 3 > 80
        assert set(markers) == {5}
    finally:
        library.triangle_utils_mesh_free(refined)


def test_maximum_angle(library, square):
    over = ctypes.c_int()
    assert (
        library.triangle_utils_mesh_count_angles_over(square, 80.0, ctypes.byref(over))
        == OK
    )
    assert over.value == 2
    refined = MESH_POINTER()
    status = library.triangle_utils_mesh_refine(
        square, 0.0, 20.0, 100.0, 1000, 0, ctypes.byref(refined)
    )
    assert status == OK
    try:
        status = library.triangle_utils_mesh_count_angles_over(
            refined, 100.0, ctypes.byref(over)
        )
        assert status == OK
        assert over.value == 0
    finally:
        library.triangle_utils_mesh_free(refined)


def test_refine_errors(library, square):
    status = library.triangle_utils_mesh_set_segments(square, 1, ints([0, 9]), None)
    assert status == ERROR
    assert library.triangle_utils_last_error() == (
        b"segment 0 refers to vertex 9, but the mesh has 4 vertices"
    )
    status = library.triangle_utils_mesh_set_area_limits(square, 3, doubles([1.0] * 3))
    assert status == INVALID_ARGUMENT
    status = library.triangle_utils_mesh_set_triangle_attributes(square, 2, -1, None)
    assert status == INVALID_ARGUMENT
    segment_count = ctypes.c_int(-1)
    library.triangle_utils_mesh_segment_count(square, ctypes.byref(segment_count))
    assert segment_count.value == 0

    refined = ctypes.cast(ctypes.c_void_p(1), MESH_POINTER)
    status = library.triangle_utils_mesh_refine(
        square, 0.0, 0.0, 45.0, 0, 0, ctypes.byref(refined)
    )
    assert status == ERROR
    assert not refined
    assert library.triangle_utils_last_error().startswith(b"the maximum angle is 45")
