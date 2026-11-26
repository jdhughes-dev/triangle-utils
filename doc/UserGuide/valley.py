"""The example valley: meshed, refined, improved, made a Voronoi grid with its
cells' connections, its streams, boundary, and wells mapped to the grid's
cells, and the grid given three layers."""

import numpy as np
import triangle_utils as tu

# the valley, in meters: its boundary, a river that crosses it from north to
# south, starting and ending on boundary edges, a tributary that enters
# across the east boundary and joins the river at its second bend, a lake,
# and three wells
BOUNDARY = [
    (0, 0),
    (4000, 0),
    (4600, 2200),
    (4200, 6000),
    (2600, 8000),
    (600, 7600),
    (0, 4000),
]
RIVER = [
    (1800, 7840),
    (1900, 6500),
    (2300, 5200),
    (2100, 3800),
    (2500, 2400),
    (2600, 1000),
    (2400, 0),
]
TRIBUTARY = [(4300, 5050), (3700, 5550), (2900, 5500), (2300, 5200)]
# the streams, each a line of its own, as the features of a polyline
# shapefile give them
STREAMS = [RIVER, TRIBUTARY]
LAKE = [(700, 5200), (1300, 4700), (1500, 5400), (1200, 6200), (700, 6000)]
WELLS = [(3300, 4400), (1000, 2000), (3400, 1500)]

# the spacing of points along each feature, in meters
BOUNDARY_SPACING = 200.0
RIVER_SPACING = 50.0
LAKE_SPACING = 60.0
# the largest triangle area in the aquifer and the lake, in square meters
MAXIMUM_AREA = 200.0 * 200.0
LAKE_AREA = 60.0 * 60.0
# each well's ring: its radius (m), its points, and its largest triangle
# area (m2)
WELL_RADIUS = 50.0
WELL_POINTS = 16
WELL_AREA = 300.0
# the smallest angle of the mesh, the largest angle refining aims for, and
# the largest angle improving aims for, in degrees
MINIMUM_ANGLE = 30.0
MAXIMUM_ANGLE = 100.0
IMPROVE_ANGLE = 90.0
# the layers, in meters, after the synthetic valley of Hughes and others
# (2024). The land surface rises from the river's mouth along the streams
# and up the valley's sides, over a ridge across the valley that the river
# cuts through in a gap, and falls into a basin shaped by the lake's shore.
# Below it are the bottom of an upper aquifer, a clay that thins from its
# greatest thickness at the west boundary to nothing at CLAY_EDGE, and a
# lower aquifer whose base dips to the west, with a relief of BASE_RELIEF
# along the valley; the thinnest cell kept is MINIMUM_THICKNESS
RIVER_MOUTH_ELEVATION = 2.0
RIVER_GRADIENT = 0.001
VALLEY_SIDE_SLOPE = 0.004
RIDGE_Y = 3800.0
RIDGE_HEIGHT = 12.0
RIDGE_WIDTH = 500.0
GAP_WIDTH = 250.0
# the lake's basin: its depth below the land around it, reached LAKE_INSET
# inside the shore, from which its sides rise to the land LAKE_RIM outside
LAKE_DEPTH = 8.0
LAKE_INSET = 150.0
LAKE_RIM = 200.0
UPPER_BOTTOM = -5.0
CLAY_THICKNESS = 11.0
CLAY_EDGE = 3000.0
BASE_WEST = -95.0
BASE_EAST = -45.0
BASE_RELIEF = 5.0
MINIMUM_THICKNESS = 1.0
# a revision of the geologic model that raises the clay's base
CLAY_RAISE = 1.0
# the attribute of each region, and the marker of each feature's segments
AQUIFER, LAKE_REGION, WELL_REGION = 1, 2, 3
BOUNDARY_MARKER, RIVER_MARKER, LAKE_MARKER, WELL_MARKER = 1, 2, 3, 4
# a point inside each region, its attribute, and its largest triangle area,
# or 0 for none; the streams divide the aquifer into three parts, so it
# needs one in each
REGIONS = [
    ((500, 1000), AQUIFER, 0.0),
    ((4000, 1000), AQUIFER, 0.0),
    ((3500, 6500), AQUIFER, 0.0),
    ((1100, 5400), LAKE_REGION, LAKE_AREA),
]

geometry = tu.Geometry()
geometry.add_polygon(BOUNDARY, spacing=BOUNDARY_SPACING, marker=BOUNDARY_MARKER)
# each stream with the river's spacing and marker; the point the tributary
# shares with the river becomes one vertex of the mesh
for stream in STREAMS:
    geometry.add_line(stream, spacing=RIVER_SPACING, marker=RIVER_MARKER)
geometry.add_polygon(LAKE, spacing=LAKE_SPACING, marker=LAKE_MARKER)
for (x, y), attribute, area_limit in REGIONS:
    geometry.add_region(x, y, attribute=attribute, area_limit=area_limit)
for x, y in WELLS:
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
refined = tu.refine(mesh, minimum_angle=MINIMUM_ANGLE, maximum_angle=MAXIMUM_ANGLE)
improved = tu.improve(refined, maximum_angle=IMPROVE_ANGLE)

# the Voronoi grid needs conforming Delaunay triangles, so every triangle's
# circumcenter is inside the mesh
delaunay_mesh = tu.generate(
    geometry,
    maximum_area=MAXIMUM_AREA,
    minimum_angle=MINIMUM_ANGLE,
    conforming_delaunay=True,
)
delaunay_refined = tu.refine(
    delaunay_mesh,
    minimum_angle=MINIMUM_ANGLE,
    maximum_angle=MAXIMUM_ANGLE,
    conforming_delaunay=True,
)
grid = tu.voronoi(delaunay_refined)
# the grid in the form FloPy's DISV package takes
disv_vertices = grid.disv_vertices()
disv_cell2d = grid.disv_cell2d()

# the connections between the grid's cells, for DISU: for each face two cells
# share, the distance from each center to the face, the face's width, and
# the direction of its normal
connections = tu.connect_cells(grid)

# each stream's cells, in order, with the length of stream in each, for
# streamflow routing reaches, the cells on the boundary, with the length of
# boundary in each, for boundary conductances, and the wells' cells
stream_cells = [tu.cells_along_line(grid, stream) for stream in STREAMS]
boundary_cells = tu.cells_on_segments(grid, delaunay_refined, BOUNDARY_MARKER)
well_cells = tu.cells_at_points(grid, WELLS)


def inside_polygon(points, polygon):
    """Whether each (x, y) point is inside a polygon."""
    x, y = np.asarray(points, dtype=float).T
    corners = np.asarray(polygon, dtype=float)
    inside = np.zeros(len(x), dtype=bool)
    for (x1, y1), (x2, y2) in zip(corners, np.roll(corners, -1, axis=0), strict=True):
        # an edge that a ray from the point to the east crosses
        spans = (y1 > y) != (y2 > y)
        with np.errstate(divide="ignore", invalid="ignore"):
            crossing = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
        inside ^= spans & (x < crossing)
    return inside


def distance_to_lines(points, lines):
    """The distance from each (x, y) point to the nearest of the lines."""
    xy = np.asarray(points, dtype=float)[:, np.newaxis, :]
    distances = []
    for line in lines:
        starts = np.array(line[:-1], dtype=float)
        ends = np.array(line[1:], dtype=float)
        step = ends - starts
        along = np.sum((xy - starts) * step, axis=2) / np.sum(step**2, axis=1)
        nearest = starts + np.clip(along, 0.0, 1.0)[..., np.newaxis] * step
        distances.append(np.hypot(*np.moveaxis(xy - nearest, 2, 0)).min(axis=1))
    return np.min(distances, axis=0)


# three layers: surfaces given at the mesh's vertices, as from a land-surface
# model or a geologic model, give each cell's top and the bottoms of the
# clay and the lower aquifer; where the clay is thinner than the minimum,
# its cells pass flow between the aquifers
def surfaces(vertices):
    """The land surface, the clay's base, and the aquifer's base at each
    (x, y) vertex of a mesh of the valley."""
    x, y = vertices.T
    stream_distance = distance_to_lines(vertices, STREAMS)
    ridge = np.exp(-(((y - RIDGE_Y) / RIDGE_WIDTH) ** 2))
    gap = 1.0 - np.exp(-((distance_to_lines(vertices, [RIVER]) / GAP_WIDTH) ** 2))
    # the distance from the lake's shore, negative inside the lake
    shore_distance = distance_to_lines(vertices, [[*LAKE, LAKE[0]]])
    shore_distance[inside_polygon(vertices, LAKE)] *= -1.0
    basin = np.clip((LAKE_RIM - shore_distance) / (LAKE_RIM + LAKE_INSET), 0.0, 1.0)
    land_surface = (
        RIVER_MOUTH_ELEVATION
        + RIVER_GRADIENT * y
        + VALLEY_SIDE_SLOPE * stream_distance
        + RIDGE_HEIGHT * ridge * gap
        - LAKE_DEPTH * basin
    )
    clay_base = UPPER_BOTTOM - CLAY_THICKNESS * np.clip(1.0 - x / CLAY_EDGE, 0.0, 1.0)
    aquifer_base = (
        BASE_WEST
        + (BASE_EAST - BASE_WEST) * x / np.max(x)
        + BASE_RELIEF * np.sin(2.0 * np.pi * y / np.max(y))
    )
    return land_surface, clay_base, aquifer_base


land_surface, clay_base, aquifer_base = surfaces(delaunay_refined.vertices)
top = tu.values_at_centers(grid, delaunay_refined, land_surface)
clay_bottom = tu.values_at_centers(grid, delaunay_refined, clay_base)
bottom = tu.values_at_centers(grid, delaunay_refined, aquifer_base)
layers = tu.layer_grid(
    grid,
    top,
    [UPPER_BOTTOM, clay_bottom, bottom],
    minimum_thickness=MINIMUM_THICKNESS,
)
# the arguments of FloPy's ModflowGwfdisv and ModflowGwfdisu
disv = layers.disv_arguments()
disu = layers.disu_arguments()

# the layers again after a revision raises the clay's base: more of the
# clay is thinner than the minimum and passes flow between the aquifers,
# and the DISU nodes and connections follow
revised = layers.with_elevations(top, [UPPER_BOTTOM, clay_bottom + CLAY_RAISE, bottom])
