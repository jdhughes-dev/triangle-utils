"""
Tests the Python module over the C API.

Cases:
  - test_generate      : a square with a hole, a river, regions, and a well
                         meshes to the domain's area, with each region's
                         attribute and the well's area limit.
  - test_refine        : refining the mesh by area limits and a maximum angle
                         keeps its area and segments, and counts what is left.
  - test_improve       : improving toward 90 degrees removes obtuse triangles,
                         keeps the area, and does not move the ring's center.
  - test_voronoi       : the Voronoi grid of a conforming Delaunay mesh has a
                         cell per vertex centered on it, covering the area,
                         in flopy's DISV form; a mesh that is not Delaunay
                         raises Error.
  - test_triangle_grid : a triangle grid has a cell per triangle with its
                         attributes; a bad center raises ValueError.
  - test_cells         : the river's cells in a Voronoi grid, in order, sum to
                         its length, the boundary's cells, with the hole's,
                         sum to their length, and the ring's center is in the
                         cell centered on it, a point outside in none; a grid
                         with a bad cell raises Error.
  - test_connections   : the Voronoi grid's connections are symmetric, each
                         face bisecting the line between its centers, and an
                         interior cell's area follows from them; circumcenters
                         outside their triangles raise Error, and a grid with
                         no cells has no connections.
  - test_layers        : elevations at the mesh's vertices give each cell's
                         value; top, botm, and idomain are taken as numbers,
                         values for each layer, or values for each cell, and
                         a minimum thickness makes thin cells pass-through;
                         the DISU nodes and connections match the layers,
                         and the arguments for FloPy hold the arrays;
                         crossing bottoms raise Error and bad shapes
                         ValueError.
  - test_shapes        : arrays of the wrong shape, length, or range, and
                         values too large for an int, raise ValueError before
                         the library reads them.
  - test_points_are_copied : changing a list after adding it does not change
                         the mesh.
  - test_version       : the versions of triangle-utils, Triangle, and its
                         release are returned.
  - test_error         : the library's reason is raised as Error.
"""

import itertools

import numpy as np
import pytest
import triangle_utils as tu


def square_geometry():
    geometry = tu.Geometry()
    geometry.add_polygon([(0, 0), (10, 0), (10, 10), (0, 10)], marker=1)
    geometry.add_hole_polygon([(6, 6), (8, 6), (8, 8), (6, 8)])
    # a river from side to side, with a region on each side
    geometry.add_line([(0, 3), (5, 4), (10, 3)], spacing=0.5, marker=2)
    geometry.add_region(1, 1, attribute=1)
    geometry.add_region(1, 9, attribute=2)
    geometry.add_ring(3, 7, radius=0.5, point_count=12, attribute=3, area_limit=0.01)
    return geometry


def test_generate():
    mesh = tu.generate(square_geometry(), maximum_area=0.5, minimum_angle=25)
    areas = mesh.triangle_areas()
    assert areas.min() > 0.0
    assert areas.max() <= 0.5 + 1e-12
    assert areas.sum() == pytest.approx(96.0)
    assert set(np.unique(mesh.attributes[:, 0])) == {1.0, 2.0, 3.0}
    assert areas[mesh.attributes[:, 0] == 3.0].max() <= 0.01 + 1e-12
    assert set(np.unique(mesh.segment_markers)) >= {1, 2}
    assert mesh.neighbors.shape == mesh.triangles.shape


def test_refine():
    mesh = tu.generate(square_geometry(), minimum_angle=25)
    mesh.area_limits = np.where(mesh.attributes[:, 0] == 1.0, 0.05, 0.0)
    refined = tu.refine(mesh, minimum_angle=25, maximum_angle=100)
    areas = refined.triangle_areas()
    assert areas.sum() == pytest.approx(96.0)
    assert areas[refined.attributes[:, 0] == 1.0].max() <= 0.05 + 1e-12
    assert len(refined.segments) > len(mesh.segments)
    assert tu.count_angles_over(refined, 100) == (refined.largest_angles() > 100).sum()


def test_version():
    utils, triangle, release = tu.version()
    assert utils.count(".") == 2
    assert triangle == "1.6"
    assert release.startswith("1.6.")


def test_improve():
    mesh = tu.generate(square_geometry(), minimum_angle=25)
    refined = tu.refine(mesh, minimum_angle=25, maximum_angle=100)
    improved = tu.improve(refined, maximum_angle=90)
    assert improved.triangle_areas().sum() == pytest.approx(96.0)
    assert tu.count_angles_over(improved, 90) < tu.count_angles_over(refined, 90)
    # the ring's center, a fixed vertex, does not move
    assert len(mesh.fixed_vertices) == 1
    np.testing.assert_array_equal(improved.fixed_vertices, refined.fixed_vertices)
    center = refined.vertices[refined.fixed_vertices[0]]
    np.testing.assert_array_equal(improved.vertices[improved.fixed_vertices[0]], center)
    with pytest.raises(ValueError, match="smoothing must be"):
        tu.improve(refined, smoothing="other")


def cell_areas(grid):
    """The area of each cell, positive for clockwise corners."""
    areas = []
    for cell in grid.cells:
        x, y = grid.vertices[cell].T
        areas.append(0.5 * (np.dot(x, np.roll(y, 1)) - np.dot(y, np.roll(x, 1))))
    return np.array(areas)


def test_voronoi():
    mesh = tu.generate(
        square_geometry(), maximum_area=0.5, minimum_angle=25, conforming_delaunay=True
    )
    grid = tu.voronoi(mesh)
    assert len(grid.cells) == len(mesh.vertices)
    np.testing.assert_array_equal(grid.sources, np.arange(len(mesh.vertices)))
    np.testing.assert_array_equal(grid.centers, mesh.vertices)
    areas = cell_areas(grid)
    assert (areas > 0).all()
    assert areas.sum() == pytest.approx(96.0)
    assert grid.attributes.shape == (len(grid.cells), 1)
    assert set(np.unique(grid.attributes)) == {1.0, 2.0, 3.0}
    vertices = grid.disv_vertices()
    cell2d = grid.disv_cell2d()
    assert vertices[0] == [0, *grid.vertices[0].tolist()]
    assert cell2d[0][:4] == [0, *grid.centers[0].tolist(), len(grid.cells[0])]
    assert cell2d[0][4:] == grid.cells[0].tolist()
    obtuse = tu.Mesh(
        np.array([(0.0, 0.0), (2.0, 0.0), (1.0, 0.2)]), np.array([(0, 1, 2)])
    )
    with pytest.raises(tu.Error, match="over 90"):
        tu.voronoi(obtuse)


def test_triangle_grid():
    mesh = tu.generate(square_geometry(), maximum_area=0.5, minimum_angle=25)
    for center in ("centroid", "circumcenter"):
        grid = tu.triangle_grid(mesh, center=center)
        assert len(grid.cells) == len(mesh.triangles)
        assert cell_areas(grid).sum() == pytest.approx(96.0)
        np.testing.assert_array_equal(grid.attributes, mesh.attributes)
    centroids = mesh.vertices[mesh.triangles].mean(axis=1)
    np.testing.assert_allclose(tu.triangle_grid(mesh).centers, centroids)
    with pytest.raises(ValueError, match="center must be"):
        tu.triangle_grid(mesh, center="other")


def test_cells():
    mesh = tu.generate(
        square_geometry(), maximum_area=0.5, minimum_angle=25, conforming_delaunay=True
    )
    grid = tu.voronoi(mesh)
    river = [(0, 3), (5, 4), (10, 3)]
    along = tu.cells_along_line(grid, river)
    assert along.lengths.sum() == pytest.approx(2 * np.hypot(5, 1))
    np.testing.assert_allclose(along.starts[1:], (along.starts + along.lengths)[:-1])
    assert (along.cells[1:] != along.cells[:-1]).all()
    # the outer boundary (40) and the hole (8) both have marker 1
    boundary = tu.cells_on_segments(grid, mesh, 1)
    assert boundary.lengths.sum() == pytest.approx(48.0)
    assert len(np.unique(boundary.cells)) == len(boundary.cells)
    bad = tu.Grid(grid.vertices, [np.array([0, 1])], grid.centers[:1], None, None)
    with pytest.raises(tu.Error, match="cell 0 has 2 corners"):
        tu.cells_along_line(bad, river)
    # the ring's center is a vertex of the mesh, so in its own cell
    center = mesh.vertices[mesh.fixed_vertices[0]]
    cells = tu.cells_at_points(grid, [center, (20.0, 20.0)])
    np.testing.assert_array_equal(grid.centers[cells[0]], center)
    assert cells[1] == -1
    centerless = tu.Grid(grid.vertices, grid.cells, grid.centers[:1], None, None)
    with pytest.raises(ValueError, match="centers has 1 rows"):
        tu.cells_along_line(centerless, river)


def test_connections():
    mesh = tu.generate(
        square_geometry(), maximum_area=0.5, minimum_angle=25, conforming_delaunay=True
    )
    grid = tu.voronoi(mesh)
    connections = tu.connect_cells(grid)
    assert len(connections.offsets) == len(grid.cells) + 1
    cell = np.repeat(np.arange(len(grid.cells)), np.diff(connections.offsets))
    other = connections.cells
    pairs = dict(zip(zip(cell.tolist(), other.tolist(), strict=True), range(len(cell))))
    back = np.array([pairs[(m, n)] for n, m in zip(cell, other, strict=True)])
    np.testing.assert_allclose(connections.widths[back], connections.widths)
    np.testing.assert_allclose(
        (connections.angles[back] - connections.angles) % 360.0, 180.0
    )
    assert ((connections.angles >= 0.0) & (connections.angles < 360.0)).all()
    for start, end in itertools.pairwise(connections.offsets):
        assert (np.diff(connections.cells[start:end]) > 0).all()
    gap = np.hypot(*(grid.centers[other] - grid.centers[cell]).T)
    np.testing.assert_allclose(connections.distances, 0.5 * gap)
    # a cell whose every side is a face is the triangles between its center
    # and its faces
    interior = np.flatnonzero(
        np.diff(connections.offsets) == [len(corners) for corners in grid.cells]
    )
    assert len(interior) > 100
    halves = np.bincount(cell, weights=0.5 * connections.widths * connections.distances)
    np.testing.assert_allclose(halves[interior], cell_areas(grid)[interior])
    obtuse = tu.Mesh(
        np.array([(0.0, 0.0), (4.0, 0.0), (2.0, 2.0 / np.sqrt(3.0)), (2.0, -5.0)]),
        np.array([(0, 1, 2), (1, 0, 3)]),
    )
    tu.connect_cells(tu.triangle_grid(obtuse))
    with pytest.raises(tu.Error, match="the center of cell 0 is not on its side"):
        tu.connect_cells(tu.triangle_grid(obtuse, center="circumcenter"))
    empty = tu.connect_cells(
        tu.Grid(np.zeros((0, 2)), [], np.zeros((0, 2)), None, None)
    )
    np.testing.assert_array_equal(empty.offsets, [0])
    assert len(empty.cells) == 0


def test_layers():
    mesh = tu.generate(
        square_geometry(), maximum_area=0.5, minimum_angle=25, conforming_delaunay=True
    )
    grid = tu.voronoi(mesh)
    cell_count = len(grid.cells)
    x = grid.centers[:, 0]
    top = tu.values_at_centers(grid, mesh, 20.0 - mesh.vertices[:, 0])
    np.testing.assert_allclose(top, 20.0 - x)
    layers = tu.layer_grid(grid, top, [np.full(cell_count, 5.0), 3.0, 0.0])
    assert layers.botm.shape == (3, cell_count)
    np.testing.assert_array_equal(layers.botm[1], 3.0)
    np.testing.assert_array_equal(layers.idomain, 1)
    np.testing.assert_array_equal(layers.top, top)
    # the second layer is thinner than 0.5 west of x = 5
    thin = tu.layer_grid(
        grid, top, np.vstack([np.full(cell_count, 5.0), 5.0 - 0.1 * x]), [1, 1], 0.5
    )
    np.testing.assert_array_equal(thin.idomain[1] == -1, 0.1 * x < 0.5)
    assert (thin.idomain[1] == -1).any() and (thin.idomain[1] == 1).any()
    np.testing.assert_array_equal(thin.idomain[0], 1)

    # the DISU nodes are the active cells, layer by layer
    d = thin.disu
    active = np.argwhere(thin.idomain > 0)
    np.testing.assert_array_equal(np.column_stack([d.layers, d.cells]), active)
    np.testing.assert_array_equal(
        thin.nodes[d.layers, d.cells], np.arange(len(d.cells))
    )
    assert (thin.nodes[thin.idomain <= 0] == -1).all()
    np.testing.assert_allclose(d.area, cell_areas(grid)[d.cells])
    np.testing.assert_array_equal(d.top, np.where(d.layers == 0, top[d.cells], 5.0))
    assert d.iac.sum() == len(d.ja)
    starts = np.concatenate([[0], np.cumsum(d.iac)[:-1]])
    np.testing.assert_array_equal(d.ja[starts], np.arange(len(d.cells)))
    node = np.repeat(np.arange(len(d.cells)), d.iac)
    vertical = (d.ihc == 0) & (node != d.ja)
    np.testing.assert_allclose(d.hwva[vertical], d.area[node[vertical]])
    np.testing.assert_allclose(d.cl12[vertical], 0.5 * (d.top - d.bot)[node[vertical]])
    # the first layer's cells above pass-through cells connect to nothing
    # below
    assert set(d.ja[vertical].tolist()) == set(
        thin.nodes[0, thin.idomain[1] > 0].tolist()
    ) | set(thin.nodes[1, thin.idomain[1] > 0].tolist())

    disv = thin.disv_arguments()
    assert (disv["nlay"], disv["ncpl"]) == (2, cell_count)
    assert disv["cell2d"] == grid.disv_cell2d()
    disu = thin.disu_arguments()
    assert (disu["nodes"], disu["nja"]) == (len(d.cells), len(d.ja))
    assert disu["cell2d"][0][:3] == [0, *grid.centers[d.cells[0]].tolist()]
    assert len(disu["cell2d"]) == len(d.cells)

    # numbers and values for each layer
    layers = tu.layer_grid(grid, 10.0, [6.0, 2.0], idomain=[1, 0])
    assert layers.idomain.tolist() == [[1] * cell_count, [0] * cell_count]
    assert len(layers.disu.cells) == cell_count
    assert (
        layers.disu.ihc[
            layers.disu.ja != np.repeat(np.arange(cell_count), layers.disu.iac)
        ]
        == 1
    ).all()
    one = tu.layer_grid(grid, 1.0, 0.0)
    assert one.botm.shape == (1, cell_count)

    with pytest.raises(tu.Error, match="in layer 1 has a top of 6 and a bottom of 7"):
        tu.layer_grid(grid, 10.0, [6.0, 7.0])
    # a crossing bottom made pass-through moves up, and the layer below starts
    # at the bottom above
    crossing = tu.layer_grid(grid, 10.0, [6.0, 7.0, 0.0], minimum_thickness=0.5)
    np.testing.assert_array_equal(crossing.idomain[1], -1)
    np.testing.assert_array_equal(crossing.botm[1], 6.0)
    np.testing.assert_array_equal(crossing.disu.top[crossing.disu.layers == 2], 6.0)
    with pytest.raises(tu.Error, match="is not active but has a bottom of 7"):
        tu.layer_grid(grid, 10.0, [6.0, 7.0, 0.0], idomain=[1, 0, 1])
    with pytest.raises(ValueError, match="botm must be"):
        tu.layer_grid(grid, 10.0, np.zeros((2, 3)))
    with pytest.raises(ValueError, match=r"botm must be .* not the shapes"):
        tu.layer_grid(grid, 10.0, [np.zeros(cell_count), np.zeros(cell_count + 1)])
    with pytest.raises(ValueError, match="at least one layer"):
        tu.layer_grid(grid, 10.0, [])
    with pytest.raises(ValueError, match="top must be"):
        tu.layer_grid(grid, [10.0, 9.0], 0.0)
    with pytest.raises(ValueError, match="idomain must be"):
        tu.layer_grid(grid, 10.0, [5.0, 0.0], idomain=[1, 1, 1])
    with pytest.raises(ValueError, match="idomain must be whole"):
        tu.layer_grid(grid, 10.0, [5.0, 0.0], idomain=0.5)
    with pytest.raises(tu.Error, match="minimum thickness"):
        tu.layer_grid(grid, 10.0, 0.0, minimum_thickness=-1.0)
    with pytest.raises(tu.Error, match="vertex count is 2"):
        tu.values_at_centers(grid, mesh, [1.0, 2.0])


def test_shapes():
    mesh = tu.generate(square_geometry())
    short = tu.Mesh(mesh.vertices, mesh.triangles, area_limits=np.ones(3))
    with pytest.raises(ValueError, match="area_limits has 3 rows"):
        tu.refine(short)
    flat = tu.Mesh(mesh.vertices.reshape(-1), mesh.triangles)
    with pytest.raises(ValueError, match="vertices must have 2 columns"):
        tu.refine(flat)
    markers = tu.Mesh(
        mesh.vertices, mesh.triangles, segments=mesh.segments, segment_markers=[1]
    )
    with pytest.raises(ValueError, match="segment_markers has 1 rows"):
        tu.refine(markers)
    fractional = tu.Mesh(mesh.vertices, mesh.triangles + 0.5)
    with pytest.raises(ValueError, match="triangles must be whole numbers"):
        tu.refine(fractional)
    unsegmented = tu.Mesh(mesh.vertices, mesh.triangles, segment_markers=[1])
    with pytest.raises(ValueError, match="without segments"):
        tu.refine(unsegmented)
    wrapped = tu.Mesh(mesh.vertices, mesh.triangles.astype(np.int64) + 2**32)
    with pytest.raises(ValueError, match="triangles must be from"):
        tu.refine(wrapped)
    with pytest.raises(ValueError, match="maximum_angle_splits must be"):
        tu.refine(mesh, maximum_angle_splits=2**40)
    geometry = tu.Geometry()
    with pytest.raises(ValueError, match="points must have 2 columns"):
        geometry.add_polygon([0, 0, 1])
    with pytest.raises(ValueError, match="point_count must be"):
        geometry.add_ring(1, 1, 0.5, 2**40)


def test_points_are_copied():
    # a list changed after it is added does not change the mesh
    square = [[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0]]
    geometry = tu.Geometry()
    geometry.add_polygon(square)
    square[2] = [5.0, 5.0]
    assert tu.generate(geometry).triangle_areas().sum() == pytest.approx(1.0)


def test_error():
    geometry = tu.Geometry()
    geometry.add_points([(0, 0), (1, 0)])
    with pytest.raises(tu.Error, match="at least three input vertices"):
        tu.generate(geometry)
