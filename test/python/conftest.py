"""Fixtures for the tests of the C API, the Python module, and the program."""

import sys
from pathlib import Path

import pytest

# the Python module, in python/ of the repository
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

from triangle_utils._library import load


@pytest.fixture(scope="session")
def library():
    return load()
