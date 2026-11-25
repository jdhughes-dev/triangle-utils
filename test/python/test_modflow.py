"""
Tests that MODFLOW 6 runs the grids, when mf6 is installed (pixi run get-mf6).

Flow between fixed heads at the two ends of a rectangle has a head linear in
x, which a grid whose connections are perpendicular to their faces gives
exactly.

Cases:
  - test_voronoi_grid_runs : the Python module's Voronoi grid, in FloPy's DISV
                         package, gives the linear head.
  - test_disv_file_runs : the DISV file the disv command writes, run in place
                         of FloPy's, gives the linear head.
"""

import os
import shutil
import subprocess

import numpy as np
import pytest
import triangle_utils as tu

flopy = pytest.importorskip("flopy")
pytestmark = pytest.mark.skipif(
    shutil.which("mf6") is None, reason="mf6 is not installed"
)

LENGTH, WIDTH = 10.0, 4.0
# the heads at the west and east ends
WEST_HEAD, EAST_HEAD = 1.0, 0.0


def run_model(workspace, vertices, cell2d, disv_file=None):
    """The heads of a steady model with the grid and fixed heads at the ends,
    with its DISV file replaced by disv_file if given."""
    centers = np.array([cell[1:3] for cell in cell2d])
    simulation = flopy.mf6.MFSimulation(sim_ws=workspace, exe_name="mf6")
    flopy.mf6.ModflowTdis(simulation)
    flopy.mf6.ModflowIms(
        simulation,
        outer_dvclose=1e-12,
        inner_dvclose=1e-12,
        rcloserecord=[1e-10, "strict"],
        inner_maximum=500,
        linear_acceleration="bicgstab",
    )
    model = flopy.mf6.ModflowGwf(simulation, modelname="grid")
    flopy.mf6.ModflowGwfdisv(
        model,
        nlay=1,
        ncpl=len(cell2d),
        nvert=len(vertices),
        top=1.0,
        botm=0.0,
        vertices=vertices,
        cell2d=cell2d,
    )
    flopy.mf6.ModflowGwfnpf(model, k=1.0)
    flopy.mf6.ModflowGwfic(model, strt=0.5)
    fixed = [[(0, cell), WEST_HEAD] for cell in np.flatnonzero(centers[:, 0] == 0.0)]
    fixed += [
        [(0, cell), EAST_HEAD] for cell in np.flatnonzero(centers[:, 0] == LENGTH)
    ]
    flopy.mf6.ModflowGwfchd(model, stress_period_data=fixed)
    flopy.mf6.ModflowGwfoc(
        model, head_filerecord="grid.hds", saverecord=[("HEAD", "ALL")]
    )
    simulation.write_simulation(silent=True)
    if disv_file is not None:
        shutil.copy(disv_file, workspace / "grid.disv")
    success, _ = simulation.run_simulation(silent=True)
    assert success
    return centers, model.output.head().get_data().ravel()


def check_linear(centers, heads):
    expected = WEST_HEAD + (EAST_HEAD - WEST_HEAD) * centers[:, 0] / LENGTH
    np.testing.assert_allclose(heads, expected, atol=1e-8)


def test_voronoi_grid_runs(tmp_path):
    geometry = tu.Geometry()
    geometry.add_polygon([(0, 0), (LENGTH, 0), (LENGTH, WIDTH), (0, WIDTH)], marker=1)
    mesh = tu.generate(
        geometry, maximum_area=0.2, minimum_angle=28, conforming_delaunay=True
    )
    grid = tu.voronoi(mesh)
    centers, heads = run_model(tmp_path, grid.disv_vertices(), grid.disv_cell2d())
    check_linear(centers, heads)


def test_disv_file_runs(tmp_path):
    (tmp_path / "box.poly").write_text(
        f"4 2 0 0\n1 0 0\n2 {LENGTH} 0\n3 {LENGTH} {WIDTH}\n4 0 {WIDTH}\n"
        "4 0\n1 1 2\n2 2 3\n3 3 4\n4 4 1\n0\n"
    )
    program = os.environ["TRIANGLE_UTILS_PROGRAM"]
    base = str(tmp_path / "box")
    for arguments in (
        ("generate", "-a", "0.2", "-q", "28", "-D", base),
        ("disv", f"{base}.1"),
    ):
        subprocess.run([program, *arguments], check=True, capture_output=True)
    # the grid as the file gives it, for FloPy, whose own DISV file is replaced
    # by the program's
    vertices, cell2d, block = [], [], None
    for words in (line.split() for line in open(f"{base}.1.disv")):
        if words[:1] == ["BEGIN"] or words[:1] == ["END"]:
            block = words[1] if words[0] == "BEGIN" else None
        elif block == "VERTICES":
            vertices.append([int(words[0]) - 1, float(words[1]), float(words[2])])
        elif block == "CELL2D":
            corners = [int(word) - 1 for word in words[4:]]
            cell2d.append(
                [int(words[0]) - 1, float(words[1]), float(words[2]), len(corners)]
            )
            cell2d[-1] += corners
    workspace = tmp_path / "model"
    centers, heads = run_model(workspace, vertices, cell2d, disv_file=f"{base}.1.disv")
    check_linear(centers, heads)
