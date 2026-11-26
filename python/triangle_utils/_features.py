"""
Features read from GIS files, GeoDataFrames, and shapely geometries.

Each part of a feature becomes the (x, y) points Geometry and the mapping
functions take; geopandas and shapely are imported only when features are
read.
"""

import os
from dataclasses import dataclass

import numpy as np

KINDS = ("polygon", "hole_polygon", "line", "point")


@dataclass(eq=False)
class Feature:
    """One part of a feature: its kind (one of KINDS), its (x, y) points, the
    row of the source it came from, and its values."""

    kind: str
    points: np.ndarray
    row: int
    marker: int = 0
    spacing: float = 0.0
    attribute: float = 0.0
    area_limit: float = 0.0


def _frame(source, layer):
    """The source as a GeoDataFrame, or a list of shapely geometries."""
    if isinstance(source, (str, os.PathLike)):
        import geopandas
        import shapely

        try:
            return geopandas.read_file(source, layer=layer)
        except shapely.errors.GEOSException as error:
            # GDAL reads a multipatch of triangle strips or fans as a TIN,
            # which shapely does not take
            raise ValueError(
                f"{source} has a shape that cannot be read ({error}), such as a "
                "multipatch of triangles; convert it to polygons, for example "
                "with ogr2ogr -nlt MULTIPOLYGON"
            ) from None
    if layer is not None:
        raise ValueError("layer is only for a file")
    if hasattr(source, "geometry"):
        return source
    import shapely

    if isinstance(source, shapely.Geometry):
        return [source]
    return list(source)


def _value(frame, value, name, whole):
    """A function of a row that gives its value: a column's, or a number."""
    if not isinstance(value, str):
        number = _number(value, name, whole)
        return lambda row: number
    if not hasattr(frame, "columns"):
        raise ValueError(f"{name} names a column, but the source has none")
    if value not in frame.columns:
        raise ValueError(f"{name} names column {value!r}, which the source lacks")
    import pandas

    column = frame[value].tolist()

    def row_value(row):
        if pandas.isna(column[row]):
            raise ValueError(f"column {value!r} has no value in row {row}")
        return _number(column[row], f"column {value!r} in row {row}", whole)

    return row_value


def _number(value, name, whole):
    """A finite number, or a whole number if whole; ValueError if not."""
    try:
        number = float(value)
    except (TypeError, ValueError):
        raise ValueError(f"{name} must be a number, not {value!r}") from None
    if not np.isfinite(number):
        raise ValueError(f"{name} must be a finite number, not {value!r}")
    if whole:
        from . import _int

        return _int(number, name)
    return number


def _ring(ring):
    """A ring's points, counterclockwise from its first, without the last,
    which repeats the first."""
    import shapely

    points = shapely.get_coordinates(ring)[:-1]
    x, y = points.T
    if np.dot(x, np.roll(y, -1)) - np.dot(np.roll(x, -1), y) < 0:
        points = np.vstack([points[:1], points[:0:-1]])
    return points


def _parts(geometry):
    """The (kind, points) of each part of a geometry; nothing if it is empty."""
    import shapely

    if geometry is None or geometry.is_empty:
        return
    kind = geometry.geom_type
    if kind == "Polygon":
        # counterclockwise, since a shapefile's outer rings are clockwise and
        # the order sets the mesh's numbering
        yield "polygon", _ring(geometry.exterior)
        for ring in geometry.interiors:
            yield "hole_polygon", _ring(ring)
    elif kind in ("LineString", "LinearRing"):
        yield "line", shapely.get_coordinates(geometry)
    elif kind == "Point":
        yield "point", shapely.get_coordinates(geometry)
    elif hasattr(geometry, "geoms"):
        for part in geometry.geoms:
            yield from _parts(part)
    else:
        raise ValueError(f"a {kind} is not a point, line, or polygon")


def read_features(
    source, layer=None, marker=0, spacing=0.0, attribute=0.0, area_limit=0.0
):
    """A list of a Feature for each part of the features of a file geopandas
    reads (a shapefile, GeoPackage, or GeoJSON), a GeoDataFrame, or shapely
    geometries.

    Each value is a number or, for a file or GeoDataFrame, the name of the
    column that holds it. A polygon's holes are hole polygons with its values,
    every polygon is counterclockwise, empty and null shapes are skipped, and
    a feature's row is its position in the source. A geographic CRS raises
    ValueError; a source without a CRS is taken to be projected."""
    frame = _frame(source, layer)
    crs = getattr(frame, "crs", None)
    if crs is not None and crs.is_geographic:
        raise ValueError(
            f"the coordinates are geographic ({crs.name}); project them, for "
            "example with GeoDataFrame.to_crs()"
        )
    # each value's function of a row, with numbers checked now and a
    # column's values when their row has a shape, since a null shape's may
    # be missing
    values = [
        _value(frame, value, name, whole)
        for name, value, whole in (
            ("marker", marker, True),
            ("spacing", spacing, False),
            ("attribute", attribute, False),
            ("area_limit", area_limit, False),
        )
    ]
    geometries = frame.geometry if hasattr(frame, "geometry") else frame
    features = []
    for row, geometry in enumerate(geometries):
        parts = list(_parts(geometry))
        if parts:
            row_values = [value(row) for value in values]
            features.extend(
                Feature(kind, np.array(points), row, *row_values)
                for kind, points in parts
            )
    return features
