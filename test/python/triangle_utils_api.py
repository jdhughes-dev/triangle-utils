"""
The C API's functions, status codes, and handles for ctypes.

load() opens the library named by the TRIANGLE_UTILS_LIBRARY environment
variable, which meson test sets, and declares each function's types.
"""

import ctypes
import os

OK = 0
ERROR = 1
INVALID_ARGUMENT = 2


class Mesh(ctypes.Structure):
    """The opaque mesh."""


class Geometry(ctypes.Structure):
    """The opaque geometry."""


MESH_POINTER = ctypes.POINTER(Mesh)
GEOMETRY_POINTER = ctypes.POINTER(Geometry)

INT = ctypes.c_int
DOUBLE = ctypes.c_double
INT_ARRAY = ctypes.POINTER(ctypes.c_int)
DOUBLE_ARRAY = ctypes.POINTER(ctypes.c_double)

# the result and argument types of each function, by name without its
# triangle_utils_ prefix
SIGNATURES = {
    "last_error": (ctypes.c_char_p, []),
    "version": (ctypes.c_char_p, []),
    "version_numbers": (None, [INT_ARRAY] * 3),
    "triangle_version": (ctypes.c_char_p, []),
    "triangle_release": (ctypes.c_char_p, []),
    "license": (ctypes.c_char_p, []),
    "triangle_copyright": (ctypes.c_char_p, []),
    "mesh_create": (
        INT,
        [INT, DOUBLE_ARRAY, INT, INT_ARRAY, ctypes.POINTER(MESH_POINTER)],
    ),
    "mesh_set_neighbors": (INT, [MESH_POINTER, INT, INT_ARRAY]),
    "mesh_free": (None, [MESH_POINTER]),
    "mesh_counts": (INT, [MESH_POINTER, INT_ARRAY, INT_ARRAY]),
    "mesh_vertices": (INT, [MESH_POINTER, INT, DOUBLE_ARRAY]),
    "mesh_triangles": (INT, [MESH_POINTER, INT, INT_ARRAY]),
    "mesh_triangle_attribute_count": (INT, [MESH_POINTER, INT_ARRAY]),
    "mesh_triangle_attributes": (INT, [MESH_POINTER, INT, DOUBLE_ARRAY]),
    "mesh_neighbors": (INT, [MESH_POINTER, INT, INT_ARRAY]),
    "mesh_segment_count": (INT, [MESH_POINTER, INT_ARRAY]),
    "mesh_segments": (INT, [MESH_POINTER, INT, INT_ARRAY, INT_ARRAY]),
    "geometry_create": (INT, [ctypes.POINTER(GEOMETRY_POINTER)]),
    "geometry_free": (None, [GEOMETRY_POINTER]),
    "geometry_add_polygon": (INT, [GEOMETRY_POINTER, INT, DOUBLE_ARRAY, DOUBLE, INT]),
    "geometry_add_hole_polygon": (
        INT,
        [GEOMETRY_POINTER, INT, DOUBLE_ARRAY, DOUBLE, INT],
    ),
    "geometry_add_line": (INT, [GEOMETRY_POINTER, INT, DOUBLE_ARRAY, DOUBLE, INT]),
    "geometry_add_hole": (INT, [GEOMETRY_POINTER, DOUBLE, DOUBLE]),
    "geometry_add_region": (INT, [GEOMETRY_POINTER, DOUBLE, DOUBLE, DOUBLE, DOUBLE]),
    "geometry_add_points": (INT, [GEOMETRY_POINTER, INT, DOUBLE_ARRAY]),
    "geometry_add_ring": (
        INT,
        [GEOMETRY_POINTER, DOUBLE, DOUBLE, DOUBLE, INT, DOUBLE, DOUBLE, INT],
    ),
    "mesh_generate": (
        INT,
        [GEOMETRY_POINTER, DOUBLE, DOUBLE, DOUBLE, INT, ctypes.POINTER(MESH_POINTER)],
    ),
}


def load():
    """Open the library and declare its functions."""
    library = ctypes.CDLL(os.environ["TRIANGLE_UTILS_LIBRARY"])
    for name, (result, arguments) in SIGNATURES.items():
        function = getattr(library, f"triangle_utils_{name}")
        function.restype = result
        function.argtypes = arguments
    return library


def doubles(values):
    """A ctypes array of doubles."""
    return (ctypes.c_double * len(values))(*values)
