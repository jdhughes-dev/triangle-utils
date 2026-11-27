"""The example valley's geometry read from shapefiles, as a GIS would hold it.

The valley's features are written to shapefiles in a temporary folder, with
the values of valley.py in their columns, and read back with read_features;
the geometry meshes to the same mesh as valley.py's coordinates. Needs
geopandas (triangle-utils[gis]).
"""

import atexit
import shutil
import tempfile
from pathlib import Path

import geopandas
import triangle_utils as tu
from shapely.geometry import LineString, Point, Polygon
from valley import (
    BOUNDARY,
    BOUNDARY_MARKER,
    BOUNDARY_SPACING,
    LAKE,
    LAKE_MARKER,
    LAKE_SPACING,
    MAXIMUM_AREA,
    MINIMUM_ANGLE,
    REGIONS,
    RIVER_MARKER,
    RIVER_SPACING,
    STREAMS,
    WELL_AREA,
    WELL_MARKER,
    WELL_POINTS,
    WELL_RADIUS,
    WELL_REGION,
    WELLS,
)

# a projected coordinate system, as a GIS gives one; the valley is in meters
CRS = "EPSG:26916"


def write_shapefiles(folder):
    """Write the valley's features to shapefiles in a folder."""
    frames = {
        "boundary": ([Polygon(BOUNDARY)], BOUNDARY_MARKER, BOUNDARY_SPACING),
        "streams": ([LineString(s) for s in STREAMS], RIVER_MARKER, RIVER_SPACING),
        "lake": ([Polygon(LAKE)], LAKE_MARKER, LAKE_SPACING),
    }
    for name, (shapes, marker, spacing) in frames.items():
        columns = {"marker": [marker] * len(shapes), "spacing": [spacing] * len(shapes)}
        frame = geopandas.GeoDataFrame(columns, geometry=shapes, crs=CRS)
        frame.to_file(folder / f"{name}.shp")
    regions = geopandas.GeoDataFrame(
        {
            "zone": [attribute for _, attribute, _ in REGIONS],
            "area": [area for _, _, area in REGIONS],
        },
        geometry=[Point(xy) for xy, _, _ in REGIONS],
        crs=CRS,
    )
    regions.to_file(folder / "regions.shp")
    wells = geopandas.GeoDataFrame(geometry=[Point(xy) for xy in WELLS], crs=CRS)
    wells.to_file(folder / "wells.shp")


folder = Path(tempfile.mkdtemp())
atexit.register(shutil.rmtree, folder, ignore_errors=True)
write_shapefiles(folder)

# the boundary, the streams, and the lake, with the marker and spacing in
# columns of those names; the region points, with the attribute and area
# limit in the zone and area columns; and a ring around each well
columns = {"marker": "marker", "spacing": "spacing"}
geometry = tu.Geometry()
for name in ["boundary", "streams", "lake"]:
    geometry.add_features(tu.read_features(folder / f"{name}.shp", **columns))
regions = tu.read_features(folder / "regions.shp", attribute="zone", area_limit="area")
geometry.add_features(regions, points="regions")
for well in tu.read_features(folder / "wells.shp"):
    x, y = well.points[0]
    geometry.add_ring(
        x,
        y,
        radius=WELL_RADIUS,
        point_count=WELL_POINTS,
        attribute=WELL_REGION,
        area_limit=WELL_AREA,
        marker=WELL_MARKER,
    )

mesh = tu.generate(geometry, maximum_area=MAXIMUM_AREA, minimum_angle=MINIMUM_ANGLE)

# the library reads a shapefile itself, without geopandas, as a C or Fortran
# program does
streams = tu.read_shapefile(folder / "streams.shp", **columns)
