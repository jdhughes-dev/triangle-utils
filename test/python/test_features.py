"""
Tests reading features from shapefiles, GeoDataFrames, and shapely geometries.

The tests are skipped where geopandas, an optional dependency, is not
installed.

Cases:
  - test_valley        : the user guide's valley read from shapefiles, with
                         markers stored as doubles, and from GeoDataFrames
                         meshes to the same mesh as its coordinates.
  - test_parts         : multi-part features and collections give one feature
                         per part with their row's values, a polygon's holes
                         are hole polygons, rings are counterclockwise, z is
                         dropped, and null and empty shapes are skipped, even
                         with missing values.
  - test_points        : point features are added as vertices of the mesh or
                         as holes.
  - test_files         : a GeoPackage layer, GeoJSON, and a shapefile with a
                         null shape and an integer field are read.
  - test_multipatch    : a multipatch of rings is a polygon with a hole; one
                         of triangle strips raises ValueError.
  - test_crs           : a geographic CRS raises ValueError; a projected one
                         or none is read.
  - test_bad_values    : a missing column or value, a value that is not a
                         finite number, a marker that is not a whole number,
                         a column name for shapely geometries, a layer of a
                         GeoDataFrame, and a bad kind or points argument
                         raise ValueError, before any feature is added.
"""

import runpy
import struct
from pathlib import Path

import numpy as np
import pytest
import triangle_utils as tu

geopandas = pytest.importorskip("geopandas")
shapely = pytest.importorskip("shapely")

REPOSITORY = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def valley():
    return runpy.run_path(str(REPOSITORY / "doc" / "UserGuide" / "valley.py"))


def valley_frames(valley):
    """The valley's features as GeoDataFrames, with the guide's values in
    columns, as a GIS would hold them."""
    from shapely.geometry import LineString, Point, Polygon

    def frame(geometries, **columns):
        return geopandas.GeoDataFrame(columns, geometry=geometries, crs="EPSG:26916")

    # markers as doubles, as a shapefile's numeric fields may be read
    boundary = frame(
        [Polygon(valley["BOUNDARY"])],
        marker=[float(valley["BOUNDARY_MARKER"])],
        spacing=[valley["BOUNDARY_SPACING"]],
    )
    streams = frame(
        [LineString(stream) for stream in valley["STREAMS"]],
        name=["river", "tributary"],
        marker=[float(valley["RIVER_MARKER"])] * 2,
        spacing=[valley["RIVER_SPACING"]] * 2,
    )
    lake = frame(
        [Polygon(valley["LAKE"])],
        marker=[valley["LAKE_MARKER"]],
        spacing=[valley["LAKE_SPACING"]],
    )
    aquifer, lake_region = valley["AQUIFER"], valley["LAKE_REGION"]
    regions = frame(
        [Point(500, 1000), Point(4000, 1000), Point(3500, 6500), Point(1100, 5400)],
        zone=[aquifer, aquifer, aquifer, lake_region],
        area=[0.0, 0.0, 0.0, valley["LAKE_AREA"]],
    )
    wells = frame([Point(xy) for xy in valley["WELLS"]])
    return boundary, streams, lake, regions, wells


def valley_mesh(valley, boundary, streams, lake, regions, wells):
    """The valley's mesh from its features, read from files or frames."""

    def read(source, **values):
        return tu.read_features(source, **values)

    columns = {"marker": "marker", "spacing": "spacing"}
    geometry = tu.Geometry()
    geometry.add_features(read(boundary, **columns))
    geometry.add_features(read(streams, **columns))
    geometry.add_features(read(lake, **columns))
    geometry.add_features(
        read(regions, attribute="zone", area_limit="area"), points="regions"
    )
    for well in read(wells):
        geometry.add_ring(
            *well.points[0],
            radius=valley["WELL_RADIUS"],
            point_count=valley["WELL_POINTS"],
            attribute=valley["WELL_REGION"],
            area_limit=valley["WELL_AREA"],
            marker=valley["WELL_MARKER"],
        )
    return tu.generate(
        geometry,
        maximum_area=valley["MAXIMUM_AREA"],
        minimum_angle=valley["MINIMUM_ANGLE"],
    )


def assert_same_mesh(mesh, expected):
    np.testing.assert_array_equal(mesh.vertices, expected.vertices)
    np.testing.assert_array_equal(mesh.triangles, expected.triangles)
    np.testing.assert_array_equal(mesh.attributes, expected.attributes)
    np.testing.assert_array_equal(mesh.segments, expected.segments)
    np.testing.assert_array_equal(mesh.segment_markers, expected.segment_markers)


def test_valley(valley, tmp_path):
    frames = valley_frames(valley)
    assert_same_mesh(valley_mesh(valley, *frames), valley["mesh"])
    paths = []
    for name, frame in zip(
        ("boundary", "streams", "lake", "regions", "wells"), frames, strict=True
    ):
        path = tmp_path / f"{name}.shp"
        frame.to_file(path)
        paths.append(path)
    assert_same_mesh(valley_mesh(valley, *paths), valley["mesh"])
    # each stream's points, ready for cells_along_line()
    streams = tu.read_features(paths[1])
    assert [feature.kind for feature in streams] == ["line", "line"]
    for feature, stream in zip(streams, valley["STREAMS"], strict=True):
        np.testing.assert_array_equal(feature.points, stream)


def test_parts():
    from shapely.geometry import (
        GeometryCollection,
        LineString,
        MultiLineString,
        MultiPoint,
        MultiPolygon,
        Point,
        Polygon,
    )

    square = [(0, 0), (10, 0), (10, 10), (0, 10)]
    # clockwise, as a shapefile stores a hole
    hole = [(4, 4), (4, 6), (6, 6), (6, 4)]
    # the null and empty shapes' values are missing
    frame = geopandas.GeoDataFrame(
        {
            "marker": [1, None, 3, None, 5, 6],
            "spacing": [1.0, np.nan, 3.0, np.nan, 5.0, 6.0],
        },
        geometry=[
            MultiPolygon(
                [Polygon(square, [hole]), Polygon([(20, 0), (30, 0), (20, 10)])]
            ),
            None,
            MultiLineString([[(0, 0, 5), (1, 1, 5)], [(2, 2), (3, 3), (4, 2)]]),
            Polygon(),
            MultiPoint([(1, 2), (3, 4)]),
            GeometryCollection([Point(5, 6), LineString([(7, 8), (9, 9)])]),
        ],
    )
    features = tu.read_features(frame, marker="marker", spacing="spacing")
    assert [(f.kind, f.row, f.marker, f.spacing) for f in features] == [
        ("polygon", 0, 1, 1.0),
        ("hole_polygon", 0, 1, 1.0),
        ("polygon", 0, 1, 1.0),
        ("line", 2, 3, 3.0),
        ("line", 2, 3, 3.0),
        ("point", 4, 5, 5.0),
        ("point", 4, 5, 5.0),
        ("point", 5, 6, 6.0),
        ("line", 5, 6, 6.0),
    ]
    np.testing.assert_array_equal(features[0].points, square)
    np.testing.assert_array_equal(features[1].points, [hole[0], *hole[:0:-1]])
    np.testing.assert_array_equal(features[3].points, [(0, 0), (1, 1)])
    # a clockwise outer boundary, as a shapefile stores it
    clockwise = tu.read_features(Polygon(square[::-1]))
    np.testing.assert_array_equal(clockwise[0].points, [square[-1], *square[:-1]])
    # shapely geometries alone, with numbers for the values
    features = tu.read_features(Polygon(square, [hole]), marker=7, spacing=0.5)
    assert [(f.kind, f.marker, f.spacing) for f in features] == [
        ("polygon", 7, 0.5),
        ("hole_polygon", 7, 0.5),
    ]
    geometry = tu.Geometry()
    geometry.add_features(features)
    mesh = tu.generate(geometry, maximum_area=1.0)
    areas = mesh.triangle_areas()
    assert areas.sum() == pytest.approx(100 - 4)
    assert set(mesh.segment_markers.tolist()) == {7}


def test_points():
    from shapely.geometry import MultiPoint, Polygon

    square = [(0, 0), (10, 0), (10, 10), (0, 10)]
    inner = [(4, 4), (6, 4), (6, 6), (4, 6)]
    points = [(2.5, 7.5), (7.5, 2.5)]
    geometry = tu.Geometry()
    geometry.add_features(tu.read_features([Polygon(square), Polygon(inner)]))
    geometry.add_features(tu.read_features(MultiPoint(points)))
    mesh = tu.generate(geometry)
    for point in points:
        assert any(np.array_equal(vertex, point) for vertex in mesh.vertices)
    assert mesh.triangle_areas().sum() == pytest.approx(100)
    # a point inside the inner polygon as a hole
    geometry = tu.Geometry()
    geometry.add_features(tu.read_features([Polygon(square), Polygon(inner)]))
    geometry.add_features(tu.read_features(MultiPoint([(5, 5)])), points="holes")
    mesh = tu.generate(geometry)
    assert mesh.triangle_areas().sum() == pytest.approx(100 - 4)


def test_files(tmp_path):
    from shapely.geometry import LineString, Point

    lines = geopandas.GeoDataFrame(
        {"marker": [2]}, geometry=[LineString([(0, 0), (5, 5)])], crs="EPSG:26916"
    )
    points = geopandas.GeoDataFrame(
        {"marker": [3, 4]}, geometry=[Point(1, 2), None], crs="EPSG:26916"
    )
    package = tmp_path / "features.gpkg"
    lines.to_file(package, layer="lines")
    points.to_file(package, layer="points")
    features = tu.read_features(package, layer="points", marker="marker")
    assert [(f.kind, f.row, f.marker) for f in features] == [("point", 0, 3)]
    lines.to_file(tmp_path / "lines.geojson")
    features = tu.read_features(tmp_path / "lines.geojson", marker="marker")
    assert [(f.kind, f.marker) for f in features] == [("line", 2)]
    # a shapefile's integer field and null shape
    points.to_file(tmp_path / "points.shp")
    read = geopandas.read_file(tmp_path / "points.shp")
    assert read.geometry.iloc[1] is None
    assert np.issubdtype(read["marker"].dtype, np.integer)
    features = tu.read_features(tmp_path / "points.shp", marker="marker")
    assert [(f.kind, f.row, f.marker) for f in features] == [("point", 0, 3)]


def write_multipatch(path, parts):
    """A multipatch shapefile of one shape with the parts, each a part type
    and its (x, y) points, and a table of one field."""
    xy = np.concatenate([points for _, points in parts]).astype("<f8")
    starts = np.cumsum([0] + [len(points) for _, points in parts[:-1]])
    box = (*xy.min(axis=0), *xy.max(axis=0))
    content = struct.pack("<i4d2i", 31, *box, len(parts), len(xy))
    content += struct.pack(f"<{len(parts)}i", *starts)
    content += struct.pack(f"<{len(parts)}i", *[kind for kind, _ in parts])
    # the points, then the range of z and each point's z
    content += xy.tobytes() + struct.pack("<2d", 0, 0) + bytes(8 * len(xy))
    record = struct.pack(">2i", 1, len(content) // 2) + content

    def header(length):
        # the file code, the file's length in 16-bit words, the version, the
        # shape type, and the box
        return (
            struct.pack(">7i", 9994, 0, 0, 0, 0, 0, length // 2)
            + struct.pack("<2i", 1000, 31)
            + struct.pack("<8d", *box, 0, 0, 0, 0)
        )

    path.with_suffix(".shp").write_bytes(header(100 + len(record)) + record)
    index = struct.pack(">2i", 50, len(content) // 2)
    path.with_suffix(".shx").write_bytes(header(100 + len(index)) + index)
    # dBASE III: one record of one numeric field, ID, four wide
    table = struct.pack("<4BIHH20x", 3, 126, 1, 1, 1, 32 + 32 + 1, 1 + 4)
    table += struct.pack("<11sc4xBB14x", b"ID", b"N", 4, 0) + b"\r"
    table += b"    1" + b"\x1a"
    path.with_suffix(".dbf").write_bytes(table)


def test_multipatch(tmp_path):
    outer, inner, strip = 2, 3, 0
    square = np.array([(0, 0), (10, 0), (10, 10), (0, 10), (0, 0)], dtype=float)
    hole = np.array([(4, 4), (4, 6), (6, 6), (6, 4), (4, 4)], dtype=float)
    rings = tmp_path / "rings.shp"
    write_multipatch(rings, [(outer, square), (inner, hole)])
    features = tu.read_features(rings)
    assert [f.kind for f in features] == ["polygon", "hole_polygon"]
    np.testing.assert_array_equal(features[0].points, square[:-1])
    np.testing.assert_array_equal(features[1].points, [hole[0], *hole[-2:0:-1]])
    triangles = tmp_path / "triangles.shp"
    write_multipatch(
        triangles, [(outer, square), (strip, np.array([(20, 0), (22, 0), (20, 2)]))]
    )
    with pytest.raises(ValueError, match="multipatch"):
        tu.read_features(triangles)


def test_crs():
    from shapely.geometry import Point

    point = [Point(-89.4, 43.1)]
    frame = geopandas.GeoDataFrame(geometry=point, crs="EPSG:4326")
    with pytest.raises(ValueError, match="geographic"):
        tu.read_features(frame)
    projected = tu.read_features(frame.to_crs("EPSG:26916"))
    assert projected[0].points[0, 0] > 1000
    assert len(tu.read_features(geopandas.GeoDataFrame(geometry=point))) == 1


def test_bad_values():
    from shapely.geometry import Point

    frame = geopandas.GeoDataFrame(
        {"marker": [1.0, np.nan], "fraction": [1.5, 2.0]},
        geometry=[Point(0, 0), Point(1, 1)],
    )
    with pytest.raises(ValueError, match="lacks"):
        tu.read_features(frame, marker="markers")
    with pytest.raises(ValueError, match="no value in row 1"):
        tu.read_features(frame, marker="marker")
    with pytest.raises(ValueError, match="whole number"):
        tu.read_features(frame, marker="fraction")
    for value in (np.inf, np.nan, None):
        with pytest.raises(ValueError, match="must be a"):
            tu.read_features(frame, spacing=value)
    with pytest.raises(ValueError, match="'text' in row 0 must be a number"):
        tu.read_features(frame.assign(text=["abc", "1"]), spacing="text")
    with pytest.raises(ValueError, match="names a column"):
        tu.read_features([Point(0, 0)], marker="marker")
    with pytest.raises(ValueError, match="layer"):
        tu.read_features(frame, layer="wells")
    geometry = tu.Geometry()
    with pytest.raises(ValueError, match="points must be"):
        geometry.add_features([], points="wells")
    with pytest.raises(ValueError, match="kind"):
        geometry.add_features(
            [
                tu.Feature("point", np.zeros((1, 2)), 0),
                tu.Feature("ring", np.zeros((1, 2)), 0),
            ]
        )
    # nothing was added
    assert geometry == tu.Geometry()
