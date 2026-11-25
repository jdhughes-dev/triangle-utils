"""
The C API's functions, status codes, and handles for ctypes.

load() opens the library and declares each function's types: the library
named by the TRIANGLE_UTILS_LIBRARY environment variable, which meson test
sets, or else the one `pixi run build` installs in the repository.
"""

import ctypes
import os
from pathlib import Path

OK = 0
ERROR = 1
INVALID_ARGUMENT = 2


class Mesh(ctypes.Structure):
    """The opaque mesh."""


class Geometry(ctypes.Structure):
    """The opaque geometry."""


class Grid(ctypes.Structure):
    """The opaque grid."""


class CellLengths(ctypes.Structure):
    """The opaque cell lengths."""


MESH_POINTER = ctypes.POINTER(Mesh)
GEOMETRY_POINTER = ctypes.POINTER(Geometry)
GRID_POINTER = ctypes.POINTER(Grid)
CELL_LENGTHS_POINTER = ctypes.POINTER(CellLengths)

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
    "mesh_set_segments": (INT, [MESH_POINTER, INT, INT_ARRAY, INT_ARRAY]),
    "mesh_set_triangle_attributes": (INT, [MESH_POINTER, INT, INT, DOUBLE_ARRAY]),
    "mesh_set_area_limits": (INT, [MESH_POINTER, INT, DOUBLE_ARRAY]),
    "mesh_refine": (
        INT,
        [MESH_POINTER, DOUBLE, DOUBLE, DOUBLE, INT, INT, ctypes.POINTER(MESH_POINTER)],
    ),
    "mesh_count_angles_over": (INT, [MESH_POINTER, DOUBLE, INT_ARRAY]),
    "mesh_set_fixed_vertices": (INT, [MESH_POINTER, INT, INT_ARRAY]),
    "mesh_fixed_vertex_count": (INT, [MESH_POINTER, INT_ARRAY]),
    "mesh_fixed_vertices": (INT, [MESH_POINTER, INT, INT_ARRAY]),
    "mesh_improve": (
        INT,
        [MESH_POINTER, INT, DOUBLE, DOUBLE, INT, DOUBLE, ctypes.POINTER(MESH_POINTER)],
    ),
    "mesh_voronoi": (INT, [MESH_POINTER, ctypes.POINTER(GRID_POINTER)]),
    "mesh_triangle_grid": (INT, [MESH_POINTER, INT, ctypes.POINTER(GRID_POINTER)]),
    "grid_free": (None, [GRID_POINTER]),
    "grid_counts": (INT, [GRID_POINTER, INT_ARRAY, INT_ARRAY, INT_ARRAY]),
    "grid_vertices": (INT, [GRID_POINTER, INT, DOUBLE_ARRAY]),
    "grid_cells": (INT, [GRID_POINTER, INT, INT_ARRAY, INT, INT_ARRAY]),
    "grid_centers": (INT, [GRID_POINTER, INT, DOUBLE_ARRAY]),
    "grid_sources": (INT, [GRID_POINTER, INT, INT_ARRAY]),
    "grid_attribute_count": (INT, [GRID_POINTER, INT_ARRAY]),
    "grid_attributes": (INT, [GRID_POINTER, INT, DOUBLE_ARRAY]),
    "grid_create": (
        INT,
        [
            INT,
            DOUBLE_ARRAY,
            INT,
            INT_ARRAY,
            INT,
            INT_ARRAY,
            DOUBLE_ARRAY,
            ctypes.POINTER(GRID_POINTER),
        ],
    ),
    "grid_line_cells": (
        INT,
        [GRID_POINTER, INT, DOUBLE_ARRAY, ctypes.POINTER(CELL_LENGTHS_POINTER)],
    ),
    "grid_segment_cells": (
        INT,
        [GRID_POINTER, MESH_POINTER, INT, ctypes.POINTER(CELL_LENGTHS_POINTER)],
    ),
    "cell_lengths_free": (None, [CELL_LENGTHS_POINTER]),
    "cell_lengths_count": (INT, [CELL_LENGTHS_POINTER, INT_ARRAY]),
    "cell_lengths": (
        INT,
        [CELL_LENGTHS_POINTER, INT, INT_ARRAY, DOUBLE_ARRAY, DOUBLE_ARRAY],
    ),
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


# the repository, where `pixi run build` installs the library
REPOSITORY = Path(__file__).resolve().parents[2]


def library_path():
    """The library to load."""
    if os.environ.get("TRIANGLE_UTILS_LIBRARY"):
        return Path(os.environ["TRIANGLE_UTILS_LIBRARY"])
    # the shared library, or its DLL, which Windows installs in bin
    for pattern in (
        "lib/libtriangle_utils.so",
        "lib/libtriangle_utils.dylib",
        "bin/*triangle_utils*.dll",
    ):
        found = sorted(REPOSITORY.glob(pattern))
        if found:
            return found[0]
    raise FileNotFoundError(
        "the triangle-utils library was not found; run `pixi run build`, or set "
        "TRIANGLE_UTILS_LIBRARY to its path"
    )


def load():
    """Open the library and declare its functions."""
    # on Windows, the standard search order (winmode 0), which includes PATH,
    # finds the compiler's runtime DLLs the library depends on; by default
    # Python does not search PATH for them
    winmode = 0 if os.name == "nt" else None
    library = ctypes.CDLL(str(library_path()), winmode=winmode)
    for name, (result, arguments) in SIGNATURES.items():
        function = getattr(library, f"triangle_utils_{name}")
        function.restype = result
        function.argtypes = arguments
    return library


def doubles(values):
    """A ctypes array of doubles."""
    return (ctypes.c_double * len(values))(*values)
