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
  - test_usage_errors  : no arguments, an unknown option or command, and a
                         check without a base exit 2 with a message.
  - test_check_help    : check -h prints the command's usage and exits 0.
  - test_check         : a valid mesh, with and without neighbors, exits 0.
  - test_check_errors  : a clockwise triangle, a wrong neighbor, and a bad
                         line exit 1 with the reason.
  - test_check_missing_file : a missing file exits 1 with the reason.
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
    (directory / "square.node").write_text(node)
    (directory / "square.ele").write_text(ele)
    if neigh is not None:
        (directory / "square.neigh").write_text(neigh)
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
    assert lines[0].startswith("triangle-utils 0.")
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
        (("--bogus",), "unknown option --bogus"),
        (("bogus",), "unknown command bogus"),
        (("--version", "extra"), "--version takes no arguments"),
        (("check",), "check: give one <base>"),
        (("check", "--bogus", "base"), "check: unknown option --bogus"),
    ],
)
def test_usage_errors(arguments, reason):
    status, output, errors = run(*arguments)
    assert status == 2
    assert output == ""
    assert errors.startswith(f"triangle-utils: {reason}\n")
    assert "triangle-utils -h" in errors


def test_check_help():
    status, output, _ = run("check", "-h")
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
