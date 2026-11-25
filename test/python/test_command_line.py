"""
Tests the triangle-utils program.

The program is named by the TRIANGLE_UTILS_PROGRAM environment variable,
which meson test sets; each test runs it and checks its output and exit
status.

Cases:
  - test_help          : -h and --help print the usage and exit 0.
  - test_version       : --version prints the triangle-utils and Triangle
                         versions and exits 0.
  - test_license       : --license prints CC0 and Triangle's terms and exits 0.
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
  - test_disv_errors   : a mesh that is not Delaunay exits 1 with the reason.
  - test_generate_errors : a bad option value is a usage error (2), and a
                         boundary not closed and a missing file are errors
                         (1).
"""

import math
import os
import subprocess

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
            ("disv", "-t", "0", "-b", "1", "box"),
            "disv: the top (0) must be above the bottom (1)",
        ),
        (("disv",), "disv: give one <base>"),
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
