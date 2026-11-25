"""
Tests the files of the Python package.

The wheel's tests set TRIANGLE_UTILS_TEST_INSTALLED, so the installed
package is imported rather than the module in the repository.

Cases:
  - test_triangle_license : LICENSE-Triangle is the license of the Triangle
                            subproject that triangle.wrap names (skipped
                            before the subproject is downloaded).
  - test_installed        : the installed package has both licenses beside
                            the module, loads the library beside it, and
                            reports the package's version
                            (skipped unless testing the installed package).
"""

import os
import sys
from importlib import metadata
from pathlib import Path

import pytest
import triangle_utils

REPOSITORY = Path(__file__).resolve().parents[2]


def test_triangle_license():
    wrap = (REPOSITORY / "subprojects" / "triangle.wrap").read_text()
    directory = next(
        line.split("=", 1)[1].strip()
        for line in wrap.splitlines()
        if line.startswith("directory")
    )
    license_file = REPOSITORY / "subprojects" / directory / "LICENSE"
    if not license_file.exists():
        pytest.skip("the Triangle subproject has not been downloaded")
    assert (REPOSITORY / "LICENSE-Triangle").read_text() == license_file.read_text()


@pytest.mark.skipif(
    not os.environ.get("TRIANGLE_UTILS_TEST_INSTALLED"),
    reason="not testing the installed package",
)
def test_installed():
    package = Path(triangle_utils.__file__).parent
    assert REPOSITORY not in package.parents
    assert "CC0 1.0" in (package / "LICENSE").read_text()
    assert "Shewchuk" in (package / "LICENSE-Triangle").read_text()
    loaded = sys.modules["triangle_utils._library"].library_path()
    assert loaded.parent == package
    assert triangle_utils.version()[0] == metadata.version("triangle-utils")
