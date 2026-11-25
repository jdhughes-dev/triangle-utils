"""The example valley: meshed, refined, improved, made a Voronoi grid, and its
river and boundary mapped to the grid's cells."""

import triangle_utils as tu

# the valley, in meters: its boundary, a river that crosses it from north to
# south, starting and ending on boundary edges, a lake, and three wells
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
# the attribute of each region, and the marker of each feature's segments
AQUIFER, LAKE_REGION, WELL_REGION = 1, 2, 3
BOUNDARY_MARKER, RIVER_MARKER, LAKE_MARKER, WELL_MARKER = 1, 2, 3, 4

geometry = tu.Geometry()
geometry.add_polygon(BOUNDARY, spacing=BOUNDARY_SPACING, marker=BOUNDARY_MARKER)
geometry.add_line(RIVER, spacing=RIVER_SPACING, marker=RIVER_MARKER)
geometry.add_polygon(LAKE, spacing=LAKE_SPACING, marker=LAKE_MARKER)
# a point inside each region; the river divides the aquifer, so it needs one
# on each side
geometry.add_region(500, 1000, attribute=AQUIFER)
geometry.add_region(4000, 1000, attribute=AQUIFER)
geometry.add_region(1100, 5400, attribute=LAKE_REGION, area_limit=LAKE_AREA)
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

# the river's cells, in order, with the length of river in each, for
# streamflow routing reaches, and the cells on the boundary, with the length
# of boundary in each, for boundary conductances
river_cells = tu.cells_along_line(grid, RIVER)
boundary_cells = tu.cells_on_segments(grid, delaunay_refined, BOUNDARY_MARKER)
