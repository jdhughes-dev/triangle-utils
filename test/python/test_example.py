"""
Tests the user guide's example, doc/UserGuide/valley.py.

It runs from geometry to grid, connections, and features, as a user of the
package would, and runs against each wheel.

Cases:
  - test_example : the valley's Voronoi grid matches its DISV arrays, its
                   connections are symmetric, and each stream's cells run in
                   order to the confluence's cell, one of the river's.
"""

import runpy
from pathlib import Path

import numpy as np
import pytest

REPOSITORY = Path(__file__).resolve().parents[2]


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
