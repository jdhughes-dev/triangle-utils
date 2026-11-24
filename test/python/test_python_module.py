"""
Tests the Python module over the C API.

Cases:
  - test_generate      : a square with a hole, a river, regions, and a well
                         meshes to the domain's area, with each region's
                         attribute and the well's area limit.
  - test_refine        : refining the mesh by area limits and a maximum angle
                         keeps its area and segments, and counts what is left.
  - test_shapes        : arrays of the wrong shape or length raise ValueError
                         before the library reads them.
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
    geometry = tu.Geometry()
    geometry.add_polygon([0, 0, 1])
    with pytest.raises(ValueError, match="points must have 2 columns"):
        tu.generate(geometry)


def test_error():
    geometry = tu.Geometry()
    geometry.add_points([(0, 0), (1, 0)])
    with pytest.raises(tu.Error, match="at least three input vertices"):
        tu.generate(geometry)
