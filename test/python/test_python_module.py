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
  - test_shapes        : arrays of the wrong shape, length, or range, and
                         values too large for an int, raise ValueError before
                         the library reads them.
  - test_points_are_copied : changing a list after adding it does not change
                         the mesh.
  - test_version       : the versions of triangle-utils, Triangle, and its
                         release are returned.
  - test_error         : the library's reason is raised as Error.
"""

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
