"""Fixtures for the tests of the C API, the Python module, and the program."""

import os
import sys
from pathlib import Path

import pytest

# the Python module, in python/ of the repository, unless the installed
# package is being tested
if not os.environ.get("TRIANGLE_UTILS_TEST_INSTALLED"):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

from triangle_utils._library import load


@pytest.fixture(scope="session")
def library():
    return load()
