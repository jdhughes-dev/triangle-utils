"""The example valley's files for the triangle-utils program.

Writes valley.poly, with the boundary, streams, and lake as segments with
their markers, the wells as vertices, and a point inside each region with
its attribute and area limit, and river.txt, tributary.txt, and wells.txt,
the points the cells command maps; valley_commands.txt runs the program on
them. Writes to the folder given, or the current one.
"""

import sys
from pathlib import Path

from valley import (
    BOUNDARY,
    BOUNDARY_MARKER,
    LAKE,
    LAKE_MARKER,
    REGIONS,
    RIVER,
    RIVER_MARKER,
    TRIBUTARY,
    WELLS,
)


def write_files(folder):
    """Write the valley's .poly file and point files to a folder."""
    vertices, segments = [], []

    def add_chain(points, marker, closed):
        first = len(vertices)
        vertices.extend(points)
        last = len(vertices) - 1
        for vertex in range(first, last):
            segments.append((vertex, vertex + 1, marker))
        if closed:
            segments.append((last, first, marker))

    add_chain(BOUNDARY, BOUNDARY_MARKER, closed=True)
    add_chain(RIVER, RIVER_MARKER, closed=False)
    # the tributary's last point is one of the river's, which generate
    # merges into one vertex
    add_chain(TRIBUTARY, RIVER_MARKER, closed=False)
    add_chain(LAKE, LAKE_MARKER, closed=True)
    vertices.extend(WELLS)
    lines = [f"{len(vertices)} 2 0 0"]
    lines += [f"{n} {x} {y}" for n, (x, y) in enumerate(vertices, 1)]
    lines.append(f"{len(segments)} 1")
    lines += [f"{n} {a + 1} {b + 1} {m}" for n, (a, b, m) in enumerate(segments, 1)]
    lines.append("0")
    lines.append(str(len(REGIONS)))
    # an area limit of -1 is none, as Triangle reads it
    lines += [
        f"{n} {x} {y} {attribute} {area if area > 0 else -1}"
        for n, ((x, y), attribute, area) in enumerate(REGIONS, 1)
    ]
    (folder / "valley.poly").write_text("\n".join(lines) + "\n")
    for name, points in (("river", RIVER), ("tributary", TRIBUTARY), ("wells", WELLS)):
        (folder / f"{name}.txt").write_text("".join(f"{x} {y}\n" for x, y in points))


if __name__ == "__main__":
    write_files(Path(sys.argv[1]) if len(sys.argv) > 1 else Path.cwd())
