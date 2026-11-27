"""
Tests the user guide's example, doc/UserGuide/valley.py and valley_gis.py.

It runs from geometry to grid, connections, features, and layers, as a
user of the package would, and runs against each wheel.

Cases:
  - test_example : the valley's Voronoi grid matches its DISV arrays, its
                   connections are symmetric, each stream's cells run in
                   order to the confluence's cell, one of the river's, and
                   its layers make the clay's thin cells pass-through and
                   leave them out of the DISU nodes; a raised clay base
                   makes more pass-through, and the first restores them.
  - test_gis     : the valley's geometry read from shapefiles meshes to the
                   same mesh as its coordinates (skipped without geopandas).
"""

import runpy
from pathlib import Path

import numpy as np
import pytest

REPOSITORY = Path(__file__).resolve().parents[2]
GUIDE = REPOSITORY / "doc" / "UserGuide"


def length(points, closed=False):
    """The length of a line, or of a polygon's boundary if closed."""
    xy = np.array(points, dtype=float)
    if closed:
        xy = np.vstack([xy, xy[:1]])
    return np.hypot(*np.diff(xy, axis=0).T).sum()


def test_example():
    valley = runpy.run_path(str(REPOSITORY / "doc" / "UserGuide" / "valley.py"))
    grid = valley["grid"]
    assert len(grid.cells) > 1000
    assert len(valley["disv_vertices"]) == len(grid.vertices)
    assert len(valley["disv_cell2d"]) == len(grid.cells)
    connections = valley["connections"]
    cell = np.repeat(np.arange(len(grid.cells)), np.diff(connections.offsets))
    pairs = set(zip(cell.tolist(), connections.cells.tolist(), strict=True))
    assert all((m, n) in pairs for n, m in pairs)
    assert (connections.distances > 0).all()
    river, tributary = valley["stream_cells"]
    for cells, stream in zip(valley["stream_cells"], valley["STREAMS"], strict=True):
        assert cells.lengths.sum() == pytest.approx(length(stream))
        # in order: from the cell centered on the first point to the one on
        # the last, each next to the one before
        np.testing.assert_allclose(
            grid.centers[cells.cells[[0, -1]]], [stream[0], stream[-1]]
        )
        for here, there in zip(cells.cells[:-1], cells.cells[1:], strict=True):
            start, end = connections.offsets[here : here + 2]
            assert there in connections.cells[start:end]
    # the tributary ends in the cell centered on the point it shares with the
    # river, which is one of the river's
    confluence = tributary.cells[-1]
    np.testing.assert_allclose(grid.centers[confluence], valley["TRIBUTARY"][-1])
    assert confluence in river.cells
    boundary = valley["boundary_cells"]
    assert boundary.lengths.sum() == pytest.approx(
        length(valley["BOUNDARY"], closed=True)
    )
    np.testing.assert_allclose(grid.centers[valley["well_cells"]], valley["WELLS"])
    layers = valley["layers"]
    x = grid.centers[:, 0]
    clay = layers.botm[0] - layers.botm[1]
    np.testing.assert_array_equal(
        layers.idomain[1] == -1, clay < valley["MINIMUM_THICKNESS"]
    )
    assert (x[layers.idomain[1] == -1] > 2000.0).all()
    np.testing.assert_array_equal(layers.idomain[[0, 2]], 1)
    disu = valley["disu"]
    assert disu["nodes"] == (layers.idomain > 0).sum()
    assert valley["disv"]["nlay"] == 3
    # the raised clay base makes more cells pass-through, and the first
    # elevations restore them
    revised = valley["revised"]
    assert (revised.idomain < 0).sum() > (layers.idomain < 0).sum()
    assert len(revised.disu.cells) == (revised.idomain > 0).sum()
    restored = revised.with_elevations(layers.top, layers.botm)
    np.testing.assert_array_equal(restored.idomain, layers.idomain)


def test_gis(monkeypatch):
    pytest.importorskip("geopandas")
    # valley_gis.py imports valley.py from beside it
    monkeypatch.syspath_prepend(str(GUIDE))
    gis = runpy.run_path(str(GUIDE / "valley_gis.py"))
    valley = runpy.run_path(str(GUIDE / "valley.py"))
    mesh, expected = gis["mesh"], valley["mesh"]
    np.testing.assert_array_equal(mesh.vertices, expected.vertices)
    np.testing.assert_array_equal(mesh.triangles, expected.triangles)
    np.testing.assert_array_equal(mesh.attributes, expected.attributes)
    np.testing.assert_array_equal(mesh.segment_markers, expected.segment_markers)
