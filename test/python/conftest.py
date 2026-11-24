"""Fixtures for the tests of the C API and the program."""

import pytest
from triangle_utils_api import load


@pytest.fixture(scope="session")
def library():
    return load()
