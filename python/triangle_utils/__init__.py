"""
Python access to triangle-utils through its C API.

Meshes go in and out as NumPy arrays; geometry is added to a Geometry and
meshed with generate(). This is the interface the user guide's examples use;
the full Python interface is decided before MODFLOW grids are written (#19).
"""

import ctypes
from dataclasses import dataclass, field

import numpy as np

from ._library import GEOMETRY_POINTER, MESH_POINTER, OK, load

__all__ = [
    "Error",
    "Geometry",
    "Mesh",
    "count_angles_over",
    "generate",
    "refine",
    "version",
]

_library = None


def _api():
    """The library, opened on first use."""
    global _library
    if _library is None:
        _library = load()
    return _library


class Error(RuntimeError):
    """An error the library reports, with its reason."""


def _check(status):
    if status != OK:
        raise Error(_api().triangle_utils_last_error().decode())


# _doubles and _ints return the array with its pointer: the array must be kept
# until the call that reads the pointer returns


def _doubles(values):
    array = np.ascontiguousarray(values, dtype=np.float64)
    return array, array.ctypes.data_as(ctypes.POINTER(ctypes.c_double))


def _ints(values):
    array = np.ascontiguousarray(values, dtype=np.intc)
    return array, array.ctypes.data_as(ctypes.POINTER(ctypes.c_int))


@dataclass
class Mesh:
    """A triangular mesh; indices are zero based."""

    # x and y of each vertex, shape (vertex count, 2)
    vertices: np.ndarray
    # corners of each triangle, counterclockwise, shape (triangle count, 3)
    triangles: np.ndarray
    # attributes of each triangle, shape (triangle count, attribute count)
    attributes: np.ndarray = None
    # the neighbor opposite each corner, or -1, shape (triangle count, 3)
    neighbors: np.ndarray = None
    # segments, shape (segment count, 2), and their markers
    segments: np.ndarray = None
    segment_markers: np.ndarray = None
    # an area limit for each triangle, for refine(), or None
    area_limits: np.ndarray = None

    def triangle_areas(self):
        """The area of each triangle."""
        corners = self.vertices[self.triangles]
        first = corners[:, 1] - corners[:, 0]
        second = corners[:, 2] - corners[:, 0]
        return 0.5 * (first[:, 0] * second[:, 1] - first[:, 1] * second[:, 0])

    def largest_angles(self):
        """The largest angle of each triangle in degrees."""
        corners = self.vertices[self.triangles]
        largest = np.zeros(len(self.triangles))
        for corner in range(3):
            to_next = corners[:, (corner + 1) % 3] - corners[:, corner]
            to_previous = corners[:, (corner + 2) % 3] - corners[:, corner]
            cross = np.abs(
                to_next[:, 0] * to_previous[:, 1] - to_next[:, 1] * to_previous[:, 0]
            )
            dot = (to_next * to_previous).sum(axis=1)
            largest = np.maximum(largest, np.degrees(np.arctan2(cross, dot)))
        return largest


def _from_handle(handle):
    """A Mesh with the arrays of a mesh handle."""
    api = _api()
    vertex_count = ctypes.c_int()
    triangle_count = ctypes.c_int()
    _check(
        api.triangle_utils_mesh_counts(
            handle, ctypes.byref(vertex_count), ctypes.byref(triangle_count)
        )
    )
    vertices, vertex_pointer = _doubles(np.zeros((vertex_count.value, 2)))
    _check(api.triangle_utils_mesh_vertices(handle, vertex_count, vertex_pointer))
    triangles, triangle_pointer = _ints(np.zeros((triangle_count.value, 3)))
    _check(api.triangle_utils_mesh_triangles(handle, triangle_count, triangle_pointer))
    attribute_count = ctypes.c_int()
    _check(
        api.triangle_utils_mesh_triangle_attribute_count(
            handle, ctypes.byref(attribute_count)
        )
    )
    attributes, attribute_pointer = _doubles(
        np.zeros((triangle_count.value, attribute_count.value))
    )
    _check(
        api.triangle_utils_mesh_triangle_attributes(
            handle, attributes.size, attribute_pointer
        )
    )
    neighbors, neighbor_pointer = _ints(np.zeros((triangle_count.value, 3)))
    _check(api.triangle_utils_mesh_neighbors(handle, triangle_count, neighbor_pointer))
    segment_count = ctypes.c_int()
    _check(api.triangle_utils_mesh_segment_count(handle, ctypes.byref(segment_count)))
    segments, segment_pointer = _ints(np.zeros((segment_count.value, 2)))
    markers, marker_pointer = _ints(np.zeros(segment_count.value))
    _check(
        api.triangle_utils_mesh_segments(
            handle, segment_count, segment_pointer, marker_pointer
        )
    )
    return Mesh(vertices, triangles, attributes, neighbors, segments, markers)


def _rows(values, columns, name, dtype):
    """An array of rows of the given number of columns; ValueError if it is not."""
    array = np.asarray(values, dtype=dtype)
    if array.ndim != 2 or array.shape[1] != columns:
        raise ValueError(f"{name} must have {columns} columns, not shape {array.shape}")
    return array


def _check_length(values, length, name, what):
    """ValueError unless an array has one value or row for each item."""
    if len(values) != length:
        raise ValueError(
            f"{name} has {len(values)} rows, but there are {length} {what}"
        )


def _to_handle(mesh):
    """A new mesh handle with the arrays of a Mesh; free it after use."""
    api = _api()
    # each array is checked before the library reads from it
    vertices, vertex_pointer = _doubles(_rows(mesh.vertices, 2, "vertices", np.float64))
    triangles, triangle_pointer = _ints(_rows(mesh.triangles, 3, "triangles", np.intc))
    count = len(triangles)
    attributes = None
    if mesh.attributes is not None and np.size(mesh.attributes) > 0:
        attributes = np.asarray(mesh.attributes, dtype=np.float64)
        if attributes.ndim == 1:
            attributes = attributes.reshape(-1, 1)
        _check_length(attributes, count, "attributes", "triangles")
    segments = markers = None
    if mesh.segments is not None and len(mesh.segments) > 0:
        segments = _rows(mesh.segments, 2, "segments", np.intc)
        if mesh.segment_markers is not None:
            markers = np.asarray(mesh.segment_markers, dtype=np.intc).reshape(-1)
            _check_length(markers, len(segments), "segment_markers", "segments")
    limits = None
    if mesh.area_limits is not None:
        limits = np.asarray(mesh.area_limits, dtype=np.float64).reshape(-1)
        _check_length(limits, count, "area_limits", "triangles")

    handle = MESH_POINTER()
    _check(
        api.triangle_utils_mesh_create(
            len(vertices), vertex_pointer, count, triangle_pointer, ctypes.byref(handle)
        )
    )
    try:
        if attributes is not None:
            attributes, pointer = _doubles(attributes)
            _check(
                api.triangle_utils_mesh_set_triangle_attributes(
                    handle, count, attributes.shape[1], pointer
                )
            )
        if segments is not None:
            segments, pointer = _ints(segments)
            marker_pointer = None
            if markers is not None:
                markers, marker_pointer = _ints(markers)
            _check(
                api.triangle_utils_mesh_set_segments(
                    handle, len(segments), pointer, marker_pointer
                )
            )
        if limits is not None:
            limits, pointer = _doubles(limits)
            _check(api.triangle_utils_mesh_set_area_limits(handle, count, pointer))
    except BaseException:
        api.triangle_utils_mesh_free(handle)
        raise
    return handle


@dataclass
class Geometry:
    """What to mesh; each method adds a feature, with points as (x, y) rows."""

    _features: list = field(default_factory=list)

    def add_polygon(self, points, spacing=0.0, marker=0):
        """Add a boundary or internal boundary the mesh follows."""
        self._features.append(
            ("geometry_add_polygon", "points", points, spacing, marker)
        )

    def add_hole_polygon(self, points, spacing=0.0, marker=0):
        """Add a polygon whose inside, and everything in it, is not meshed."""
        self._features.append(
            ("geometry_add_hole_polygon", "points", points, spacing, marker)
        )

    def add_line(self, points, spacing=0.0, marker=0):
        """Add an open line the mesh follows, such as a river."""
        self._features.append(("geometry_add_line", "points", points, spacing, marker))

    def add_hole(self, x, y):
        """Add a point inside a hole bounded by the polygons."""
        self._features.append(("geometry_add_hole", None, x, y))

    def add_region(self, x, y, attribute=0.0, area_limit=0.0):
        """Add a region by a point inside it, up to the segments around it."""
        self._features.append(
            ("geometry_add_region", None, x, y, attribute, area_limit)
        )

    def add_points(self, points):
        """Add points the mesh includes as vertices."""
        self._features.append(("geometry_add_points", "points", points))

    def add_ring(
        self, x, y, radius, point_count, attribute=0.0, area_limit=0.0, marker=0
    ):
        """Add a ring of finer triangles around a point, such as a well."""
        self._features.append(
            (
                "geometry_add_ring",
                None,
                x,
                y,
                radius,
                point_count,
                attribute,
                area_limit,
                marker,
            )
        )

    def _handle(self):
        """A new geometry handle with the features; free it after use."""
        api = _api()
        handle = GEOMETRY_POINTER()
        _check(api.triangle_utils_geometry_create(ctypes.byref(handle)))
        try:
            for name, kind, *arguments in self._features:
                function = getattr(api, f"triangle_utils_{name}")
                if kind is None:
                    _check(function(handle, *arguments))
                    continue
                points, pointer = _doubles(_rows(arguments[0], 2, "points", np.float64))
                _check(function(handle, len(points), pointer, *arguments[1:]))
        except BaseException:
            api.triangle_utils_geometry_free(handle)
            raise
        return handle


def generate(
    geometry,
    maximum_area=0.0,
    minimum_angle=0.0,
    merge_distance=0.0,
    conforming_delaunay=False,
):
    """Mesh the geometry; a limit of 0 is no limit."""
    api = _api()
    geometry_handle = geometry._handle()
    try:
        mesh_handle = MESH_POINTER()
        _check(
            api.triangle_utils_mesh_generate(
                geometry_handle,
                maximum_area,
                minimum_angle,
                merge_distance,
                int(conforming_delaunay),
                ctypes.byref(mesh_handle),
            )
        )
    finally:
        api.triangle_utils_geometry_free(geometry_handle)
    try:
        return _from_handle(mesh_handle)
    finally:
        api.triangle_utils_mesh_free(mesh_handle)


def refine(
    mesh,
    maximum_area=0.0,
    minimum_angle=0.0,
    maximum_angle=0.0,
    maximum_angle_splits=100000,
    conforming_delaunay=False,
):
    """Refine a mesh by its area limits and these limits; 0 is no limit."""
    api = _api()
    mesh_handle = _to_handle(mesh)
    try:
        refined = MESH_POINTER()
        _check(
            api.triangle_utils_mesh_refine(
                mesh_handle,
                maximum_area,
                minimum_angle,
                maximum_angle,
                maximum_angle_splits,
                int(conforming_delaunay),
                ctypes.byref(refined),
            )
        )
    finally:
        api.triangle_utils_mesh_free(mesh_handle)
    try:
        return _from_handle(refined)
    finally:
        api.triangle_utils_mesh_free(refined)


def count_angles_over(mesh, limit):
    """The number of a mesh's triangles with an angle over limit degrees."""
    api = _api()
    handle = _to_handle(mesh)
    try:
        count = ctypes.c_int()
        _check(
            api.triangle_utils_mesh_count_angles_over(
                handle, limit, ctypes.byref(count)
            )
        )
        return count.value
    finally:
        api.triangle_utils_mesh_free(handle)


def version():
    """The versions of triangle-utils, Triangle, and its MODFLOW-ORG release."""
    api = _api()
    return (
        api.triangle_utils_version().decode(),
        api.triangle_utils_triangle_version().decode(),
        api.triangle_utils_triangle_release().decode(),
    )
