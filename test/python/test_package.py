"""
Tests the files of the Python package.

The wheel's tests set TRIANGLE_UTILS_TEST_INSTALLED, so the installed
package is imported rather than the module in the repository.

Cases:
  - test_triangle_license : LICENSE-Triangle is the license of the Triangle
                            subproject that triangle.wrap names (skipped
                            before the subproject is downloaded, a failure
                            when testing the installed package).
  - test_notice           : NOTICE gives the source of the Triangle that
                            triangle.wrap names.
  - test_example          : the user guide's example, valley.py, meshes the
                            valley and builds its Voronoi grid, whose DISV
                            arrays match it, whose connections are
                            symmetric, and whose river and boundary cells
                            sum to their lengths, with each well's cell
                            centered on the well.
  - test_installed        : the installed package has the licenses, the
                            notice, and every module file beside the module,
                            loads the library beside it, and reports the
                            package's version (skipped unless testing the
                            installed package).
"""

import os
import runpy
import sys
from importlib import metadata
from pathlib import Path

import numpy as np
import pytest
import triangle_utils

REPOSITORY = Path(__file__).resolve().parents[2]


def wrap_value(key):
    """A value of the Triangle subproject's wrap file."""
    wrap = (REPOSITORY / "subprojects" / "triangle.wrap").read_text()
    return next(
        line.split("=", 1)[1].strip()
        for line in wrap.splitlines()
        if line.split("=")[0].strip() == key
    )


def test_triangle_license():
    license_file = REPOSITORY / "subprojects" / wrap_value("directory") / "LICENSE"
    if not license_file.exists():
        # the wheel is built from the subproject, so it must be there
        if os.environ.get("TRIANGLE_UTILS_TEST_INSTALLED"):
            pytest.fail("the Triangle subproject the wheel was built from is missing")
        pytest.skip("the Triangle subproject has not been downloaded")
    assert (REPOSITORY / "LICENSE-Triangle").read_text() == license_file.read_text()


def test_notice():
    assert wrap_value("source_url") in (REPOSITORY / "NOTICE").read_text()


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
    river = valley["river_cells"]
    assert river.lengths.sum() == pytest.approx(length(valley["RIVER"]))
    boundary = valley["boundary_cells"]
    assert boundary.lengths.sum() == pytest.approx(
        length(valley["BOUNDARY"], closed=True)
    )
    np.testing.assert_allclose(grid.centers[valley["well_cells"]], valley["WELLS"])


@pytest.mark.skipif(
    not os.environ.get("TRIANGLE_UTILS_TEST_INSTALLED"),
    reason="not testing the installed package",
)
def test_installed():
    package = Path(triangle_utils.__file__).parent
    assert package != REPOSITORY / "python" / "triangle_utils"
    assert "CC0 1.0" in (package / "LICENSE").read_text()
    assert "Shewchuk" in (package / "LICENSE-Triangle").read_text()
    assert (package / "NOTICE").read_text() == (REPOSITORY / "NOTICE").read_text()
    modules = sorted(
        p.name for p in (REPOSITORY / "python" / "triangle_utils").glob("*.py")
    )
    assert sorted(p.name for p in package.glob("*.py")) == modules
    loaded = sys.modules["triangle_utils._library"].library_path()
    assert loaded.parent == package
    assert triangle_utils.version()[0] == metadata.version("triangle-utils")
