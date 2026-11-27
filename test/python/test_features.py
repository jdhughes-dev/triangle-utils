"""
Tests reading features from shapefiles, GeoDataFrames, and shapely geometries.

The tests are skipped where geopandas, an optional dependency, is not
installed.

Cases:
  - test_valley        : the user guide's valley, read from shapefiles with
                         markers stored as doubles, by geopandas or the
                         library, or from GeoDataFrames, meshes to the same
                         mesh as its coordinates.
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
  - test_read_shapefile : the library's reader gives the features geopandas
                         does from shapefiles of polygons with holes, lines
                         with z, and points, with integer, decimal, and text
                         fields and null shapes, and from multipatch rings;
                         a missing field or file, a marker that is not whole,
                         and a multipatch of triangles raise.
  - test_c_api_features : the C API reads a shapefile's points and values
                         and adds them to geometry; wrong counts, null
                         pointers, and a missing file are errors.
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


def valley_mesh(valley, boundary, streams, lake, regions, wells, read=None):
    """The valley's mesh from its features, read from files or frames by
    read_features(), or by read if given."""
    read = read or tu.read_features

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
    # the library's reader, without geopandas
    assert_same_mesh(
        valley_mesh(valley, *paths, read=tu.read_shapefile), valley["mesh"]
    )
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
    # a clockwise 1 mm square at UTM coordinates, whose area from the raw
    # coordinates rounds to zero
    corner = np.array([500000.123, 4500000.456])
    small = corner + 0.001 * np.array(square[::-1], dtype=float) / 10
    np.testing.assert_array_equal(
        tu.read_features(Polygon(small))[0].points, [small[0], *small[:0:-1]]
    )
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
    with pytest.raises(ValueError, match="repeats"):
        tu.read_features(frame.assign(m=1, n=2).rename(columns={"n": "m"}), marker="m")
    with pytest.raises(ValueError, match="not a shapely geometry"):
        tu.read_features([b"abc"])
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
    with pytest.raises(ValueError, match="columns"):
        geometry.add_features(
            [
                tu.Feature("polygon", np.eye(3, 2), 0),
                tu.Feature("line", np.zeros((2, 3)), 1),
            ]
        )
    with pytest.raises(ValueError, match="whole number"):
        geometry.add_features(
            [
                tu.Feature("polygon", np.eye(3, 2), 0),
                tu.Feature("line", np.eye(2), 1, marker=1.5),
            ]
        )
    # nothing was added
    assert geometry == tu.Geometry()


def assert_same_features(features, expected):
    assert len(features) == len(expected)
    for feature, other in zip(features, expected, strict=True):
        assert (feature.kind, feature.row, feature.marker) == (
            other.kind,
            other.row,
            other.marker,
        )
        assert (feature.spacing, feature.attribute, feature.area_limit) == (
            other.spacing,
            other.attribute,
            other.area_limit,
        )
        np.testing.assert_array_equal(feature.points, other.points)


def test_read_shapefile(tmp_path):
    from shapely.geometry import (
        LineString,
        MultiLineString,
        MultiPoint,
        MultiPolygon,
        Polygon,
    )

    square = [(0, 0), (10, 0), (10, 10), (0, 10)]
    hole = [(4, 4), (4, 6), (6, 6), (6, 4)]
    corner = np.array([500000.123, 4500000.456])
    small = corner + 0.001 * np.array(square[::-1], dtype=float) / 10
    frames = {
        "polygons": [
            MultiPolygon(
                [Polygon(square, [hole]), Polygon([(20, 0), (30, 0), (20, 10)])]
            ),
            None,
            Polygon(small),
            # draped, with z varying along the hole
            Polygon(
                [(40, 0, 1), (50, 0, 2), (50, 10, 3), (40, 10, 4)],
                [[(44, 4, 5), (44, 6, 6), (46, 6, 7), (46, 4, 8)]],
            ),
        ],
        "lines": [
            MultiLineString([[(0, 0, 5), (1, 1, 5)], [(2, 2, 5), (3, 3, 5)]]),
            None,
            LineString([(7, 8, 5), (9, 9, 5)]),
            LineString([(1, 1, 5), (2, 1, 6)]),
        ],
        "points": [
            MultiPoint([(1, 2), (3, 4)]),
            None,
            MultiPoint([(5, 6)]),
            MultiPoint([(7, 8)]),
        ],
    }
    for name, geometries in frames.items():
        # an integer, a decimal, and a text field, missing for the null shape
        frame = geopandas.GeoDataFrame(
            {
                "marker": [1, None, 3, 4],
                "spacing": [1.5, np.nan, 3.5, 4.5],
                "zone": ["2.5", None, "7", "8"],
            },
            geometry=geometries,
            crs="EPSG:26916",
        )
        path = tmp_path / f"{name}.shp"
        frame.to_file(path)
        values = {"marker": "marker", "spacing": "spacing", "attribute": "zone"}
        assert_same_features(
            tu.read_shapefile(path, **values, area_limit=0.25),
            tu.read_features(path, **values, area_limit=0.25),
        )
        assert_same_features(tu.read_shapefile(path), tu.read_features(path))
    with pytest.raises(tu.Error, match=r"the marker field missing is not in the \.dbf"):
        tu.read_shapefile(tmp_path / "points.shp", marker="missing")
    with pytest.raises(ValueError, match="marker"):
        tu.read_shapefile(tmp_path / "points.shp", marker=1.5)
    with pytest.raises(tu.Error, match="cannot open the shapefile"):
        tu.read_shapefile(tmp_path / "none.shp")

    outer, inner, strip = 2, 3, 0
    ring = np.array([*square, square[0]], dtype=float)
    inside = np.array([*hole, hole[0]], dtype=float)
    rings = tmp_path / "rings.shp"
    write_multipatch(rings, [(outer, ring), (inner, inside)])
    assert_same_features(tu.read_shapefile(rings), tu.read_features(rings))
    triangles = tmp_path / "triangles.shp"
    write_multipatch(triangles, [(strip, np.array([(20, 0), (22, 0), (20, 2)]))])
    with pytest.raises(tu.Error, match="is a multipatch of triangles"):
        tu.read_shapefile(triangles)


def test_c_api_features(library, tmp_path):
    import ctypes

    from triangle_utils._library import FEATURES_POINTER, GEOMETRY_POINTER

    ok, error, invalid = 0, 1, 2
    from shapely.geometry import Point

    geopandas.GeoDataFrame(
        {"zone": [4.0, 5.0]}, geometry=[Point(1, 2), Point(3, 4)], crs="EPSG:26916"
    ).to_file(tmp_path / "points.shp")
    name = str(tmp_path / "points.shp").encode()
    features = ctypes.cast(ctypes.c_void_p(1), FEATURES_POINTER)
    read = library.triangle_utils_shapefile_read
    assert (
        read(name, None, 2, None, 0.0, b"zone", 0.0, b"", 9.0, ctypes.byref(features))
        == ok
    )
    try:
        counts = [ctypes.c_int(-1), ctypes.c_int(-1)]
        status = library.triangle_utils_features_count(
            features, *[ctypes.byref(c) for c in counts]
        )
        assert status == ok
        assert [c.value for c in counts] == [2, 2]
        kinds, rows, markers = ((ctypes.c_int * 2)() for _ in range(3))
        offsets = (ctypes.c_int * 3)()
        xy = (ctypes.c_double * 4)()
        spacings, attributes, limits = ((ctypes.c_double * 2)() for _ in range(3))
        arguments = [kinds, rows, offsets, 2, xy, markers, spacings, attributes, limits]
        assert library.triangle_utils_features(features, 2, *arguments) == ok
        assert list(kinds) == [3, 3]
        assert list(rows) == [0, 1]
        assert list(offsets) == [0, 1, 2]
        assert list(xy) == [1, 2, 3, 4]
        assert list(markers) == [2, 2]
        assert list(attributes) == [4, 5]
        assert list(limits) == [9, 9]
        assert library.triangle_utils_features(features, 3, *arguments) == invalid
        assert (
            library.triangle_utils_features(
                features, 2, *arguments[:2], None, *arguments[3:]
            )
            == invalid
        )
        geometry = GEOMETRY_POINTER()
        assert library.triangle_utils_geometry_create(ctypes.byref(geometry)) == ok
        try:
            add = library.triangle_utils_geometry_add_features
            assert add(geometry, features, 1) == ok
            assert add(geometry, features, 3) == invalid
            assert add(geometry, None, 0) == invalid
            assert add(None, features, 0) == invalid
        finally:
            library.triangle_utils_geometry_free(geometry)
    finally:
        library.triangle_utils_features_free(features)
    library.triangle_utils_features_free(None)
    features = ctypes.cast(ctypes.c_void_p(1), FEATURES_POINTER)
    status = read(
        b"none.shp", None, 0, None, 0, None, 0, None, 0, ctypes.byref(features)
    )
    assert status == error
    assert not features
    assert library.triangle_utils_last_error().startswith(b"cannot open the shapefile")
    assert (
        read(None, None, 0, None, 0, None, 0, None, 0, ctypes.byref(features))
        == invalid
    )
    assert read(name, None, 0, None, 0, None, 0, None, 0, None) == invalid
