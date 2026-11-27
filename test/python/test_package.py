"""
Tests the files of the Python package.

The wheel's tests set TRIANGLE_UTILS_TEST_INSTALLED, so the installed
package is imported rather than the module in the repository.

Cases:
  - test_triangle_license : LICENSE-Triangle is the wrapped subproject's
                            license (skipped before it is downloaded).
  - test_notice           : NOTICE gives the wrapped Triangle's source.
  - test_shapelib_license : LICENSE-shapelib is the vendored shapelib's MIT
                            license, and its README and NOTICE give the
                            version of its header.
  - test_installed        : the installed package has its files and licenses
                            and loads its own library (installed only).
"""

import os
import re
import sys
from importlib import metadata
from pathlib import Path

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


def test_shapelib_license():
    vendored = REPOSITORY / "src" / "shapelib" / "LICENSE-MIT"
    assert (REPOSITORY / "LICENSE-shapelib").read_text() == vendored.read_text()
    # the version of the header, as the README and NOTICE give it
    header = (REPOSITORY / "src" / "shapelib" / "shapefil.h").read_text()
    version = ".".join(
        re.search(rf"#define SHAPELIB_VERSION_{part} (\d+)", header).group(1)
        for part in ("MAJOR", "MINOR", "MICRO")
    )
    assert (
        f"shapelib {version},"
        in (REPOSITORY / "src" / "shapelib" / "README.md").read_text()
    )
    assert f"shapelib {version}," in (REPOSITORY / "NOTICE").read_text()


@pytest.mark.skipif(
    not os.environ.get("TRIANGLE_UTILS_TEST_INSTALLED"),
    reason="not testing the installed package",
)
def test_installed():
    package = Path(triangle_utils.__file__).parent
    assert package != REPOSITORY / "python" / "triangle_utils"
    assert "CC0 1.0" in (package / "LICENSE").read_text()
    assert "Shewchuk" in (package / "LICENSE-Triangle").read_text()
    assert "Warmerdam" in (package / "LICENSE-shapelib").read_text()
    assert (package / "NOTICE").read_text() == (REPOSITORY / "NOTICE").read_text()
    modules = sorted(
        p.name for p in (REPOSITORY / "python" / "triangle_utils").glob("*.py")
    )
    assert sorted(p.name for p in package.glob("*.py")) == modules
    licenses = {
        path.name
        for path in metadata.distribution("triangle-utils").files
        if path.parent.name == "licenses"
    }
    assert licenses == {"LICENSE", "LICENSE-Triangle", "LICENSE-shapelib", "NOTICE"}
    expression = metadata.metadata("triangle-utils")["License-Expression"]
    assert expression == "CC0-1.0 AND LicenseRef-Triangle AND MIT"
    loaded = sys.modules["triangle_utils._library"].library_path()
    assert loaded.parent == package
    assert triangle_utils.version()[0] == metadata.version("triangle-utils")
