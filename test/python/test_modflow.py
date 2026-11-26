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
  - test_disu_runs     : the Voronoi grid in DISU, with its connections, gives
                         the linear head and the uniform specific discharge,
                         as in DISV.
  - test_layers_run    : three layers of the Voronoi grid, with pass-through,
                         thin, and inactive cells, give the same heads in
                         DISV and DISU.
"""

import itertools
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


def run_model(workspace, vertices, cell2d, disv_file=None, connections=None):
    """The heads and specific discharge of a steady model with the grid and
    fixed heads at the ends, with its DISV file replaced by disv_file if
    given, or in DISU with the connections if given."""
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
    model = flopy.mf6.ModflowGwf(simulation, modelname="grid", save_flows=True)
    if connections is None:
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
    else:
        add_disu(model, vertices, cell2d, connections)
    flopy.mf6.ModflowGwfnpf(model, k=1.0, save_specific_discharge=True)
    flopy.mf6.ModflowGwfic(model, strt=0.5)
    # DISU cells have no layer
    layer = () if connections is not None else (0,)
    fixed = [
        [(*layer, cell), WEST_HEAD] for cell in np.flatnonzero(centers[:, 0] == 0.0)
    ]
    fixed += [
        [(*layer, cell), EAST_HEAD] for cell in np.flatnonzero(centers[:, 0] == LENGTH)
    ]
    flopy.mf6.ModflowGwfchd(model, stress_period_data=fixed)
    flopy.mf6.ModflowGwfoc(
        model,
        head_filerecord="grid.hds",
        budget_filerecord="grid.cbc",
        saverecord=[("HEAD", "ALL"), ("BUDGET", "ALL")],
    )
    simulation.write_simulation(silent=True)
    if disv_file is not None:
        shutil.copy(disv_file, workspace / "grid.disv")
    success, _ = simulation.run_simulation(silent=True)
    assert success
    discharge = model.output.budget().get_data(text="DATA-SPDIS")[0]
    return (
        centers,
        model.output.head().get_data().ravel(),
        np.column_stack([discharge["qx"], discharge["qy"]]),
    )


def add_disu(model, vertices, cell2d, connections):
    """A one-layer DISU package with the grid's horizontal connections; each
    cell's row starts with the cell itself."""
    xy = np.array([vertex[1:] for vertex in vertices])
    corners = [cell[4:] for cell in cell2d]
    area = [
        0.5 * abs(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1)))
        for x, y in (xy[cell].T for cell in corners)
    ]
    iac, ja, cl12, hwva, angldegx = [], [], [], [], []
    for cell, (start, end) in enumerate(itertools.pairwise(connections.offsets)):
        iac.append(end - start + 1)
        ja += [cell, *connections.cells[start:end]]
        cl12 += [0.0, *connections.distances[start:end]]
        hwva += [0.0, *connections.widths[start:end]]
        angldegx += [0.0, *connections.angles[start:end]]
    flopy.mf6.ModflowGwfdisu(
        model,
        nodes=len(cell2d),
        nja=len(ja),
        nvert=len(vertices),
        top=1.0,
        bot=0.0,
        area=area,
        iac=iac,
        ja=ja,
        ihc=[1] * len(ja),
        cl12=cl12,
        hwva=hwva,
        angldegx=angldegx,
        vertices=vertices,
        cell2d=cell2d,
    )


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
    centers, heads, _ = run_model(tmp_path, grid.disv_vertices(), grid.disv_cell2d())
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
    centers, heads, _ = run_model(
        workspace, vertices, cell2d, disv_file=f"{base}.1.disv"
    )
    check_linear(centers, heads)


def test_disu_runs(tmp_path):
    geometry = tu.Geometry()
    geometry.add_polygon([(0, 0), (LENGTH, 0), (LENGTH, WIDTH), (0, WIDTH)], marker=1)
    mesh = tu.generate(
        geometry, maximum_area=0.2, minimum_angle=28, conforming_delaunay=True
    )
    grid = tu.voronoi(mesh)
    arguments = (grid.disv_vertices(), grid.disv_cell2d())
    centers, heads, discharge = run_model(
        tmp_path / "disu", *arguments, connections=tu.connect_cells(grid)
    )
    check_linear(centers, heads)
    _, disv_heads, disv_discharge = run_model(tmp_path / "disv", *arguments)
    np.testing.assert_allclose(heads, disv_heads, atol=1e-8)
    # the exact specific discharge is uniform, along x
    exact = [(WEST_HEAD - EAST_HEAD) / LENGTH, 0.0]
    np.testing.assert_allclose(discharge, np.tile(exact, (len(heads), 1)), atol=1e-7)
    np.testing.assert_allclose(disv_discharge, discharge, atol=1e-7)


def run_layers(workspace, layers, disu):
    """The heads of a steady model of the layers, in DISV or DISU, with a
    fixed head at the west end of the top layer and the east end of the
    bottom layer, as an array of layer by cell (nan where there is no node)."""
    simulation = flopy.mf6.MFSimulation(sim_ws=workspace, exe_name="mf6")
    flopy.mf6.ModflowTdis(simulation)
    flopy.mf6.ModflowIms(
        simulation,
        outer_dvclose=1e-12,
        inner_dvclose=1e-12,
        rcloserecord=[1e-10, "strict"],
        outer_maximum=50,
        inner_maximum=2000,
        linear_acceleration="cg",
    )
    model = flopy.mf6.ModflowGwf(simulation, modelname="layers")
    if disu:
        flopy.mf6.ModflowGwfdisu(model, **layers.disu_arguments())
    else:
        flopy.mf6.ModflowGwfdisv(model, **layers.disv_arguments())
    flopy.mf6.ModflowGwfnpf(model, k=1.0, k33=0.1)
    flopy.mf6.ModflowGwfic(model, strt=0.5)
    x = layers.grid.centers[:, 0]
    bottom = len(layers.botm) - 1
    fixed = []
    for layer, cells, head in (
        (0, np.flatnonzero(x == 0.0), WEST_HEAD),
        (bottom, np.flatnonzero(x == LENGTH), EAST_HEAD),
    ):
        for cell in cells:
            cellid = (layers.nodes[layer, cell],) if disu else (layer, cell)
            fixed.append([cellid, head])
    flopy.mf6.ModflowGwfchd(model, stress_period_data=fixed)
    flopy.mf6.ModflowGwfoc(
        model, head_filerecord="layers.hds", saverecord=[("HEAD", "ALL")]
    )
    simulation.write_simulation(silent=True)
    success, _ = simulation.run_simulation(silent=True)
    assert success
    heads = model.output.head().get_data().ravel()
    if disu:
        result = np.full(layers.nodes.shape, np.nan)
        result[layers.disu.layers, layers.disu.cells] = heads
        return result
    heads = heads.reshape(layers.nodes.shape)
    return np.where(layers.nodes >= 0, heads, np.nan)


def test_layers_run(tmp_path):
    geometry = tu.Geometry()
    geometry.add_polygon([(0, 0), (LENGTH, 0), (LENGTH, WIDTH), (0, WIDTH)], marker=1)
    mesh = tu.generate(
        geometry, maximum_area=0.2, minimum_angle=28, conforming_delaunay=True
    )
    grid = tu.voronoi(mesh)
    x, y = grid.centers.T
    # a top sloping to the east, from the mesh's vertices, and a middle
    # layer that thins to nothing in a band and is cut out of a patch
    top = tu.values_at_centers(grid, mesh, 10.0 - 0.2 * mesh.vertices[:, 0])
    middle = np.where(np.abs(x - 5.0) < 1.0, 5.0 - 0.01, 3.0)
    botm = np.array([np.full_like(x, 5.0), middle, np.full_like(x, 0.0)])
    idomain = np.ones(botm.shape, dtype=int)
    idomain[1, (x > 7.0) & (y > 2.0)] = 0
    layers = tu.layer_grid(grid, top, botm, idomain, minimum_thickness=0.1)
    assert (layers.idomain[1] == -1).sum() > 0
    assert (layers.idomain == 0).sum() == (idomain == 0).sum()
    disv = run_layers(tmp_path / "disv", layers, disu=False)
    disu = run_layers(tmp_path / "disu", layers, disu=True)
    np.testing.assert_array_equal(np.isnan(disv), np.isnan(disu))
    np.testing.assert_allclose(disu, disv, atol=1e-8)
    # flow crosses the thin band only through its pass-through cells
    assert np.nanmax(disu) - np.nanmin(disu) > 0.5
