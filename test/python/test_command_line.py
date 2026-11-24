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
  - test_generate_errors : a bad option value is a usage error (2), and a
                         boundary not closed and a missing file are errors
                         (1).
"""

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
        (
            ("generate", "-o", "box", "box"),
            "generate: the output would replace the input; give another -o",
        ),
        (("--bogus",), "unknown option --bogus"),
        (("bogus",), "unknown command bogus"),
        (("--version", "extra"), "--version takes no arguments"),
        (("--license", "extra"), "--license takes no arguments"),
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
