"""
Python access to triangle-utils through its C API.

Meshes and grids go in and out as NumPy arrays; geometry is added to a
Geometry and meshed with generate(), and a grid's vertices and cells are
given in the form FloPy's DISV package takes.
"""

import ctypes
import itertools
from dataclasses import dataclass, field

import numpy as np

from ._features import KINDS, Feature, read_features
from ._library import (
    CELL_LENGTHS_POINTER,
    CONNECTIONS_POINTER,
    GEOMETRY_POINTER,
    GRID_POINTER,
    LAYERS_POINTER,
    MESH_POINTER,
    OK,
    load,
)

__all__ = [
    "CellLengths",
    "Connections",
    "Disu",
    "Error",
    "Feature",
    "Geometry",
    "Grid",
    "Layers",
    "Mesh",
    "cells_along_line",
    "cells_at_points",
    "cells_on_segments",
    "connect_cells",
    "count_angles_over",
    "generate",
    "improve",
    "layer_grid",
    "read_features",
    "refine",
    "triangle_grid",
    "values_at_centers",
    "version",
    "voronoi",
]

_library = None

# the range of the library's int
INT_MIN = int(np.iinfo(np.intc).min)
INT_MAX = int(np.iinfo(np.intc).max)


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


@dataclass(eq=False)
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
    # vertices improve() must not move, besides those on segments and the
    # boundary, such as wells; generate() sets its points and ring centers
    fixed_vertices: np.ndarray = None

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
    fixed_count = ctypes.c_int()
    _check(
        api.triangle_utils_mesh_fixed_vertex_count(handle, ctypes.byref(fixed_count))
    )
    fixed, fixed_pointer = _ints(np.zeros(fixed_count.value))
    _check(api.triangle_utils_mesh_fixed_vertices(handle, fixed_count, fixed_pointer))
    return Mesh(
        vertices,
        triangles,
        attributes,
        neighbors,
        segments,
        markers,
        fixed_vertices=fixed,
    )


def _integers(values, name):
    """An int array; ValueError if a value is not a whole number."""
    array = np.asarray(values)
    if array.size and not np.issubdtype(array.dtype, np.integer):
        if not np.issubdtype(array.dtype, np.number) or np.any(
            array != np.round(array)
        ):
            raise ValueError(f"{name} must be whole numbers")
    if array.size and (array.min() < INT_MIN or array.max() > INT_MAX):
        raise ValueError(f"{name} must be from {INT_MIN} to {INT_MAX}")
    return array.astype(np.intc)


def _int(value, name):
    """A whole number for an int argument; ValueError if it does not fit."""
    if int(value) != value or not INT_MIN <= value <= INT_MAX:
        raise ValueError(f"{name} must be a whole number from {INT_MIN} to {INT_MAX}")
    return int(value)


def _rows(values, columns, name, dtype):
    """An array of rows of the given number of columns; ValueError if it is not."""
    array = (
        _integers(values, name) if dtype is np.intc else np.asarray(values, dtype=dtype)
    )
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
        if attributes.ndim != 2:
            raise ValueError(
                f"attributes must have 1 or 2 dimensions, not {attributes.ndim}"
            )
        _check_length(attributes, count, "attributes", "triangles")
    segments = markers = None
    if mesh.segments is not None and len(mesh.segments) > 0:
        segments = _rows(mesh.segments, 2, "segments", np.intc)
        if mesh.segment_markers is not None:
            markers = _integers(mesh.segment_markers, "segment_markers").reshape(-1)
            _check_length(markers, len(segments), "segment_markers", "segments")
    elif mesh.segment_markers is not None and len(mesh.segment_markers) > 0:
        raise ValueError("segment_markers are given without segments")
    fixed = None
    if mesh.fixed_vertices is not None:
        fixed = _integers(mesh.fixed_vertices, "fixed_vertices").reshape(-1)
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
        if fixed is not None:
            fixed, pointer = _ints(fixed)
            _check(
                api.triangle_utils_mesh_set_fixed_vertices(handle, len(fixed), pointer)
            )
    except BaseException:
        api.triangle_utils_mesh_free(handle)
        raise
    return handle


@dataclass
class Geometry:
    """What to mesh; each method adds a feature, with points as (x, y) rows."""

    _features: list = field(default_factory=list, init=False, repr=False)

    # each feature's points and values are copied and checked when it is
    # added, so changing the caller's arrays later changes nothing

    def _add_chain(self, name, points, spacing, marker):
        points = np.array(_rows(points, 2, "points", np.float64))
        self._features.append(
            (name, "points", points, float(spacing), _int(marker, "marker"))
        )

    def add_polygon(self, points, spacing=0.0, marker=0):
        """Add a boundary or internal boundary the mesh follows."""
        self._add_chain("geometry_add_polygon", points, spacing, marker)

    def add_hole_polygon(self, points, spacing=0.0, marker=0):
        """Add a polygon whose inside, and everything in it, is not meshed."""
        self._add_chain("geometry_add_hole_polygon", points, spacing, marker)

    def add_line(self, points, spacing=0.0, marker=0):
        """Add an open line the mesh follows, such as a river."""
        self._add_chain("geometry_add_line", points, spacing, marker)

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
        points = np.array(_rows(points, 2, "points", np.float64))
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
                _int(point_count, "point_count"),
                attribute,
                area_limit,
                _int(marker, "marker"),
            )
        )

    def add_features(self, features, points="vertices"):
        """Add features, such as those of read_features(), with point features
        as "vertices", "regions", or "holes".

        Polygons, hole polygons, and lines get their spacing and marker
        whatever points is, and regions their attribute and area limit."""
        if points not in ("vertices", "regions", "holes"):
            raise ValueError(
                f'points must be "vertices", "regions", or "holes", not {points!r}'
            )
        # added to a new geometry first, so an error adds nothing
        staged = Geometry()
        vertices = []
        for feature in features:
            if feature.kind not in KINDS:
                raise ValueError(f"{feature.kind!r} is not a kind of feature")
            if feature.kind != "point":
                add = getattr(staged, f"add_{feature.kind}")
                add(
                    feature.points,
                    spacing=float(feature.spacing),
                    marker=feature.marker,
                )
                continue
            for x, y in _rows(feature.points, 2, "points", np.float64):
                if points == "vertices":
                    vertices.append((x, y))
                elif points == "regions":
                    staged.add_region(
                        x, y, float(feature.attribute), float(feature.area_limit)
                    )
                else:
                    staged.add_hole(x, y)
        # in one call, after the other features
        if vertices:
            staged.add_points(vertices)
        self._features.extend(staged._features)

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
                points, pointer = _doubles(arguments[0])
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
    maximum_angle_splits = _int(maximum_angle_splits, "maximum_angle_splits")
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


def improve(
    mesh,
    smoothing="laplacian",
    maximum_angle=0.0,
    minimum_angle=0.0,
    iterations=100,
    tolerance=1e-4,
):
    """Improve a mesh by moving the vertices that are not fixed and flipping
    edges; smoothing is "laplacian" or "centroidal", and a minimum angle of 0
    keeps the mesh's smallest angle from decreasing."""
    choices = {"laplacian": 0, "centroidal": 1}
    if smoothing not in choices:
        raise ValueError(
            f'smoothing must be "laplacian" or "centroidal", not {smoothing!r}'
        )
    iterations = _int(iterations, "iterations")
    api = _api()
    mesh_handle = _to_handle(mesh)
    try:
        improved = MESH_POINTER()
        _check(
            api.triangle_utils_mesh_improve(
                mesh_handle,
                choices[smoothing],
                maximum_angle,
                minimum_angle,
                iterations,
                tolerance,
                ctypes.byref(improved),
            )
        )
    finally:
        api.triangle_utils_mesh_free(mesh_handle)
    try:
        return _from_handle(improved)
    finally:
        api.triangle_utils_mesh_free(improved)


@dataclass(eq=False)
class Grid:
    """A grid of polygonal cells for MODFLOW 6 DISV; indices are zero based."""

    # x and y of each vertex, each once, shape (vertex count, 2)
    vertices: np.ndarray
    # the vertices of each cell, clockwise, one array per cell
    cells: list
    # x and y of each cell's center, shape (cell count, 2)
    centers: np.ndarray
    # the mesh vertex (Voronoi grid) or triangle (triangle grid) of each cell
    sources: np.ndarray
    # attributes of each cell, shape (cell count, attribute count)
    attributes: np.ndarray

    def disv_vertices(self):
        """The vertices as flopy's ModflowGwfdisv takes them."""
        return [[vertex, x, y] for vertex, (x, y) in enumerate(self.vertices.tolist())]

    def disv_cell2d(self):
        """The cells as flopy's ModflowGwfdisv takes them."""
        return [
            [cell, x, y, len(corners), *corners.tolist()]
            for cell, ((x, y), corners) in enumerate(
                zip(self.centers.tolist(), self.cells, strict=True)
            )
        ]


def _grid_from_handle(handle):
    """A Grid with the arrays of a grid handle."""
    api = _api()
    vertex_count = ctypes.c_int()
    cell_count = ctypes.c_int()
    corner_count = ctypes.c_int()
    _check(
        api.triangle_utils_grid_counts(
            handle,
            ctypes.byref(vertex_count),
            ctypes.byref(cell_count),
            ctypes.byref(corner_count),
        )
    )
    vertices, vertex_pointer = _doubles(np.zeros((vertex_count.value, 2)))
    _check(api.triangle_utils_grid_vertices(handle, vertex_count, vertex_pointer))
    offsets, offset_pointer = _ints(np.zeros(cell_count.value + 1))
    corners, corner_pointer = _ints(np.zeros(corner_count.value))
    _check(
        api.triangle_utils_grid_cells(
            handle, cell_count, offset_pointer, corner_count, corner_pointer
        )
    )
    centers, center_pointer = _doubles(np.zeros((cell_count.value, 2)))
    _check(api.triangle_utils_grid_centers(handle, cell_count, center_pointer))
    sources, source_pointer = _ints(np.zeros(cell_count.value))
    _check(api.triangle_utils_grid_sources(handle, cell_count, source_pointer))
    attribute_count = ctypes.c_int()
    _check(
        api.triangle_utils_grid_attribute_count(handle, ctypes.byref(attribute_count))
    )
    attributes, attribute_pointer = _doubles(
        np.zeros((cell_count.value, attribute_count.value))
    )
    _check(
        api.triangle_utils_grid_attributes(handle, attributes.size, attribute_pointer)
    )
    cells = [corners[start:end] for start, end in itertools.pairwise(offsets)]
    return Grid(vertices, cells, centers, sources, attributes)


def _grid(mesh, build):
    """The grid a C API function builds from a mesh."""
    api = _api()
    mesh_handle = _to_handle(mesh)
    try:
        grid = GRID_POINTER()
        _check(build(mesh_handle, ctypes.byref(grid)))
    finally:
        api.triangle_utils_mesh_free(mesh_handle)
    try:
        return _grid_from_handle(grid)
    finally:
        api.triangle_utils_grid_free(grid)


def voronoi(mesh):
    """The Voronoi grid of a conforming Delaunay mesh: a cell around each
    vertex, centered on it."""
    return _grid(mesh, _api().triangle_utils_mesh_voronoi)


def triangle_grid(mesh, center="centroid"):
    """A grid whose cells are the mesh's triangles; center is "centroid" or
    "circumcenter"."""
    choices = {"centroid": 0, "circumcenter": 1}
    if center not in choices:
        raise ValueError(f'center must be "centroid" or "circumcenter", not {center!r}')
    return _grid(
        mesh,
        lambda handle, grid: _api().triangle_utils_mesh_triangle_grid(
            handle, choices[center], grid
        ),
    )


def _grid_handle(grid):
    """A grid handle with the grid's vertices, cells, and centers; free it
    with triangle_utils_grid_free."""
    vertices = _rows(grid.vertices, 2, "vertices", np.float64)
    centers = _rows(grid.centers, 2, "centers", np.float64)
    cells = [_integers(cell, "cells") for cell in grid.cells]
    _check_length(centers, len(cells), "centers", "cells")
    offsets = np.concatenate([[0], np.cumsum([len(cell) for cell in cells])])
    corners = np.concatenate(cells) if cells else np.zeros(0)
    # the arrays are kept until the call that reads their pointers returns
    vertex_array, vertex_pointer = _doubles(vertices)
    _offset_array, offset_pointer = _ints(offsets)
    corner_array, corner_pointer = _ints(corners)
    _center_array, center_pointer = _doubles(centers)
    handle = GRID_POINTER()
    _check(
        _api().triangle_utils_grid_create(
            len(vertex_array),
            vertex_pointer,
            len(cells),
            offset_pointer,
            len(corner_array),
            corner_pointer,
            center_pointer,
            ctypes.byref(handle),
        )
    )
    return handle


@dataclass(eq=False)
class CellLengths:
    """Cells a line or marked segments pass through; indices are zero based."""

    # each cell
    cells: np.ndarray
    # where its part starts along the line or segments
    starts: np.ndarray
    # the length of line or segment in it
    lengths: np.ndarray


def _cell_lengths(build):
    """The cell lengths a C API function returns."""
    api = _api()
    handle = CELL_LENGTHS_POINTER()
    _check(build(ctypes.byref(handle)))
    try:
        count = ctypes.c_int()
        _check(api.triangle_utils_cell_lengths_count(handle, ctypes.byref(count)))
        cells, cell_pointer = _ints(np.zeros(count.value))
        starts, start_pointer = _doubles(np.zeros(count.value))
        lengths, length_pointer = _doubles(np.zeros(count.value))
        _check(
            api.triangle_utils_cell_lengths(
                handle, count, cell_pointer, start_pointer, length_pointer
            )
        )
        return CellLengths(cells, starts, lengths)
    finally:
        api.triangle_utils_cell_lengths_free(handle)


def cells_along_line(grid, line):
    """The cells along a line of (x, y) points, in order, with the length of
    line in each; a part along a face two cells share is in the cell on the
    line's left."""
    points, point_pointer = _doubles(_rows(line, 2, "line", np.float64))
    api = _api()
    handle = _grid_handle(grid)
    try:
        return _cell_lengths(
            lambda result: api.triangle_utils_grid_line_cells(
                handle, len(points), point_pointer, result
            )
        )
    finally:
        api.triangle_utils_grid_free(handle)


def cells_on_segments(grid, mesh, marker):
    """The cells on the mesh's segments with a marker, such as a boundary,
    each once, with the length of segment in each."""
    marker = _int(marker, "marker")
    api = _api()
    grid_handle = _grid_handle(grid)
    try:
        mesh_handle = _to_handle(mesh)
        try:
            return _cell_lengths(
                lambda result: api.triangle_utils_grid_segment_cells(
                    grid_handle, mesh_handle, marker, result
                )
            )
        finally:
            api.triangle_utils_mesh_free(mesh_handle)
    finally:
        api.triangle_utils_grid_free(grid_handle)


def cells_at_points(grid, points):
    """The cell containing each (x, y) point, such as a well, or -1 outside the
    grid; a point on a face or corner is in the lowest-numbered cell there."""
    xy, point_pointer = _doubles(_rows(points, 2, "points", np.float64))
    cells, cell_pointer = _ints(np.zeros(len(xy)))
    api = _api()
    handle = _grid_handle(grid)
    try:
        _check(
            api.triangle_utils_grid_point_cells(
                handle, len(xy), point_pointer, cell_pointer
            )
        )
    finally:
        api.triangle_utils_grid_free(handle)
    return cells


@dataclass(eq=False)
class Connections:
    """The faces the cells of a grid share, for MODFLOW DISU; indices are
    zero based. Those of cell i are offsets[i] up to offsets[i + 1], in
    increasing order of the other cell, and each is listed from both of its
    cells."""

    # cell count + 1 values
    offsets: np.ndarray
    # the other cell (DISU JA, without the cell itself)
    cells: np.ndarray
    # the distance from the cell's center to the face, normal to it (CL12)
    distances: np.ndarray
    # the width of the face (HWVA)
    widths: np.ndarray
    # the direction of the face's normal out of the cell, in degrees
    # counterclockwise from the x axis, 0 up to 360 (ANGLDEGX)
    angles: np.ndarray


def connect_cells(grid):
    """The connections of a grid's cells to the cells they share a face with,
    at two common corners;
    an error if a center is on a face or beyond it, as an obtuse triangle's
    circumcenter is."""
    api = _api()
    grid_handle = _grid_handle(grid)
    try:
        handle = CONNECTIONS_POINTER()
        _check(api.triangle_utils_grid_connections(grid_handle, ctypes.byref(handle)))
    finally:
        api.triangle_utils_grid_free(grid_handle)
    try:
        cell_count = ctypes.c_int()
        count = ctypes.c_int()
        _check(
            api.triangle_utils_connections_count(
                handle, ctypes.byref(cell_count), ctypes.byref(count)
            )
        )
        offsets, offset_pointer = _ints(np.zeros(cell_count.value + 1))
        cells, cell_pointer = _ints(np.zeros(count.value))
        distances, distance_pointer = _doubles(np.zeros(count.value))
        widths, width_pointer = _doubles(np.zeros(count.value))
        angles, angle_pointer = _doubles(np.zeros(count.value))
        _check(
            api.triangle_utils_connections(
                handle,
                cell_count,
                offset_pointer,
                count,
                cell_pointer,
                distance_pointer,
                width_pointer,
                angle_pointer,
            )
        )
        return Connections(offsets, cells, distances, widths, angles)
    finally:
        api.triangle_utils_connections_free(handle)


def values_at_centers(grid, mesh, vertex_values):
    """The value at each cell's center, such as an elevation, interpolated
    linearly from a value at each of the mesh's vertices; a center on a
    vertex, as in a Voronoi grid, gets the vertex's value."""
    values, value_pointer = _doubles(np.asarray(vertex_values, dtype=np.float64))
    if values.ndim != 1:
        raise ValueError(
            f"vertex_values must be one value per vertex, not {values.shape}"
        )
    result, result_pointer = _doubles(np.zeros(len(grid.cells)))
    api = _api()
    grid_handle = _grid_handle(grid)
    try:
        mesh_handle = _to_handle(mesh)
        try:
            _check(
                api.triangle_utils_grid_vertex_values(
                    grid_handle,
                    mesh_handle,
                    len(values),
                    value_pointer,
                    len(result),
                    result_pointer,
                )
            )
        finally:
            api.triangle_utils_mesh_free(mesh_handle)
    finally:
        api.triangle_utils_grid_free(grid_handle)
    return result


@dataclass(eq=False)
class Disu:
    """The DISU data of layers, a node for each active cell numbered layer by
    layer; indices are zero based. The connections of node n are iac[n]
    values of ja and the arrays beside it, the first to n itself."""

    # the layer and cell of each node
    layers: np.ndarray
    cells: np.ndarray
    top: np.ndarray
    bot: np.ndarray
    area: np.ndarray
    iac: np.ndarray
    ja: np.ndarray
    # 0 vertical, 1 horizontal
    ihc: np.ndarray
    # 0 for a node's connection to itself
    cl12: np.ndarray
    hwva: np.ndarray
    angldegx: np.ndarray


@dataclass(eq=False)
class Layers:
    """Layers of a grid, each with the grid's cells, with their DISV and DISU
    data; indices are zero based."""

    grid: Grid
    # the top of each cell, shape (cell count,)
    top: np.ndarray
    # the bottom of each cell in each layer, shape (layer count, cell count),
    # with a bottom the minimum thickness made pass-through moved up to its
    # top if it was above it
    botm: np.ndarray
    # > 0 active, 0 not in the model, < 0 vertical pass-through, with the
    # pass-through cells the minimum thickness made
    idomain: np.ndarray
    # the DISU node of each cell in each layer, or -1
    nodes: np.ndarray
    disu: Disu

    def disv_arguments(self):
        """The arguments of flopy's ModflowGwfdisv for the layers."""
        layer_count, cell_count = self.botm.shape
        return {
            "nlay": layer_count,
            "ncpl": cell_count,
            "nvert": len(self.grid.vertices),
            "top": self.top,
            "botm": self.botm,
            "idomain": self.idomain,
            "vertices": self.grid.disv_vertices(),
            "cell2d": self.grid.disv_cell2d(),
        }

    def disu_arguments(self):
        """The arguments of flopy's ModflowGwfdisu for the layers, with each
        node's cell as its vertices."""
        d = self.disu
        centers = self.grid.centers[d.cells].tolist()
        corners = [self.grid.cells[cell].tolist() for cell in d.cells]
        return {
            "nodes": len(d.cells),
            "nja": len(d.ja),
            "nvert": len(self.grid.vertices),
            "top": d.top,
            "bot": d.bot,
            "area": d.area,
            "iac": d.iac,
            "ja": d.ja,
            "ihc": d.ihc,
            "cl12": d.cl12,
            "hwva": d.hwva,
            "angldegx": d.angldegx,
            "vertices": self.grid.disv_vertices(),
            "cell2d": [
                [node, x, y, len(cell), *cell]
                for node, ((x, y), cell) in enumerate(
                    zip(centers, corners, strict=True)
                )
            ],
        }


def _layer_values(values, layer_count, cell_count, name, dtype):
    """Values for each cell in each layer from a number, or for each layer a
    number or a value for each cell."""
    try:
        if isinstance(values, (list, tuple)):
            # a number or a value for each cell, for each layer
            array = np.array(
                [
                    np.broadcast_to(np.asarray(v, dtype=dtype), cell_count)
                    for v in values
                ]
            )
        else:
            array = np.asarray(values, dtype=dtype)
            if array.ndim == 1:
                array = array[:, np.newaxis]
        return np.ascontiguousarray(np.broadcast_to(array, (layer_count, cell_count)))
    except ValueError:
        if isinstance(values, (list, tuple)):
            shape = f"the shapes {[np.shape(v) for v in values]}"
        else:
            shape = f"shape {np.shape(values)}"
        raise ValueError(
            f"{name} must be a number, a value for each of {layer_count} layers, "
            f"or shape ({layer_count}, {cell_count}), not {shape}"
        ) from None


def layer_grid(grid, top, botm, idomain=None, minimum_thickness=0.0):
    """Layers of a grid, from the top of its cells and the bottom of each
    layer, as DISV takes them: top is a number or a value for each cell, and
    botm has, for each layer, a number or a value for each cell, from which
    the layer count is taken, so a one-dimensional array is a number for each
    layer and one layer's values for each cell are [values]; idomain, if
    given, is a number or is given for each layer as botm is.

    A cell's top is the bottom of the cell above it, as in MODFLOW. An active
    cell thinner than minimum_thickness, if it is positive, becomes a
    pass-through cell (idomain -1), and a bottom above its top moves up to
    it; otherwise an active cell that is not thicker than 0 raises Error,
    naming its cell and layer, as does a cell that is not active, above the
    last layer, whose bottom is above its top, and a grid whose cells
    connect_cells() cannot connect, such as a triangle grid centered on the
    circumcenters of obtuse triangles."""
    cell_count = len(grid.cells)
    scalar = not isinstance(botm, (list, tuple)) and np.ndim(botm) == 0
    layer_count = 1 if scalar else len(botm)
    if layer_count == 0:
        raise ValueError("botm must have a bottom for at least one layer")
    bottoms = _layer_values(botm, layer_count, cell_count, "botm", np.float64)
    tops = np.asarray(top, dtype=np.float64)
    if tops.ndim == 0:
        tops = np.full(cell_count, tops)
    elif tops.shape != (cell_count,):
        raise ValueError(
            f"top must be a number or one of {cell_count} values, not shape "
            f"{tops.shape}"
        )
    domain_pointer = None
    if idomain is not None:
        domain = _integers(
            _layer_values(idomain, layer_count, cell_count, "idomain", np.float64),
            "idomain",
        )
        domain, domain_pointer = _ints(domain)
    tops, top_pointer = _doubles(tops)
    bottoms, bottom_pointer = _doubles(bottoms)
    api = _api()
    grid_handle = _grid_handle(grid)
    try:
        handle = LAYERS_POINTER()
        _check(
            api.triangle_utils_grid_layers(
                grid_handle,
                layer_count,
                cell_count,
                top_pointer,
                bottom_pointer,
                domain_pointer,
                float(minimum_thickness),
                ctypes.byref(handle),
            )
        )
    finally:
        api.triangle_utils_grid_free(grid_handle)
    try:
        result, result_pointer = _ints(np.zeros((layer_count, cell_count)))
        _check(api.triangle_utils_layers_idomain(handle, result.size, result_pointer))
        bottoms, bottom_pointer = _doubles(np.zeros((layer_count, cell_count)))
        _check(api.triangle_utils_layers_bottoms(handle, bottoms.size, bottom_pointer))
        node_count = ctypes.c_int()
        count = ctypes.c_int()
        _check(
            api.triangle_utils_layers_disu_count(
                handle, ctypes.byref(node_count), ctypes.byref(count)
            )
        )
        n = node_count.value
        cells, cell_pointer = _ints(np.zeros(n))
        node_top, node_top_pointer = _doubles(np.zeros(n))
        node_bot, node_bot_pointer = _doubles(np.zeros(n))
        area, area_pointer = _doubles(np.zeros(n))
        _check(
            api.triangle_utils_layers_disu_nodes(
                handle,
                n,
                cell_pointer,
                node_top_pointer,
                node_bot_pointer,
                area_pointer,
            )
        )
        offsets, offset_pointer = _ints(np.zeros(n + 1))
        ja, ja_pointer = _ints(np.zeros(count.value))
        ihc, ihc_pointer = _ints(np.zeros(count.value))
        cl12, cl12_pointer = _doubles(np.zeros(count.value))
        hwva, hwva_pointer = _doubles(np.zeros(count.value))
        angles, angle_pointer = _doubles(np.zeros(count.value))
        _check(
            api.triangle_utils_layers_disu_connections(
                handle,
                n,
                offset_pointer,
                count,
                ja_pointer,
                ihc_pointer,
                cl12_pointer,
                hwva_pointer,
                angle_pointer,
            )
        )
    finally:
        api.triangle_utils_layers_free(handle)
    nodes = np.full(layer_count * cell_count, -1, dtype=np.intc)
    nodes[cells] = np.arange(n, dtype=np.intc)
    disu = Disu(
        cells // cell_count if cell_count else cells,
        cells % cell_count if cell_count else cells,
        node_top,
        node_bot,
        area,
        np.diff(offsets).astype(np.intc),
        ja,
        ihc,
        cl12,
        hwva,
        angles,
    )
    return Layers(
        grid,
        tops,
        bottoms,
        result,
        nodes.reshape(layer_count, cell_count),
        disu,
    )
