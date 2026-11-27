"""
Tests the triangle-utils program.

The program is named by the TRIANGLE_UTILS_PROGRAM environment variable,
which meson test sets; each test runs it and checks its output and exit
status.

Cases:
  - test_help          : -h and --help print the usage and exit 0.
  - test_version       : --version prints the triangle-utils and Triangle
                         versions and exits 0.
  - test_license       : --license prints CC0, Triangle's terms, and
                         shapelib's MIT license, and exits 0.
  - test_usage_errors  : no arguments, an unknown option or command,
                         arguments after --version or --license, and check
                         without one base or with an unknown option exit 2
                         with a message.
  - test_check_help    : check -h and --help print the command's usage and
                         exit 0.
  - test_check         : a valid mesh, with and without neighbors, exits 0.
  - test_check_numbered_from_zero : a mesh numbered from 0, with comments and
                         CRLF line ends, exits 0.
  - test_check_errors  : a clockwise triangle, a wrong neighbor, a corner out
                         of range, a neighbor file for another mesh, no
                         triangles, and a bad line exit 1 with the reason.
  - test_check_missing_file : a missing file exits 1 with the reason.
  - test_generate      : a closed box with a square hole, given in a .poly
                         file numbered from 1, meshes to its area within the
                         maximum area, and check accepts the mesh.
  - test_generate_options : -s, -D, -m, and -o mesh to the same area, -s adds
                         points, and a .poly without vertices, numbered from
                         0, takes them from the .node file.
  - test_refine        : a generated mesh refines by an .area file and a
                         maximum area to x.2 within the limits, keeping its
                         area, and reports angles over a maximum angle.
  - test_improve_keeps_points : generate, refine, and improve keep a point
                         feature of the .poly file in place, through the
                         marker each writes.
  - test_improve_command : a refined mesh improves toward 90 degrees to x.3,
                         keeping its area and a vertex marked in the .node
                         file, and reports angles over 90 degrees.
  - test_disv          : a conforming Delaunay mesh of the box writes a
                         Voronoi DISV grid with a cell per vertex covering
                         the box, and a .cells file; the triangle grids have
                         a cell per triangle.
  - test_disv_cells    : a mesh numbered from 0 with triangle attributes writes
                         each cell's vertex or triangle from 0 and its
                         attributes to the .cells file.
  - test_disv_ignores_poly : disv reads only the .node and .ele files, so a
                         .poly file with its own vertices beside them does
                         not stop it.
  - test_disv_errors   : a mesh that is not Delaunay, and a top below the
                         bottom, exit 1 with the reason.
  - test_disv_layers   : three layers, with a top from a file of the mesh's
                         vertices, a bottom and idomain from files of the
                         cells, and a minimum thickness, write their arrays,
                         with pass-through and inactive cells, to the DISV file.
  - test_disu          : the same layers write a MODFLOW 6 DISU file with a
                         node for each active cell, each row starting with the
                         node, a .nodes file of each node's layer and cell,
                         and a MODFLOW-USG DISU file.
  - test_disu_errors   : a file with the wrong number of values or a word, an
                         idomain that is not a whole number, and a MODFLOW-USG
                         layer with no active cells exit 1 with the reason.
  - test_cells         : a line across the box and the box's boundary print
                         their cells, numbered from 1, whose lengths sum to
                         the line's length in the box and to the boundary's,
                         and points print their cells, 0 outside the box.
  - test_cells_errors  : a bad line file and a marker no segment has exit 1
                         with the reason.
  - test_generate_shapefiles : polygons with a hole, a line, regions, and
                         wells in shapefiles, with values from fields and
                         numbers for the next shapefile only, mesh as the
                         Python module meshes the same features; cells reads
                         a shapefile's lines and points; a missing field, a
                         marker that is not whole, and a shapefile with no
                         lines exit 1 (skipped without geopandas).
  - test_generate_errors : a bad option value is a usage error (2), and a
                         boundary not closed and a missing file are errors
                         (1).
  - test_guide_commands : the guide's commands, with valley.py's values, give
                         the valley's mesh, grid, cells, and layers from the
                         files valley_files.py writes.
"""

import itertools
import math
import os
import runpy
import subprocess
import sys
from pathlib import Path

import pytest

PROGRAM = os.environ.get("TRIANGLE_UTILS_PROGRAM", "")

# the unit square in two triangles, numbered from 1
SQUARE_NODE = "4 2 0 0\n1 0 0\n2 1 0\n3 1 1\n4 0 1\n"
SQUARE_ELE = "2 3 0\n1 1 2 3\n2 1 3 4\n"
SQUARE_NEIGH = "2 3\n1 -1 2 -1\n2 -1 -1 1\n"


def run(*arguments):
    """Run the program and return its exit status, output, and errors."""
    result = subprocess.run(
        [PROGRAM, *arguments], capture_output=True, text=True, check=False
    )
    return result.returncode, result.stdout, result.stderr


def write_mesh(directory, node=SQUARE_NODE, ele=SQUARE_ELE, neigh=None):
    """Write a mesh's files and return their base name."""
    base = directory / "square"
    # newline="" writes the line ends as given, on Windows too
    (directory / "square.node").write_text(node, newline="")
    (directory / "square.ele").write_text(ele, newline="")
    if neigh is not None:
        (directory / "square.neigh").write_text(neigh, newline="")
    return str(base)


@pytest.mark.parametrize("option", ["-h", "--help"])
def test_help(option):
    status, output, _ = run(option)
    assert status == 0
    assert output.startswith("usage: triangle-utils <command>")
    assert "check" in output


def test_version():
    status, output, _ = run("--version")
    assert status == 0
    lines = output.splitlines()
    assert lines[0].startswith("triangle-utils ")
    assert lines[1].startswith("Triangle 1.6 (MODFLOW-ORG/triangle 1.6.")


def test_license():
    status, output, _ = run("--license")
    assert status == 0
    assert "CC0 1.0" in output
    assert "Jonathan Richard Shewchuk" in output
    assert "commercial system" in output
    # shapelib's license, as LICENSE-shapelib has it
    shapelib = Path(__file__).resolve().parents[2] / "LICENSE-shapelib"
    assert shapelib.read_text().strip() in output


@pytest.mark.parametrize(
    "arguments, reason",
    [
        ((), "give a command or an option"),
        (("generate", "-a", "big", "box"), 'generate: -a needs a number, not "big"'),
        (("generate", "box", "-o"), "generate: -o needs a value"),
        (("generate",), "generate: give one <base>"),
        (("refine", "-n", "1.5", "box"), "refine: -n needs a whole number, not 1.5"),
        (("refine",), "refine: give one <base>"),
        (
            ("improve", "-s", "other", "box"),
            "improve: the smoothing must be laplacian or centroidal, not other",
        ),
        (
            ("refine", "--conforming-delaunay=yes", "box"),
            "refine: --conforming-delaunay takes no value",
        ),
        (
            ("generate", "-o", "box", "box"),
            "generate: the output would replace the input; give another -o",
        ),
        (("--bogus",), "unknown option --bogus"),
        (("bogus",), "unknown command bogus"),
        (("--version", "extra"), "--version takes no arguments"),
        (("--license", "extra"), "--license takes no arguments"),
        (
            ("disv", "-g", "other", "box"),
            "disv: the grid must be voronoi, centroid, or circumcenter, not other",
        ),
        (
            ("disv", "-i", "1", "-i", "1", "box"),
            "disv: give -i once for each layer, 1, not 2 times",
        ),
        (
            ("disv", "-m", "-1", "box"),
            "disv: the minimum thickness (-1) must not be negative",
        ),
        (("disv",), "disv: give one <base>"),
        (
            ("disu", "-f", "mf2005", "box"),
            "disu: the format must be mf6 or usg, not mf2005",
        ),
        (("disu", "-b", "0", "box", "box"), "disu: give one <base>"),
        (("cells", "box"), "cells: give one of -l, -p, and -m"),
        (("cells", "-l", "x", "-m", "1", "box"), "cells: give one of -l, -p, and -m"),
        (("cells", "-m", "1.5", "box"), "cells: -m needs a whole number, not 1.5"),
        (("check",), "check: give one <base>"),
        (("check", "one", "two"), "check: give one <base>"),
        (("check", "--bogus", "base"), "check: unknown option --bogus"),
    ],
)
def test_usage_errors(arguments, reason):
    status, output, errors = run(*arguments)
    assert status == 2
    assert output == ""
    assert errors.startswith(f"triangle-utils: {reason}\n")
    assert "triangle-utils -h" in errors


@pytest.mark.parametrize("option", ["-h", "--help"])
def test_check_help(option):
    status, output, _ = run("check", option)
    assert status == 0
    assert output.startswith("usage: triangle-utils check")


def test_check(tmp_path):
    base = write_mesh(tmp_path)
    status, output, _ = run("check", base)
    assert status == 0
    assert output == f"{base}: 4 vertices and 2 triangles; the mesh is valid\n"
    base = write_mesh(tmp_path, neigh=SQUARE_NEIGH)
    status, output, _ = run("check", base)
    assert status == 0
    assert "with neighbors" in output


def test_check_numbered_from_zero(tmp_path):
    node = (
        "# the unit square\r\n4 2 0 0\r\n0 0 0\r\n1 1 0  # corner\r\n2 1 1\r\n3 0 1\r\n"
    )
    ele = "2 3 0\r\n0 0 1 2\r\n1 0 2 3\r\n"
    neigh = "2 3\r\n0 -1 1 -1\r\n1 -1 -1 0\r\n"
    base = write_mesh(tmp_path, node=node, ele=ele, neigh=neigh)
    status, output, _ = run("check", base)
    assert status == 0
    assert output.endswith("2 triangles, with neighbors; the mesh is valid\n")


@pytest.mark.parametrize(
    "files, reason",
    [
        (
            {"ele": "2 3 0\n1 1 3 2\n2 1 3 4\n"},
            "triangle 0 is not counterclockwise",
        ),
        (
            {"neigh": "2 3\n1 -1 -1 -1\n2 -1 -1 1\n"},
            "triangle 0 has neighbor -1 opposite corner 1",
        ),
        ({"node": "4 2 0 0\n1 0 0\n2 1 zero\n"}, "square.node line 3"),
        ({"ele": "1 3 0\n1 1 2 5\n"}, "square.ele line 2: a triangle corner"),
        ({"neigh": "1 3\n1 -1 -1 -1\n"}, "the file has 1 triangles, but the mesh"),
        ({"ele": "0 3 0\n"}, "square.ele has no triangles"),
    ],
)
def test_check_errors(tmp_path, files, reason):
    base = write_mesh(tmp_path, **files)
    status, output, errors = run("check", base)
    assert status == 1
    assert output == ""
    assert errors.startswith("triangle-utils: error: ")
    assert reason in errors


def test_check_missing_file(tmp_path):
    status, _, errors = run("check", str(tmp_path / "missing"))
    assert status == 1
    assert "cannot open" in errors


# a 3 by 3 box with a 1 by 1 square hole, numbered from 1
BOX_POLY = """8 2 0 1
1 0 0 0
2 3 0 0
3 3 3 0
4 0 3 0
5 1 1 0
6 2 1 0
7 2 2 0
8 1 2 0
8 1
1 1 2 1
2 2 3 1
3 3 4 1
4 4 1 1
5 5 6 2
6 6 7 2
7 7 8 2
8 8 5 2
1
1 1.5 1.5
"""


def read_mesh_area(base, first_number):
    """The total area and largest triangle of a mesh's .node and .ele files."""
    node = [line.split() for line in open(f"{base}.node") if line.strip()]
    ele = [line.split() for line in open(f"{base}.ele") if line.strip()]
    first = int(node[1][0])
    xy = {int(row[0]): (float(row[1]), float(row[2])) for row in node[1:]}
    area = 0.0
    largest = 0.0
    for row in ele[1:]:
        (ax, ay), (bx, by), (cx, cy) = (xy[int(v)] for v in row[1:4])
        triangle = 0.5 * ((bx - ax) * (cy - ay) - (cx - ax) * (by - ay))
        area += triangle
        largest = max(largest, triangle)
    assert first == first_number
    return area, largest


def test_generate(tmp_path):
    (tmp_path / "box.poly").write_text(BOX_POLY)
    base = str(tmp_path / "box")
    status, output, _ = run("generate", "-a", "0.1", "--minimum-angle=25", base)
    assert status == 0
    assert output.startswith(f"{base}.1: ")
    area, largest = read_mesh_area(f"{base}.1", 1)
    assert area == pytest.approx(8.0)
    assert largest <= 0.1 + 1e-12
    status, output, _ = run("check", f"{base}.1")
    assert status == 0
    assert output.endswith("with neighbors; the mesh is valid\n")


def test_generate_errors(tmp_path):
    open_box = BOX_POLY.replace("8 1\n1 1 2 1\n2 2 3 1\n3 3 4 1\n4 4 1 1\n", "4 1\n")
    (tmp_path / "open.poly").write_text(open_box)
    status, _, errors = run("generate", str(tmp_path / "open"))
    assert status == 1
    assert "the mesh has no triangles" in errors
    status, _, errors = run("generate", str(tmp_path / "missing"))
    assert status == 1
    assert "cannot open" in errors


def vertex_count(base):
    """The number of vertices in a .node file."""
    return int(open(f"{base}.node").readline().split()[0])


def test_generate_options(tmp_path):
    (tmp_path / "box.poly").write_text(BOX_POLY)
    base = str(tmp_path / "box")
    assert run("generate", base)[0] == 0
    plain = vertex_count(f"{base}.1")
    out = str(tmp_path / "spaced")
    status, output, _ = run(
        "generate", "-s", "0.25", "-D", "-m", "1e-9", "-o", out, base
    )
    assert status == 0
    assert output.startswith(f"{out}: ")
    assert vertex_count(out) > plain
    assert read_mesh_area(out, 1)[0] == pytest.approx(8.0)

    # the vertices in a .node file, numbered from 0
    node = "4 2 0 0\n0 0 0\n1 2 0\n2 2 2\n3 0 2\n"
    poly = "0 2 0 0\n4 0\n0 0 1\n1 1 2\n2 2 3\n3 3 0\n0\n"
    (tmp_path / "zero.node").write_text(node)
    (tmp_path / "zero.poly").write_text(poly)
    status, _, _ = run("generate", "-a", "0.5", str(tmp_path / "zero"))
    assert status == 0
    area, largest = read_mesh_area(str(tmp_path / "zero.1"), 0)
    assert area == pytest.approx(4.0)
    assert largest <= 0.5 + 1e-12


def read_triangles(base):
    """The corners of each triangle in a mesh's .node and .ele files."""
    node = [line.split() for line in open(f"{base}.node") if line.strip()]
    ele = [line.split() for line in open(f"{base}.ele") if line.strip()]
    xy = {int(row[0]): (float(row[1]), float(row[2])) for row in node[1:]}
    return [tuple(xy[int(v)] for v in row[1:4]) for row in ele[1:]]


def largest_angle(corners):
    """The largest angle of a triangle in degrees."""
    largest = 0.0
    for index in range(3):
        (ax, ay), (bx, by), (cx, cy) = (corners[(index + k) % 3] for k in range(3))
        ux, uy, vx, vy = bx - ax, by - ay, cx - ax, cy - ay
        angle = math.degrees(math.atan2(abs(ux * vy - uy * vx), ux * vx + uy * vy))
        largest = max(largest, angle)
    return largest


def test_refine(tmp_path):
    (tmp_path / "box.poly").write_text(BOX_POLY)
    base = str(tmp_path / "box")
    assert run("generate", "-q", "30", base)[0] == 0
    original = read_triangles(f"{base}.1")
    limits = "".join(
        f"{t + 1} {0.01 if t == 0 else -1}\n" for t in range(len(original))
    )
    (tmp_path / "box.1.area").write_text(f"{len(original)}\n{limits}")
    status, output, _ = run("refine", "-a", "0.2", "-A", "100", f"{base}.1")
    assert status == 0
    assert output.startswith(f"{base}.2: ")
    # the reported count is that of the written mesh
    reported = int(output.rsplit("; ", 1)[1].split()[0])
    assert reported == sum(
        largest_angle(corners) > 100.0 for corners in read_triangles(f"{base}.2")
    )
    area, largest = read_mesh_area(f"{base}.2", 1)
    assert area == pytest.approx(8.0)
    assert largest <= 0.2 + 1e-12
    # the .area file's limit refines further than -a alone; Triangle spreads
    # a limit across the edges it flips, so it is not exact per triangle
    for suffix in ("node", "ele", "poly"):
        text = (tmp_path / f"box.1.{suffix}").read_text()
        (tmp_path / f"plain.{suffix}").write_text(text)
    assert run("refine", "-a", "0.2", str(tmp_path / "plain"))[0] == 0
    assert len(read_triangles(f"{base}.2")) > len(
        read_triangles(str(tmp_path / "plain.1"))
    )
    status, output, _ = run("check", f"{base}.2")
    assert status == 0


def test_improve_command(tmp_path):
    (tmp_path / "box.poly").write_text(BOX_POLY)
    base = str(tmp_path / "box")
    assert run("generate", "-q", "30", "-a", "0.1", base)[0] == 0
    assert run("refine", "-q", "30", "-A", "100", f"{base}.1")[0] == 0
    # mark an interior vertex to keep in place
    lines = open(f"{base}.2.node").read().splitlines()
    header = lines[0].split()
    count = int(header[0])
    rows = [line.split() for line in lines[1 : count + 1]]
    interior = next(
        i
        for i, row in enumerate(rows)
        if 0.2 < float(row[1]) < 0.8 and 0.2 < float(row[2]) < 0.8
    )
    marked = [f"{count} 2 0 1"] + [
        " ".join([*row[:3], "7" if i == interior else "0"])
        for i, row in enumerate(rows)
    ]
    (tmp_path / "box.2.node").write_text("\n".join(marked) + "\n")
    status, output, _ = run(
        "improve", "-A", "90", "--smoothing", "centroidal", f"{base}.2"
    )
    assert status == 0
    assert output.startswith(f"{base}.3: ")
    assert output.endswith(" with an angle over 90 degrees\n")
    assert read_mesh_area(f"{base}.3", 1)[0] == pytest.approx(8.0)
    improved = open(f"{base}.3.node").read().splitlines()[interior + 1].split()
    assert improved[1:3] == rows[interior][1:3]
    assert run("check", f"{base}.3")[0] == 0


def test_improve_keeps_points(tmp_path):
    # a square with a point feature, which generate marks as fixed
    poly = (
        "5 2 0 0\n1 0 0\n2 1 0\n3 1 1\n4 0 1\n5 0.33 0.41\n"
        "4 0\n1 1 2\n2 2 3\n3 3 4\n4 4 1\n0\n"
    )
    (tmp_path / "w.poly").write_text(poly)
    base = str(tmp_path / "w")
    assert run("generate", "-a", "0.01", "-q", "25", base)[0] == 0
    assert run("refine", "-A", "100", f"{base}.1")[0] == 0
    assert run("improve", "-A", "90", f"{base}.2")[0] == 0
    for name in (f"{base}.1", f"{base}.2", f"{base}.3"):
        lines = open(f"{name}.node").read().splitlines()
        rows = [line.split() for line in lines[1:] if line and not line.startswith("#")]
        point = [r for r in rows if float(r[1]) == 0.33 and float(r[2]) == 0.41]
        assert len(point) == 1, name
        assert point[0][3] == "1"


def read_disv(name):
    """The dimensions, vertices, and cells of a DISV file."""
    lines = [line.split() for line in open(name) if line.strip()]
    block = None
    dimensions, vertices, cells = {}, {}, []
    for words in lines:
        if words[0] == "BEGIN":
            block = words[1]
        elif words[0] == "END":
            block = None
        elif block == "DIMENSIONS":
            dimensions[words[0]] = int(words[1])
        elif block == "VERTICES":
            vertices[int(words[0])] = (float(words[1]), float(words[2]))
        elif block == "CELL2D":
            count = int(words[3])
            corners = [int(word) for word in words[4 : 4 + count]]
            assert len(words) == 4 + count
            cells.append((int(words[0]), float(words[1]), float(words[2]), corners))
    return dimensions, vertices, cells


def clockwise_area(vertices, corners):
    """The area of a cell, positive for clockwise corners."""
    points = [vertices[corner] for corner in corners]
    return 0.5 * sum(
        bx * ay - ax * by
        for (ax, ay), (bx, by) in zip(points, points[1:] + points[:1], strict=True)
    )


def test_disv(tmp_path):
    (tmp_path / "box.poly").write_text(BOX_POLY)
    base = str(tmp_path / "box")
    assert run("generate", "-a", "0.1", "-q", "25", "-D", base)[0] == 0
    vertex_total = vertex_count(f"{base}.1")
    status, output, _ = run("disv", "-t", "10", "-b", "-5", f"{base}.1")
    assert status == 0
    assert output.startswith(f"{base}.1.disv: {vertex_total} cells and ")
    dimensions, vertices, cells = read_disv(f"{base}.1.disv")
    assert dimensions == {"NLAY": 1, "NCPL": vertex_total, "NVERT": len(vertices)}
    assert [cell[0] for cell in cells] == list(range(1, vertex_total + 1))
    areas = [clockwise_area(vertices, cell[3]) for cell in cells]
    assert min(areas) > 0
    assert sum(areas) == pytest.approx(8.0)
    text = open(f"{base}.1.disv").read()
    assert "TOP\n    CONSTANT 10\n" in text
    assert "BOTM LAYERED\n    CONSTANT -5\n" in text
    assert "IDOMAIN LAYERED\n    CONSTANT 1\n" in text
    rows = [line.split() for line in open(f"{base}.1.cells")][1:]
    assert rows[0] == [str(vertex_total), "0"]
    assert [row[:2] for row in rows[1:]] == [
        [str(cell), str(cell)] for cell in range(1, vertex_total + 1)
    ]
    for grid in ("centroid", "circumcenter"):
        status, output, _ = run("disv", "-g", grid, "-o", f"{base}.{grid}", f"{base}.1")
        assert status == 0
        dimensions, vertices, cells = read_disv(f"{base}.{grid}.disv")
        triangle_total = len(open(f"{base}.1.ele").readlines()) - 1
        assert dimensions["NCPL"] == triangle_total
        assert len(vertices) == vertex_total
        assert sum(clockwise_area(vertices, cell[3]) for cell in cells) == (
            pytest.approx(8.0)
        )


def test_disv_cells(tmp_path):
    # the unit square in two triangles, numbered from 0, each with two
    # attributes
    base = write_mesh(
        tmp_path,
        node="4 2 0 0\n0 0 0\n1 1 0\n2 1 1\n3 0 1\n",
        ele="2 3 2\n0 0 1 2 5 50\n1 0 2 3 6 60\n",
    )
    assert run("disv", "-g", "centroid", base)[0] == 0
    rows = [line.split() for line in open(f"{base}.cells")][1:]
    assert rows == [["2", "2"], ["1", "0", "5", "50"], ["2", "1", "6", "60"]]
    assert run("disv", base)[0] == 0
    rows = [line.split() for line in open(f"{base}.cells")][1:]
    assert rows[0] == ["4", "2"]
    # vertices 1 and 3 are each in one triangle
    assert rows[2] == ["2", "1", "5", "50"]
    assert rows[4] == ["4", "3", "6", "60"]


def test_disv_ignores_poly(tmp_path):
    base = write_mesh(tmp_path)
    (tmp_path / "square.poly").write_text(BOX_POLY)
    assert run("refine", base)[0] == 1
    status, output, _ = run("disv", base)
    assert status == 0
    assert output.startswith(f"{base}.disv: 4 cells")


def test_disv_errors(tmp_path):
    # a triangle whose angle opposite its long edge is obtuse
    base = write_mesh(
        tmp_path, node="3 2 0 0\n1 0 0\n2 2 0\n3 1 0.2\n", ele="1 3 0\n1 1 2 3\n"
    )
    status, output, errors = run("disv", base)
    assert status == 1
    assert output == ""
    assert "over 90" in errors
    assert "conforming Delaunay" in errors
    base = write_mesh(tmp_path)
    status, _, errors = run("disv", "-t", "0", "-b", "1", base)
    assert status == 1
    assert "cell 0 in layer 0 has a top of 0 and a bottom of 1" in errors


def read_arrays(name):
    """The arrays of a MODFLOW 6 file by name, each a list of its values (one
    for a CONSTANT); a LAYERED array is a list of each layer's."""
    arrays, layered, values, current = {}, set(), None, None
    for words in (line.split() for line in open(name)):
        if not words or words[0].startswith("#") or words[0] in ("BEGIN", "END"):
            values = None
        elif words[0] in ("CONSTANT", "INTERNAL"):
            values = [float(word) for word in words[1:]]
            arrays[current].append(values)
        elif words[0][0].isalpha():
            current, values = words[0], None
            arrays[current] = []
            if words[1:] == ["LAYERED"]:
                layered.add(current)
        elif values is not None:
            values += [float(word) for word in words]
    return {
        key: values if key in layered else values[0]
        for key, values in arrays.items()
        if values
    }


def layer_files(tmp_path):
    """A mesh of the box, its cells' centers, and the options of three layers:
    a top from the mesh's vertices, a middle bottom from the cells that thins
    to pass-through in a band, and a middle layer cut out of the northeast
    corner."""
    (tmp_path / "box.poly").write_text(BOX_POLY)
    base = str(tmp_path / "box")
    assert run("generate", "-a", "0.05", "-q", "28", "-D", base)[0] == 0
    base += ".1"
    assert run("disv", base)[0] == 0
    centers = [(cell[1], cell[2]) for cell in read_disv(f"{base}.disv")[2]]
    rows = [line.split() for line in open(f"{base}.node")][1:]
    vertices = [(float(row[1]), float(row[2])) for row in rows if row and row[0] != "#"]
    (tmp_path / "top.txt").write_text(
        "# the top of each vertex\n" + "\n".join(f"{10 - x}" for x, _ in vertices)
    )
    middle = [4.95 if abs(x - 0.5) < 0.25 else 3.0 for x, _ in centers]
    (tmp_path / "middle.txt").write_text(" ".join(map(str, middle)))
    cut = [0 if x > 2.5 and y > 2.5 else 1 for x, y in centers]
    (tmp_path / "idomain.txt").write_text(" ".join(map(str, cut)))
    options = ["-t", str(tmp_path / "top.txt"), "-b", "5", "-b"]
    options += [str(tmp_path / "middle.txt"), "-b", "0", "-i", "1", "-i"]
    options += [str(tmp_path / "idomain.txt"), "-i", "1", "-m", "0.1"]
    return base, centers, options


def test_disv_layers(tmp_path):
    base, centers, options = layer_files(tmp_path)
    status, output, _ = run("disv", *options, base)
    assert status == 0
    arrays = read_arrays(f"{base}.disv")
    count = len(centers)
    band = [abs(x - 0.5) < 0.25 for x, _ in centers]
    cut = [x > 2.5 and y > 2.5 for x, y in centers]
    assert output == (
        f"{base}.disv: {count} cells and {len(read_disv(f'{base}.disv')[1])} "
        f"vertices in 3 layers, {sum(band)} pass-through\n"
    )
    assert read_disv(f"{base}.disv")[0]["NLAY"] == 3
    # a Voronoi cell's top is its vertex's
    assert arrays["TOP"] == pytest.approx([10 - x for x, _ in centers])
    assert arrays["BOTM"][0] == [5.0]
    assert arrays["BOTM"][2] == [0.0]
    assert arrays["BOTM"][1] == [4.95 if b else 3.0 for b in band]
    assert arrays["IDOMAIN"][0] == [1.0]
    assert arrays["IDOMAIN"][1] == [
        0.0 if c else -1.0 if b else 1.0 for b, c in zip(band, cut, strict=True)
    ]
    # a triangle grid has more cells than the mesh has vertices, so the top's
    # file is interpolated, exactly for a plane
    top = options[options.index("-t") + 1]
    output_base = f"{base}.centroid"
    status, _, _ = run("disv", "-g", "centroid", "-o", output_base, "-t", top, base)
    assert status == 0
    centroids = [(cell[1], cell[2]) for cell in read_disv(f"{output_base}.disv")[2]]
    assert len(centroids) != len(centers)
    assert read_arrays(f"{output_base}.disv")["TOP"] == pytest.approx(
        [10 - x for x, _ in centroids]
    )


def test_disu(tmp_path):
    base, centers, options = layer_files(tmp_path)
    assert run("disv", *options, base)[0] == 0
    idomain = read_arrays(f"{base}.disv")["IDOMAIN"]
    count = len(centers)
    active = [1.0] * count + idomain[1] + [1.0] * count
    nodes = [(i // count + 1, i % count + 1) for i, a in enumerate(active) if a > 0]
    status, output, _ = run("disu", *options, base)
    assert status == 0
    assert output.startswith(
        f"{base}.disu: {len(nodes)} nodes and "
    ) and output.endswith(
        f" in 3 layers of {count} cells, {active.count(-1.0)} pass-through\n"
    )
    text = open(f"{base}.disu").read()
    dimensions = read_disv(f"{base}.disu")[0]
    assert dimensions["NODES"] == len(nodes)
    arrays = read_arrays(f"{base}.disu")
    assert len(arrays["IAC"]) == len(nodes)
    assert sum(arrays["IAC"]) == dimensions["NJA"] == len(arrays["JA"])
    # each node's row starts with the node itself
    iac = [int(value) for value in arrays["IAC"]]
    starts = list(itertools.accumulate(iac, initial=0))[:-1]
    assert [arrays["JA"][s] for s in starts] == list(range(1, len(nodes) + 1))
    assert "BEGIN CELL2D" in text
    rows = [line.split() for line in open(f"{base}.nodes")]
    assert rows[1] == [str(len(nodes))]
    assert [(int(r[1]), int(r[2])) for r in rows[2:]] == nodes
    assert len(open(f"{base}.cells").readlines()) == count + 2

    status, output, _ = run("disu", "-f", "usg", "-o", f"{base}.usg", *options, base)
    assert status == 0
    lines = open(f"{base}.usg.disu").read().splitlines()
    # IVSD 0, one period, undefined units, and CL12
    assert lines[1] == f"{len(nodes)} 3 {dimensions['NJA']} 0 1 0 0 0"
    assert lines[-1] == "1 1 1 SS"
    # the arrays of the MODFLOW 6 file, with each horizontal face's area
    usg = read_usg_arrays(f"{base}.usg.disu", 3, dimensions["NODES"], dimensions["NJA"])
    assert usg["NODELAY"] == [sum(1 for n in nodes if n[0] == k) for k in (1, 2, 3)]
    for name in ("TOP", "BOT", "AREA", "IAC", "JA", "CL12"):
        assert usg[name] == pytest.approx(arrays[name]), name
    rows = [n for n, count in enumerate(iac) for _ in range(count)]
    thickness = [t - b for t, b in zip(arrays["TOP"], arrays["BOT"], strict=True)]
    for c, (row, other) in enumerate(zip(rows, arrays["JA"], strict=True)):
        mean = 0.5 * (thickness[row] + thickness[int(other) - 1])
        width = arrays["HWVA"][c]
        expected = width * mean if arrays["IHC"][c] == 1 else width
        assert usg["FAHL"][c] == pytest.approx(expected)


def read_usg_arrays(name, layer_count, node_count, connection_count):
    """The arrays of a MODFLOW-USG DISU file, each layer's arrays joined."""
    words = " ".join(open(name).read().splitlines()[3:]).split()
    position = 0

    def array(count):
        nonlocal position
        if words[position] == "CONSTANT":
            values = [float(words[position + 1])] * count
            position += 2
        else:
            assert words[position : position + 4] == ["INTERNAL", "1", "(FREE)", "-1"]
            values = [
                float(word) for word in words[position + 4 : position + 4 + count]
            ]
            position += 4 + count
        return values

    arrays = {"NODELAY": [int(value) for value in array(layer_count)]}
    for key in ("TOP", "BOT", "AREA"):
        arrays[key] = [v for count in arrays["NODELAY"] for v in array(count)]
    arrays["IAC"] = array(node_count)
    for key in ("JA", "CL12", "FAHL"):
        arrays[key] = array(connection_count)
    assert words[position:] == ["1", "1", "1", "SS"]
    return arrays


def test_disu_errors(tmp_path):
    base, centers, _ = layer_files(tmp_path)
    (tmp_path / "short.txt").write_text("1 2 3")
    status, _, errors = run("disu", "-b", str(tmp_path / "short.txt"), base)
    assert status == 1
    assert "short.txt has 3 values, but the grid has " in errors
    (tmp_path / "word.txt").write_text("1\ntwo\n")
    status, _, errors = run("disu", "-t", str(tmp_path / "word.txt"), base)
    assert status == 1
    assert 'word.txt line 2: "two" is not a finite number' in errors
    status, _, errors = run("disu", "-t", "nan", base)
    assert status == 1
    assert "nan is not a finite number" in errors
    status, _, errors = run("disu", "-i", "1.5", base)
    assert status == 1
    assert "the idomain 1.5 is not an integer" in errors
    (tmp_path / "half.txt").write_text("0.5\n" * len(centers))
    status, _, errors = run("disu", "-i", str(tmp_path / "half.txt"), base)
    assert status == 1
    assert "the idomain 0.5 in " in errors and "half.txt is not an integer" in errors
    # a layer of only pass-through cells, which MODFLOW-USG cannot read,
    # leaving the files as they were
    Path(f"{base}.disu").write_text("kept")
    status, _, errors = run(
        "disu",
        "-f",
        "usg",
        "-t",
        "10",
        "-b",
        "5",
        "-b",
        "5",
        "-b",
        "0",
        "-m",
        "1",
        base,
    )
    assert status == 1
    assert "layer 1 has no active cells, which MODFLOW-USG needs" in errors
    assert Path(f"{base}.disu").read_text() == "kept"
    assert not Path(f"{base}.nodes").exists()


def read_cells(output):
    """The cells, starts, and lengths cells prints, after its comment."""
    lines = output.splitlines()
    assert lines[0].startswith("# cell, start, and length along ")
    rows = [line.split() for line in lines[1:]]
    return [(int(cell), float(start), float(length)) for cell, start, length in rows]


def test_cells(tmp_path):
    (tmp_path / "box.poly").write_text(BOX_POLY)
    base = str(tmp_path / "box")
    assert run("generate", "-a", "0.1", "-q", "25", "-D", base)[0] == 0
    # from outside the box to beyond it, through 3 of the box and none of the
    # hole, which it misses
    (tmp_path / "line.txt").write_text("# x y\n-1 0.5\n\n4 0.5  # east of the box\n")
    status, output, _ = run("cells", "-l", str(tmp_path / "line.txt"), f"{base}.1")
    assert status == 0
    cells = read_cells(output)
    assert cells[0][1] == pytest.approx(1.0)
    assert sum(length for _, _, length in cells) == pytest.approx(3.0)
    for (_, start, length), (_, next_start, _) in itertools.pairwise(cells):
        assert next_start == pytest.approx(start + length)
    vertex_total = vertex_count(f"{base}.1")
    assert all(1 <= cell <= vertex_total for cell, _, _ in cells)
    # a point of the box's corner and one outside it
    (tmp_path / "points.txt").write_text("0 0\n5 5\n")
    status, output, _ = run("cells", "-p", str(tmp_path / "points.txt"), f"{base}.1")
    assert status == 0
    rows = [line.split() for line in output.splitlines()[1:]]
    assert [row[0] for row in rows] == ["1", "2"]
    assert int(rows[0][1]) >= 1
    assert rows[1][1] == "0"
    for grid in ("voronoi", "centroid"):
        status, output, _ = run("cells", "-g", grid, "-m", "1", f"{base}.1")
        assert status == 0
        cells = read_cells(output)
        assert sum(length for _, _, length in cells) == pytest.approx(12.0)
        assert len({cell for cell, _, _ in cells}) == len(cells)


def test_cells_errors(tmp_path):
    (tmp_path / "box.poly").write_text(BOX_POLY)
    base = str(tmp_path / "box")
    assert run("generate", "-a", "0.1", "-D", base)[0] == 0
    (tmp_path / "bad.txt").write_text("0 0\n1\n")
    status, _, errors = run("cells", "-l", str(tmp_path / "bad.txt"), f"{base}.1")
    assert status == 1
    assert "bad.txt line 2: expected x and y" in errors
    status, _, errors = run("cells", "-m", "7", f"{base}.1")
    assert status == 1
    assert "no segment of the mesh has marker 7" in errors


def test_guide_commands(tmp_path):
    guide = Path(__file__).resolve().parents[2] / "doc" / "UserGuide"
    valley = runpy.run_path(str(guide / "valley.py"))
    lines = (guide / "valley_commands.txt").read_text().splitlines()
    # the scripts of the guide's folder, which import valley.py from beside
    # them, and the program
    scripts = [line.split() for line in lines if line.startswith("python ")]
    commands = [line.split() for line in lines if not line.startswith("python ")]
    # the scripts find the module as the tests do
    environment = {**os.environ, "PYTHONPATH": os.pathsep.join(sys.path)}
    outputs = {}
    for words in (line.split() for line in lines):
        if words[0] == "python":
            arguments = [sys.executable, str(guide / words[1]), *words[2:]]
        else:
            assert words[0] == "triangle-utils"
            arguments = [PROGRAM, *words[1:]]
        result = subprocess.run(
            arguments,
            capture_output=True,
            text=True,
            check=False,
            cwd=tmp_path,
            env=environment,
        )
        assert result.returncode == 0, result.stderr
        outputs[" ".join(words[1:])] = result.stdout
    assert [script[1] for script in scripts] == ["valley_files.py"] * 2
    # the grid file, and the cells of each stream, the boundary, and the wells
    assert (tmp_path / "valley.2.disv").stat().st_size > 0
    # the layers, from the surfaces at the mesh's vertices, with cells of the
    # clay passing flow between the aquifers
    disu = commands[[c[1] for c in commands].index("disu")]
    layers = outputs[" ".join(disu[1:])]
    assert layers.startswith("valley.2.disu: ")
    assert " in 3 layers of " in layers
    assert int(layers.split(", ")[-1].split()[0]) > 0

    def rows(command):
        output = outputs[f"cells {command} valley.2"]
        return [line.split() for line in output.splitlines() if line[:1] != "#"]

    # a line passes through a cell centered on each of its points, and the
    # boundary through one on each corner
    assert len(rows("-l river.txt")) >= len(valley["RIVER"])
    assert len(rows("-l tributary.txt")) >= len(valley["TRIBUTARY"])
    assert len(rows("-m 1")) >= len(valley["BOUNDARY"])
    # each well in a cell of its own
    wells = [int(row[1]) for row in rows("-p wells.txt")]
    assert len(wells) == len(valley["WELLS"])
    assert min(wells) > 0 and len(set(wells)) == len(wells)
    # the .poly file gave the wells, the regions, and the markers
    node = (tmp_path / "valley.1.node").read_text().split("\n")
    vertices = {
        (float(w[1]), float(w[2])): int(w[-1])
        for w in (line.split() for line in node[1:])
        if w
    }
    assert all(vertices[well] == 1 for well in valley["WELLS"])
    ele = [line.split() for line in (tmp_path / "valley.1.ele").read_text().split("\n")]
    assert ele[0][2] == "1"
    attributes = {float(w[4]) for w in ele[1:] if w}
    assert attributes == {valley["AQUIFER"], valley["LAKE_REGION"]}
    poly = [
        line.split() for line in (tmp_path / "valley.1.poly").read_text().split("\n")
    ]
    segment_count = int(poly[1][0])
    markers = {int(w[3]) for w in poly[2 : 2 + segment_count]}
    marker_names = ("BOUNDARY_MARKER", "RIVER_MARKER", "LAKE_MARKER")
    assert markers == {valley[name] for name in marker_names}
    # the commands use the values of valley.py
    options = {(c[1], c[i]): c[i + 1] for c in commands for i in range(2, len(c) - 1)}
    assert float(options["generate", "-a"]) == valley["MAXIMUM_AREA"]
    assert float(options["generate", "-q"]) == valley["MINIMUM_ANGLE"]
    assert float(options["generate", "-s"]) == valley["BOUNDARY_SPACING"]
    assert float(options["refine", "-A"]) == valley["MAXIMUM_ANGLE"]
    assert float(options["improve", "-A"]) == valley["IMPROVE_ANGLE"]
    assert int(options["cells", "-m"]) == valley["BOUNDARY_MARKER"]
    assert float(disu[disu.index("-b") + 1]) == valley["UPPER_BOTTOM"]
    assert float(options["disu", "-m"]) == valley["MINIMUM_THICKNESS"]


def test_generate_shapefiles(tmp_path):
    geopandas = pytest.importorskip("geopandas")
    import numpy as np
    import triangle_utils as tu
    from shapely.geometry import LineString, MultiPoint, Point, Polygon

    square = [(0, 0), (10, 0), (10, 10), (0, 10)]
    hole = [(6, 6), (8, 6), (8, 8), (6, 8)]
    frames = {
        "areas": geopandas.GeoDataFrame(
            {"marker": [1], "spacing": [2.0]},
            geometry=[Polygon(square, [hole])],
            crs="EPSG:26916",
        ),
        "river": geopandas.GeoDataFrame(
            {"marker": [2]},
            geometry=[LineString([(1, 5), (5, 3), (9, 5)])],
            crs="EPSG:26916",
        ),
        "regions": geopandas.GeoDataFrame(
            {"zone": [1.0, 2.0], "area": [0.5, 0.0]},
            geometry=[MultiPoint([(2, 8)]), MultiPoint([(5, 1)])],
            crs="EPSG:26916",
        ),
        "wells": geopandas.GeoDataFrame(
            geometry=[Point(3, 3), Point(7, 2)], crs="EPSG:26916"
        ),
        # a point in the hole the polygon's hole makes
        "inside": geopandas.GeoDataFrame(geometry=[Point(7, 7)], crs="EPSG:26916"),
    }
    for name, frame in frames.items():
        frame.to_file(tmp_path / f"{name}.shp")
    shapefile = {name: str(tmp_path / f"{name}.shp") for name in frames}
    base = str(tmp_path / "model")
    status, output, errors = run(
        *("generate", "-a", "1", "-q", "28", "-D", "-o", base),
        *(
            "--marker",
            "marker",
            "--feature-spacing",
            "spacing",
            "-f",
            shapefile["areas"],
        ),
        # the values are the areas' only, so the river's marker is 0
        *("-f", shapefile["river"]),
        *("--attribute", "zone", "--area-limit", "area", "-r", shapefile["regions"]),
        *("-f", shapefile["wells"], "-H", shapefile["inside"], str(tmp_path / "none")),
    )
    assert status == 0, errors
    assert output.startswith(f"{base}: ")

    geometry = tu.Geometry()
    values = {"marker": "marker", "spacing": "spacing"}
    geometry.add_features(tu.read_features(shapefile["areas"], **values))
    geometry.add_features(tu.read_features(shapefile["river"]))
    regions = tu.read_features(
        shapefile["regions"], attribute="zone", area_limit="area"
    )
    geometry.add_features(regions, points="regions")
    geometry.add_features(tu.read_features(shapefile["wells"]))
    geometry.add_features(tu.read_features(shapefile["inside"]), points="holes")
    mesh = tu.generate(
        geometry, maximum_area=1, minimum_angle=28, conforming_delaunay=True
    )
    node = np.loadtxt(f"{base}.node", skiprows=1, comments="#", ndmin=2)
    ele = np.loadtxt(f"{base}.ele", skiprows=1, comments="#", ndmin=2)
    np.testing.assert_array_equal(node[:, 1:3], mesh.vertices)
    np.testing.assert_array_equal(ele[:, 1:4] - 1, mesh.triangles)
    np.testing.assert_array_equal(ele[:, 4:], mesh.attributes)
    # the wells are kept in place, and the regions are in the .poly file
    wells = [tuple(row[1:3]) for row in node if row[3] == 1]
    assert set(wells) == {(3.0, 3.0), (7.0, 2.0)}
    # the segments and their markers, and the regions and the hole
    rows = [line.split() for line in open(f"{base}.poly") if line.strip()]
    segment_count = int(rows[1][0])
    segments = np.array(rows[2 : 2 + segment_count], dtype=float)
    np.testing.assert_array_equal(segments[:, 1:3] - 1, mesh.segments)
    np.testing.assert_array_equal(segments[:, 3], mesh.segment_markers)
    assert 0 in mesh.segment_markers and 1 in mesh.segment_markers
    holes = rows[2 + segment_count]
    assert holes[0] == "1"
    assert rows[3 + segment_count][1:] == ["7", "7"]
    assert rows[5 + segment_count][1:] == ["2", "8", "1", "0.5"]

    status, output, _ = run("cells", "-l", shapefile["river"], base)
    assert status == 0
    assert output.splitlines()[0] == (
        f"# cell, start, and length along {shapefile['river']} line 1, from row 0"
    )
    cells = read_cells(output)
    assert sum(length for _, _, length in cells) == pytest.approx(2 * math.hypot(4, 2))
    status, output, _ = run("cells", "-p", shapefile["wells"], base)
    assert status == 0
    assert [line.split()[0] for line in output.splitlines()[1:]] == ["1", "2"]

    for arguments, reason in (
        (
            ("--marker", "zone", "-f", shapefile["areas"]),
            "the marker field zone is not in the .dbf file",
        ),
        (
            ("--marker", "1.5", "-f", shapefile["areas"]),
            "the marker 1.5 is not a whole",
        ),
    ):
        status, _, errors = run("generate", "-o", base, *arguments, str(tmp_path / "x"))
        assert status == 1
        assert reason in errors
    status, _, errors = run("cells", "-l", shapefile["wells"], base)
    assert status == 1
    assert "has no lines" in errors
    # a value after the last shapefile is for none
    status, _, errors = run("generate", "-f", shapefile["areas"], "--marker", "2", base)
    assert status == 2
    assert "give --marker, --feature-spacing, --attribute, and --area-limit" in errors
