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


def _values(frame, value, name, convert):
    """A value for each row: a column's, or the same number for all."""
    rows = len(frame)
    if not isinstance(value, str):
        return [convert(value, name)] * rows
    if not hasattr(frame, "columns"):
        raise ValueError(f"{name} names a column, but the source has none")
    if value not in frame.columns:
        raise ValueError(f"{name} names column {value!r}, which the source lacks")
    import pandas

    values = []
    for row, item in enumerate(frame[value]):
        if pandas.isna(item):
            raise ValueError(f"column {value!r} has no value in row {row}")
        values.append(convert(item, f"column {value!r} in row {row}"))
    return values


def _float(value, name):
    """A number for a double argument."""
    return float(value)


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
    """The parts of the features of a file geopandas reads (a shapefile,
    GeoPackage, or GeoJSON), a GeoDataFrame, or shapely geometries.

    Each value is a number for every feature or, for a file or GeoDataFrame,
    the name of the column that holds it. A polygon's holes are hole polygons
    with its values, every polygon is counterclockwise, and empty and null
    shapes are skipped. The coordinates
    must be projected; a geographic CRS raises ValueError."""
    from . import _int

    frame = _frame(source, layer)
    crs = getattr(frame, "crs", None)
    if crs is not None and crs.is_geographic:
        raise ValueError(
            f"the coordinates are geographic ({crs.name}); project them, for "
            "example with GeoDataFrame.to_crs()"
        )
    values = zip(
        _values(frame, marker, "marker", _int),
        _values(frame, spacing, "spacing", _float),
        _values(frame, attribute, "attribute", _float),
        _values(frame, area_limit, "area_limit", _float),
    )
    geometries = frame.geometry if hasattr(frame, "geometry") else frame
    features = []
    for row, (geometry, row_values) in enumerate(zip(geometries, values)):
        for kind, points in _parts(geometry):
            features.append(Feature(kind, np.array(points), row, *row_values))
    return features
