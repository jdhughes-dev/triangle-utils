"""
Make the user guide's figures and the values its text quotes.

Takes the example valley's meshes and grid from valley.py, which the guide
prints, so its example is the code that made the figures; uses the library
`pixi run build` installs, and writes the figures to Figures/ and the quoted
values to values.tex, so the guide rebuilds from the repository alone.
"""

import sys
from pathlib import Path

import flopy.plot.styles as styles
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.collections import PolyCollection
from matplotlib.colors import to_rgba
from matplotlib.patches import Patch, Rectangle
from matplotlib.ticker import FuncFormatter

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "python"))

import triangle_utils as tu  # noqa: E402
from valley import (  # noqa: E402
    AQUIFER,
    BASE_EAST,
    BASE_WEST,
    BOUNDARY,
    BOUNDARY_MARKER,
    BOUNDARY_SPACING,
    CLAY_EDGE,
    CLAY_THICKNESS,
    IMPROVE_ANGLE,
    LAKE,
    LAKE_AREA,
    LAKE_DEPTH,
    LAKE_MARKER,
    LAKE_REGION,
    LAKE_SPACING,
    MAXIMUM_ANGLE,
    MAXIMUM_AREA,
    MINIMUM_ANGLE,
    MINIMUM_THICKNESS,
    RIDGE_HEIGHT,
    RIVER_MARKER,
    RIVER_MOUTH_ELEVATION,
    RIVER_SPACING,
    TRIBUTARY,
    UPPER_BOTTOM,
    WELL_AREA,
    WELL_POINTS,
    WELL_RADIUS,
    WELL_REGION,
    WELLS,
    boundary_cells,
    connections,
    delaunay_refined,
    grid,
    improved,
    layers,
    mesh,
    refined,
    stream_cells,
    well_cells,
)

FIGURES = HERE / "Figures"


# the axis limits of a valley panel: a margin around the boundary
_x, _y = np.array(BOUNDARY, dtype=float).T
MARGIN = 100.0
X_LIMITS = (_x.min() - MARGIN, _x.max() + MARGIN)
Y_LIMITS = (_y.min() - MARGIN, _y.max() + MARGIN)
# the sides of the square close-ups around a well and along the river, in
# meters
WELL_WINDOW = 600.0
# a window on the lake's shore at its east corner, (x, y, side) in meters
LAKE_WINDOW = (1500.0, 5400.0, 600.0)
RIVER_WINDOW = 800.0
# the window on the confluence, where the tributary joins the river
CONFLUENCE_WINDOW = (*TRIBUTARY[-1], RIVER_WINDOW)

REGION_NAMES = {AQUIFER: "Aquifer", LAKE_REGION: "Lake", WELL_REGION: "Well ring"}
REGION_COLORS = {AQUIFER: "#e8e3d3", LAKE_REGION: "#a6cee3", WELL_REGION: "#fdbf6f"}

# tick labels with thousands separators, as the text writes numbers
THOUSANDS = FuncFormatter(lambda value, _: f"{value:,.0f}")


def draw_mesh(ax, mesh, highlight=None):
    """Draw the triangles colored by region, the river, and the wells."""
    regions = mesh.attributes[:, 0].astype(int)
    colors = [REGION_COLORS[region] for region in regions]
    if highlight is not None:
        colors = [
            to_rgba("#e31a1c") if over else color
            for over, color in zip(highlight, colors, strict=True)
        ]
    triangles = PolyCollection(
        mesh.vertices[mesh.triangles],
        facecolors=colors,
        edgecolors="0.35",
        linewidths=0.15,
    )
    ax.add_collection(triangles)
    for segment, marker in zip(mesh.segments, mesh.segment_markers, strict=True):
        if marker == RIVER_MARKER:
            ax.plot(*mesh.vertices[segment].T, color="#1f78b4", lw=1.0)
    for x, y in WELLS:
        ax.plot(x, y, marker="o", color="black", ms=2.5, ls="none")
    ax.xaxis.set_major_formatter(THOUSANDS)
    ax.yaxis.set_major_formatter(THOUSANDS)
    ax.set_aspect("equal")
    ax.set_xlim(*X_LIMITS)
    ax.set_ylim(*Y_LIMITS)


def centroids(mesh):
    """The centroid of each triangle."""
    return mesh.vertices[mesh.triangles].mean(axis=1)


def in_window(points, window):
    """Whether each point is inside a square window (x, y, side)."""
    x, y, side = window
    half = 0.5 * side
    return (np.abs(points[:, 0] - x) <= half) & (np.abs(points[:, 1] - y) <= half)


def choose_windows(mesh):
    """The close-up windows: the well whose window holds the most triangles
    over the maximum angle, the confluence, and the lake's shore."""
    over = centroids(mesh)[mesh.largest_angles() > MAXIMUM_ANGLE]
    wells = [(x, y, WELL_WINDOW) for x, y in WELLS]
    well = max(wells, key=lambda window: in_window(over, window).sum())
    return {"B": well, "C": CONFLUENCE_WINDOW, "D": LAKE_WINDOW}


# the sections of valley.py the guide prints in turn, by the start of the
# first line of each
VALLEY_SECTIONS = {
    "coordinates": "import triangle_utils as tu",
    "values": "# the spacing of points along each feature",
    "geometry": "geometry = tu.Geometry()",
    "meshing": "mesh = tu.generate(",
    "grid": "# the Voronoi grid needs",
    "connections": "# the connections between",
    "features": "# each stream's cells",
    "layers": "def inside_polygon(",
}


def valley_section_lines():
    """The first and last line of each section of valley.py, for the guide;
    a section ends before the blank line that precedes the next."""
    lines = (HERE / "valley.py").read_text().splitlines()
    firsts = []
    for start in VALLEY_SECTIONS.values():
        found = [n for n, line in enumerate(lines, 1) if line.startswith(start)]
        if len(found) != 1:
            raise ValueError(f"valley.py has {len(found)} lines starting {start!r}")
        firsts.append(found[0])
    lasts = [first - 2 for first in firsts[1:]] + [len(lines)]
    values = {}
    for name, first, last in zip(VALLEY_SECTIONS, firsts, lasts, strict=True):
        values[f"valley{name}first"] = first
        values[f"valley{name}last"] = last
    return values


def over_in(mesh, limit, window):
    """The number of triangles over an angle whose centroids are in a
    window."""
    return int(in_window(centroids(mesh)[mesh.largest_angles() > limit], window).sum())


def show_window(ax, window, letter):
    """Outline a close-up window on a valley panel, with its letter."""
    x, y, side = window
    ax.add_patch(
        Rectangle(
            (x - 0.5 * side, y - 0.5 * side),
            side,
            side,
            fill=False,
            edgecolor="black",
            lw=0.9,
        )
    )
    ax.text(
        x + 0.5 * side + 60,
        y + 0.5 * side,
        letter,
        fontsize=8,
        fontweight="bold",
        va="top",
    )


def zoom(ax, window):
    """Limit a panel to a close-up window."""
    x, y, side = window
    ax.set_xlim(x - 0.5 * side, x + 0.5 * side)
    ax.set_ylim(y - 0.5 * side, y + 0.5 * side)


def explanation(fig, handles, columns=None):
    """The explanation below the panels, in one row unless the columns are
    given."""
    legend = fig.legend(
        handles=handles,
        labels=[h.get_label() for h in handles],
        loc="outside lower center",
        ncols=columns or len(handles),
        title="EXPLANATION",
        frameon=False,
    )
    legend.get_title().set_fontweight("bold")


def save(fig, name):
    """Save a figure with the axis labels of its panels aligned."""
    fig.align_labels()
    # the layout leaves no room for the panel headings, which are text above
    # the panels, so the saved figure is cropped to everything drawn
    fig.savefig(FIGURES / name, bbox_inches="tight", pad_inches=0.05)


def well_handle():
    return plt.Line2D(
        [], [], color="black", marker="o", ms=2.5, ls="none", label="Pumping well"
    )


# the height, in inches, and the width ratio at which a valley panel and
# three close-ups beside it end together
VALLEY_FIGURE_HEIGHT = 7.5
VALLEY_FIGURE_RATIO = 1.6
# the headings of the close-up windows
WINDOW_HEADINGS = {
    "B": "Around a well",
    "C": "At the confluence",
    "D": "On the lake's shore",
}


def valley_and_windows():
    """A figure with the valley in A and its three close-ups beside it."""
    return plt.subplot_mosaic(
        [["A", "B"], ["A", "C"], ["A", "D"]],
        figsize=(6.7, VALLEY_FIGURE_HEIGHT),
        width_ratios=[VALLEY_FIGURE_RATIO, 1.0],
        layout="constrained",
    )


def mesh_figure(mesh, windows):
    """The generated mesh of the valley, with close-ups of a well, the river,
    and the lake's shore."""
    with styles.USGSMap():
        fig, axd = valley_and_windows()
        draw_mesh(axd["A"], mesh)
        for letter, window in windows.items():
            show_window(axd["A"], window, letter)
        styles.heading(ax=axd["A"], letter="A", heading="Valley")
        styles.ylabel(ax=axd["A"], label="y, in meters")
        styles.xlabel(ax=axd["A"], label="x, in meters")
        for letter, heading in WINDOW_HEADINGS.items():
            draw_mesh(axd[letter], mesh)
            zoom(axd[letter], windows[letter])
            styles.heading(ax=axd[letter], letter=letter, heading=heading)
        styles.xlabel(ax=axd["D"], label="x, in meters")
        handles = [
            Patch(facecolor=REGION_COLORS[region], edgecolor="0.35", label=name)
            for region, name in REGION_NAMES.items()
        ]
        handles += [plt.Line2D([], [], color="#1f78b4", label="Stream"), well_handle()]
        explanation(fig, handles)
        save(fig, "generated_mesh.pdf")
        plt.close(fig)


def over_handles(limit, regions):
    """The explanation of a figure of triangles over an angle, with the
    regions its panels show."""
    return [
        Patch(facecolor="#e31a1c", label=f"Angle over {limit:g} degrees"),
        *(
            Patch(
                facecolor=REGION_COLORS[region],
                edgecolor="0.35",
                label=REGION_NAMES[region],
            )
            for region in regions
        ),
        plt.Line2D([], [], color="#1f78b4", label="Stream"),
        well_handle(),
    ]


def refine_figure(mesh, refined, windows):
    """Triangles over the maximum angle before and after refining."""
    with styles.USGSMap():
        fig, axd = plt.subplot_mosaic(
            [["A", "B"]], figsize=(6.7, 6.0), layout="constrained"
        )
        for letter, item, heading in (
            ("A", mesh, "Generated"),
            ("B", refined, "Refined"),
        ):
            draw_mesh(
                axd[letter], item, highlight=item.largest_angles() > MAXIMUM_ANGLE
            )
            for window_letter, window in windows.items():
                show_window(axd[letter], window, window_letter)
            styles.heading(ax=axd[letter], letter=letter, heading=heading)
            styles.xlabel(ax=axd[letter], label="x, in meters")
        styles.ylabel(ax=axd["A"], label="y, in meters")
        explanation(fig, over_handles(MAXIMUM_ANGLE, (LAKE_REGION,)))
        save(fig, "refined_mesh.pdf")
        plt.close(fig)


# a smallest angle below the generated mesh's, which improving may trade for
# fewer triangles over the improving angle, in degrees
TRADED_ANGLE = 25.0


def smallest_angle(mesh):
    """The smallest angle of a mesh in degrees."""
    corners = mesh.vertices[mesh.triangles]
    smallest = 180.0
    for corner in range(3):
        to_next = corners[:, (corner + 1) % 3] - corners[:, corner]
        to_previous = corners[:, (corner + 2) % 3] - corners[:, corner]
        cross = np.abs(
            to_next[:, 0] * to_previous[:, 1] - to_next[:, 1] * to_previous[:, 0]
        )
        dot = (to_next * to_previous).sum(axis=1)
        smallest = min(smallest, np.degrees(np.arctan2(cross, dot)).min())
    return smallest


def obtuse_kinds(mesh, limit):
    """The angles over the limit at a boundary point where two triangles
    meet, at another vertex that does not move (on a segment, or a well),
    and at a vertex that moves."""
    triangle_count = np.bincount(mesh.triangles.ravel(), minlength=len(mesh.vertices))
    on_boundary = np.zeros(len(mesh.vertices), dtype=bool)
    for triangle, corners in enumerate(mesh.triangles):
        for corner in range(3):
            if mesh.neighbors[triangle, corner] == -1:
                on_boundary[corners[(corner + 1) % 3]] = True
                on_boundary[corners[(corner + 2) % 3]] = True
    fixed = on_boundary.copy()
    fixed[mesh.segments.ravel()] = True
    fixed[mesh.fixed_vertices] = True
    kinds = {"boundary": 0, "fixed": 0, "free": 0}
    points = mesh.vertices[mesh.triangles]
    for triangle, corners in enumerate(mesh.triangles):
        for corner in range(3):
            at = points[triangle, corner]
            to_next = points[triangle, (corner + 1) % 3] - at
            to_previous = points[triangle, (corner + 2) % 3] - at
            angle = np.degrees(
                np.arctan2(
                    abs(to_next[0] * to_previous[1] - to_next[1] * to_previous[0]),
                    to_next @ to_previous,
                )
            )
            if angle <= limit:
                continue
            vertex = corners[corner]
            if on_boundary[vertex] and triangle_count[vertex] == 2:
                kinds["boundary"] += 1
            elif fixed[vertex]:
                kinds["fixed"] += 1
            else:
                kinds["free"] += 1
    return kinds


def improve_figure(refined, improved, traded, windows):
    """Triangles over the improving angle before and after improving, keeping
    the smallest angle and with a lower one."""
    with styles.USGSMap():
        fig, axd = plt.subplot_mosaic(
            [["A", "B", "C"]], figsize=(6.7, 4.6), layout="constrained"
        )
        panels = (
            ("A", refined, "Refined"),
            ("B", improved, "Improved"),
            ("C", traded, f"Improved, {TRADED_ANGLE:g}\u00b0 minimum"),
        )
        for letter, item, heading in panels:
            draw_mesh(
                axd[letter], item, highlight=item.largest_angles() > IMPROVE_ANGLE
            )
            for window_letter in ("B", "C"):
                show_window(axd[letter], windows[window_letter], window_letter)
            styles.heading(ax=axd[letter], letter=letter, heading=heading)
            styles.xlabel(ax=axd[letter], label="x, in meters")
        styles.ylabel(ax=axd["A"], label="y, in meters")
        explanation(fig, over_handles(IMPROVE_ANGLE, (LAKE_REGION,)))
        save(fig, "improved_mesh.pdf")
        plt.close(fig)


# the size of a figure of the three close-ups before and after a step, in
# inches
CLOSE_UP_FIGURE_SIZE = (5.4, 8.2)


def close_up_figure(before, after, windows, limit, steps, name):
    """The close-up windows before and after a step, with the triangles over
    an angle: a row for each window."""
    with styles.USGSMap():
        fig, axd = plt.subplot_mosaic(
            [["A", "B"], ["C", "D"], ["E", "F"]],
            figsize=CLOSE_UP_FIGURE_SIZE,
            layout="constrained",
        )
        rows = zip(("AB", "CD", "EF"), WINDOW_HEADINGS.items(), strict=True)
        for letters, (window, place) in rows:
            for letter, item, step in zip(letters, (before, after), steps, strict=True):
                draw_mesh(axd[letter], item, highlight=item.largest_angles() > limit)
                zoom(axd[letter], windows[window])
                styles.heading(
                    ax=axd[letter], letter=letter, heading=f"{place}, {step}"
                )
            styles.ylabel(ax=axd[letters[0]], label="y, in meters")
        for letter in ("E", "F"):
            styles.xlabel(ax=axd[letter], label="x, in meters")
        explanation(fig, over_handles(limit, (WELL_REGION, LAKE_REGION)), columns=3)
        save(fig, name)
        plt.close(fig)


def draw_grid(ax, grid, mesh, centers=False):
    """Draw the cells colored by region, the river and the lake's shore, the
    wells, and the cell centers if asked."""
    regions = grid.attributes[:, 0].astype(int)
    ax.add_collection(
        PolyCollection(
            [grid.vertices[cell] for cell in grid.cells],
            facecolors=[REGION_COLORS[region] for region in regions],
            edgecolors="0.35",
            linewidths=0.15,
        )
    )
    for segment, marker in zip(mesh.segments, mesh.segment_markers, strict=True):
        if marker == RIVER_MARKER:
            ax.plot(*mesh.vertices[segment].T, color="#1f78b4", lw=1.0)
        elif marker == LAKE_MARKER:
            ax.plot(*mesh.vertices[segment].T, color="#08306b", lw=0.6, ls="--")
    if centers:
        ax.plot(*grid.centers.T, marker=".", color="0.2", ms=1.2, ls="none")
    for x, y in WELLS:
        ax.plot(x, y, marker="o", color="black", ms=2.5, ls="none")
    ax.xaxis.set_major_formatter(THOUSANDS)
    ax.yaxis.set_major_formatter(THOUSANDS)
    ax.set_aspect("equal")
    ax.set_xlim(*X_LIMITS)
    ax.set_ylim(*Y_LIMITS)


def voronoi_figure(grid, mesh, windows):
    """The Voronoi grid of the valley, with close-ups of a well, the river,
    and the lake's shore."""
    with styles.USGSMap():
        fig, axd = valley_and_windows()
        draw_grid(axd["A"], grid, mesh)
        for letter, window in windows.items():
            show_window(axd["A"], window, letter)
        styles.heading(ax=axd["A"], letter="A", heading="Valley")
        styles.ylabel(ax=axd["A"], label="y, in meters")
        styles.xlabel(ax=axd["A"], label="x, in meters")
        for letter, heading in WINDOW_HEADINGS.items():
            draw_grid(axd[letter], grid, mesh, centers=True)
            zoom(axd[letter], windows[letter])
            styles.heading(ax=axd[letter], letter=letter, heading=heading)
        styles.xlabel(ax=axd["D"], label="x, in meters")
        handles = [
            Patch(facecolor=REGION_COLORS[region], edgecolor="0.35", label=name)
            for region, name in REGION_NAMES.items()
        ]
        handles += [
            plt.Line2D([], [], color="#1f78b4", label="Stream"),
            plt.Line2D([], [], color="#08306b", lw=0.6, ls="--", label="Lake shore"),
            plt.Line2D(
                [], [], color="0.2", marker=".", ms=3, ls="none", label="Cell center"
            ),
            well_handle(),
        ]
        explanation(fig, handles, columns=4)
        save(fig, "voronoi_grid.pdf")
        plt.close(fig)


# a window on the boundary at the valley's west corner, (x, y, side) in meters
BOUNDARY_WINDOW = (300.0, 4000.0, 800.0)
# the colors of each stream's cells, in turn, of the cell where the
# tributary joins the river, and of the boundary's cells
STREAM_CELL_COLORS = (("#6baed6", "#2171b5"), ("#74c476", "#238b45"))
CONFLUENCE_CELL_COLOR = "#e31a1c"
BOUNDARY_CELL_COLOR = "#fd8d3c"
WELL_CELL_COLOR = "#807dba"
# the side of the close-up around a well, small enough to show its cell,
# in meters
WELL_CELL_WINDOW = 150.0


def draw_features(ax, grid, mesh):
    """Draw the grid with each stream's cells in alternating shades, in
    order, the confluence's cell in red, the boundary's cells in orange, and
    the wells' cells in purple."""
    colors = ["#f0ece0"] * len(grid.cells)
    for cell in boundary_cells.cells:
        colors[cell] = BOUNDARY_CELL_COLOR
    for cell in well_cells:
        colors[cell] = WELL_CELL_COLOR
    for cells, shades in zip(stream_cells, STREAM_CELL_COLORS, strict=True):
        for reach, cell in enumerate(cells.cells):
            colors[cell] = shades[reach % 2]
    colors[stream_cells[1].cells[-1]] = CONFLUENCE_CELL_COLOR
    ax.add_collection(
        PolyCollection(
            [grid.vertices[cell] for cell in grid.cells],
            facecolors=colors,
            edgecolors="0.35",
            linewidths=0.15,
        )
    )
    for segment, marker in zip(mesh.segments, mesh.segment_markers, strict=True):
        if marker == RIVER_MARKER:
            ax.plot(*mesh.vertices[segment].T, color="black", lw=0.8)
    for x, y in WELLS:
        ax.plot(x, y, marker="o", color="black", ms=2.5, ls="none")
    ax.xaxis.set_major_formatter(THOUSANDS)
    ax.yaxis.set_major_formatter(THOUSANDS)
    ax.set_aspect("equal")
    ax.set_xlim(*X_LIMITS)
    ax.set_ylim(*Y_LIMITS)


def features_figure(grid, mesh, windows):
    """The streams', the boundary's, and the wells' cells in the Voronoi grid,
    with close-ups of the confluence, the boundary, and a well."""
    with styles.USGSMap():
        fig, axd = valley_and_windows()
        well_x, well_y, _ = windows["B"]
        close_ups = {
            "B": windows["C"],
            "C": BOUNDARY_WINDOW,
            "D": (well_x, well_y, WELL_CELL_WINDOW),
        }
        draw_features(axd["A"], grid, mesh)
        for letter, window in close_ups.items():
            show_window(axd["A"], window, letter)
        styles.heading(ax=axd["A"], letter="A", heading="Valley")
        styles.ylabel(ax=axd["A"], label="y, in meters")
        styles.xlabel(ax=axd["A"], label="x, in meters")
        for letter, heading in (
            ("B", "At the confluence"),
            ("C", "On the boundary"),
            ("D", "Around a well"),
        ):
            draw_features(axd[letter], grid, mesh)
            zoom(axd[letter], close_ups[letter])
            styles.heading(ax=axd[letter], letter=letter, heading=heading)
        styles.xlabel(ax=axd["D"], label="x, in meters")
        # the explanation fills by column: the river and the confluence, the
        # tributary and the boundary, then the wells and the lines
        (river_shades, tributary_shades) = STREAM_CELL_COLORS
        handles = [
            Patch(facecolor=river_shades[0], edgecolor="0.35", label="River cell"),
            Patch(facecolor=river_shades[1], edgecolor="0.35", label="Next river cell"),
            Patch(
                facecolor=CONFLUENCE_CELL_COLOR,
                edgecolor="0.35",
                label="Confluence cell",
            ),
            Patch(
                facecolor=tributary_shades[0], edgecolor="0.35", label="Tributary cell"
            ),
            Patch(
                facecolor=tributary_shades[1],
                edgecolor="0.35",
                label="Next tributary cell",
            ),
            Patch(
                facecolor=BOUNDARY_CELL_COLOR, edgecolor="0.35", label="Boundary cell"
            ),
            Patch(facecolor=WELL_CELL_COLOR, edgecolor="0.35", label="Well cell"),
            plt.Line2D([], [], color="black", lw=0.8, label="Stream"),
            well_handle(),
        ]
        explanation(fig, handles, columns=3)
        save(fig, "features.pdf")
        plt.close(fig)


# the point whose cell the connections figure shows, in meters
CONNECTION_POINT = (3000.0, 3000.0)
FACE_COLOR = "#e6550d"
DISTANCE_COLOR = "#2171b5"
CONNECTED_CELL_COLOR = "#c6dbef"


def draw_connections(ax, grid, connections, cell, window):
    """Draw the grid in a window around a cell, with each face the cell
    shares, the distance from its center to the face, the face's normal, and
    the line to the other cell's center."""
    colors = ["#f0ece0"] * len(grid.cells)
    colors[cell] = CONNECTED_CELL_COLOR
    ax.add_collection(
        PolyCollection(
            [grid.vertices[corners] for corners in grid.cells],
            facecolors=colors,
            edgecolors="0.35",
            linewidths=0.3,
        )
    )
    corners = grid.cells[cell].tolist()
    center = grid.centers[cell]
    start, end = connections.offsets[cell : cell + 2]
    for k in range(start, end):
        other = connections.cells[k]
        shared = set(grid.cells[other].tolist())
        for a, b in zip(corners, corners[1:] + corners[:1], strict=True):
            if a in shared and b in shared:
                ax.plot(*grid.vertices[[a, b]].T, color=FACE_COLOR, lw=1.6)
        angle = np.radians(connections.angles[k])
        normal = np.array([np.cos(angle), np.sin(angle)])
        foot = center + connections.distances[k] * normal
        ax.plot(*np.array([center, foot]).T, color=DISTANCE_COLOR, lw=1.0)
        ax.annotate(
            "",
            xy=foot + 0.12 * window[2] * normal,
            xytext=foot,
            arrowprops={"arrowstyle": "-|>", "color": "black", "lw": 0.6},
        )
        ax.plot(*np.array([center, grid.centers[other]]).T, color="0.3", lw=0.5, ls=":")
        ax.plot(*grid.centers[other], marker=".", color="0.2", ms=3, ls="none")
    ax.plot(*center, marker=".", color="0.2", ms=3, ls="none")
    ax.xaxis.set_major_formatter(THOUSANDS)
    ax.yaxis.set_major_formatter(THOUSANDS)
    ax.set_aspect("equal")
    zoom(ax, window)


def connections_figure(grid, triangles):
    """A cell of the triangle grid of a mesh and of its Voronoi grid, with its
    connections to the cells beside it."""
    with styles.USGSMap():
        fig, axd = plt.subplot_mosaic(
            [["A", "B"]], figsize=(6.7, 3.9), layout="constrained"
        )
        # both panels show the window twice the size of the Voronoi cell
        # nearest the point, so the triangles' corners are the cells' centers
        voronoi_cell = int(np.argmin(np.hypot(*(grid.centers - CONNECTION_POINT).T)))
        center = grid.centers[voronoi_cell]
        extent = np.ptp(grid.vertices[grid.cells[voronoi_cell]], axis=0).max()
        window = (*center, 2.0 * extent)
        for letter, g, heading in (
            ("A", triangles, "Triangle grid, centered on centroids"),
            ("B", grid, "Voronoi grid"),
        ):
            g_connections = connections if g is grid else tu.connect_cells(g)
            # the triangle whose centroid is nearest the Voronoi cell's center,
            # one of its corners
            cell = int(np.argmin(np.hypot(*(g.centers - center).T)))
            draw_connections(axd[letter], g, g_connections, cell, window)
            styles.heading(ax=axd[letter], letter=letter, heading=heading)
            styles.xlabel(ax=axd[letter], label="x, in meters")
        styles.ylabel(ax=axd["A"], label="y, in meters")
        handles = [
            Patch(facecolor=CONNECTED_CELL_COLOR, edgecolor="0.35", label="Cell"),
            plt.Line2D([], [], color=FACE_COLOR, lw=1.6, label="Shared face"),
            plt.Line2D([], [], color=DISTANCE_COLOR, lw=1.0, label="Distance to face"),
            plt.Line2D(
                [],
                [],
                color="black",
                lw=0.6,
                marker=">",
                ms=4,
                label="Face normal",
            ),
            plt.Line2D(
                [], [], color="0.3", lw=0.5, ls=":", label="Line between centers"
            ),
            plt.Line2D(
                [], [], color="0.2", marker=".", ms=3, ls="none", label="Cell center"
            ),
        ]
        explanation(fig, handles, columns=3)
        save(fig, "connections.pdf")
        plt.close(fig)


# the west-east line of the layers' section, through the lake's basin, in
# meters
SECTION_Y = float(np.mean(np.array(LAKE)[:, 1]))
SECTION = [(0.0, SECTION_Y), (5000.0, SECTION_Y)]
LAYER_COLORS = ("#fdd49e", "#8c6d31", "#fdae6b")
LAYER_NAMES = ("Upper aquifer", "Clay", "Lower aquifer")
PASS_THROUGH_COLOR = "#bdbdbd"
# the interval of the land surface's contours, in meters
CONTOUR_INTERVAL = 2.0


def shade_cells(ax, grid, values, cmap, limits, label, fig):
    """Shade the cells by a value, with a color bar below; cells whose value
    is nan are pass-through cells."""
    shading = PolyCollection(
        [grid.vertices[cell] for cell in grid.cells],
        array=values,
        cmap=cmap,
        edgecolors="face",
        linewidths=0.2,
    )
    shading.set_clim(*limits)
    shading.cmap.set_bad(PASS_THROUGH_COLOR)
    ax.add_collection(shading)
    ax.xaxis.set_major_formatter(THOUSANDS)
    ax.yaxis.set_major_formatter(THOUSANDS)
    ax.set_aspect("equal")
    ax.set_xlim(*X_LIMITS)
    ax.set_ylim(*Y_LIMITS)
    bar = fig.colorbar(shading, ax=ax, location="bottom", shrink=0.9, pad=0.02)
    bar.set_label(label)


def draw_streams_and_lake(ax, mesh):
    for segment, marker in zip(mesh.segments, mesh.segment_markers, strict=True):
        if marker == RIVER_MARKER:
            ax.plot(*mesh.vertices[segment].T, color="#1f78b4", lw=0.8)
        elif marker == LAKE_MARKER:
            ax.plot(*mesh.vertices[segment].T, color="#08306b", lw=0.6, ls="--")


def layers_figure(grid, mesh, layers):
    """The land surface, the clay's thickness with its pass-through cells,
    the base of the lower aquifer, and a west-east section of the layers."""
    clay = layers.botm[0] - layers.botm[1]
    passing = layers.idomain[1] < 0
    with styles.USGSMap():
        fig, axd = plt.subplot_mosaic(
            [["A", "B", "C"], ["D", "D", "D"]],
            figsize=(6.7, 7.6),
            layout="constrained",
            height_ratios=(2.2, 1.0),
        )
        shade_cells(
            axd["A"],
            grid,
            layers.top,
            "terrain",
            (0.0, np.ceil(layers.top.max())),
            "Land surface, in meters",
            fig,
        )
        levels = np.arange(0.0, layers.top.max(), CONTOUR_INTERVAL)
        axd["A"].tricontour(
            *grid.centers.T, layers.top, levels=levels, colors="0.3", linewidths=0.3
        )
        shade_cells(
            axd["B"],
            grid,
            np.where(passing, np.nan, clay),
            "YlOrBr",
            (0.0, CLAY_THICKNESS),
            "Clay thickness, in meters",
            fig,
        )
        shade_cells(
            axd["C"],
            grid,
            layers.botm[-1],
            "viridis",
            (np.floor(layers.botm[-1].min()), np.ceil(layers.botm[-1].max())),
            "Base of lower aquifer, in meters",
            fig,
        )
        for letter, heading in (
            ("A", "Land surface"),
            ("B", "Clay"),
            ("C", "Base of lower aquifer"),
        ):
            ax = axd[letter]
            draw_streams_and_lake(ax, mesh)
            ax.plot(*np.array(SECTION).T, color="black", lw=0.6)
            styles.heading(ax=ax, letter=letter, heading=heading)
            if letter != "A":
                ax.tick_params(labelleft=False)
        styles.ylabel(ax=axd["A"], label="y, in meters")

        ax = axd["D"]
        along = tu.cells_along_line(grid, SECTION)
        tops = np.vstack([layers.top, layers.botm[:-1]])
        for cell, start, length in zip(
            along.cells, along.starts, along.lengths, strict=True
        ):
            for layer in range(len(layers.botm)):
                through = layers.idomain[layer, cell] < 0
                ax.add_patch(
                    Rectangle(
                        (start, layers.botm[layer, cell]),
                        length,
                        tops[layer, cell] - layers.botm[layer, cell],
                        facecolor=PASS_THROUGH_COLOR
                        if through
                        else LAYER_COLORS[layer],
                        edgecolor="0.35",
                        linewidth=0.1,
                    )
                )
        ax.set_xlim(along.starts.min(), (along.starts + along.lengths).max())
        ax.set_ylim(
            np.floor(layers.botm[-1].min() / 10.0) * 10.0, layers.top.max() + 5.0
        )
        ax.xaxis.set_major_formatter(THOUSANDS)
        ax.text(0.01, 0.97, "A", transform=ax.transAxes, va="top", fontsize=7)
        ax.text(
            0.99,
            0.97,
            "A\u2032",
            transform=ax.transAxes,
            va="top",
            ha="right",
            fontsize=7,
        )
        styles.heading(ax=ax, letter="D", heading="Section A\u2013A\u2032")
        styles.xlabel(ax=ax, label="x, in meters")
        styles.ylabel(ax=ax, label="Elevation, in meters")
        handles = [
            Patch(facecolor=color, edgecolor="0.35", label=name)
            for color, name in zip(LAYER_COLORS, LAYER_NAMES, strict=True)
        ]
        handles += [
            Patch(
                facecolor=PASS_THROUGH_COLOR,
                edgecolor="0.35",
                label="Pass-through cell",
            ),
            plt.Line2D([], [], color="#1f78b4", lw=0.8, label="Stream"),
            plt.Line2D([], [], color="#08306b", lw=0.6, ls="--", label="Lake shore"),
            plt.Line2D([], [], color="black", lw=0.6, label="Line of section"),
        ]
        explanation(fig, handles, columns=4)
        save(fig, "layers.pdf")
        plt.close(fig)


def cover_figure(mesh):
    """The cover image: the refined mesh of the valley in dark colors, under
    the white title, without axes."""
    fig, ax = plt.subplots(figsize=(8.5, 6.0), facecolor="#1b2a2a")
    regions = mesh.attributes[:, 0].astype(int)
    shades = {AQUIFER: "#2f4a48", LAKE_REGION: "#1f4e79", WELL_REGION: "#7a5a1e"}
    ax.add_collection(
        PolyCollection(
            mesh.vertices[mesh.triangles],
            facecolors=[shades[region] for region in regions],
            edgecolors="#7fa3a0",
            linewidths=0.2,
        )
    )
    for segment, marker in zip(mesh.segments, mesh.segment_markers, strict=True):
        if marker == RIVER_MARKER:
            ax.plot(*mesh.vertices[segment].T, color="#6baed6", lw=1.2)
    ax.set_aspect("equal")
    ax.set_xlim(*X_LIMITS)
    ax.set_ylim(_y.min() - MARGIN, _y.max() + MARGIN)
    ax.set_axis_off()
    fig.savefig(
        FIGURES / "cover.png",
        dpi=200,
        bbox_inches="tight",
        facecolor=fig.get_facecolor(),
    )
    plt.close(fig)


def signed(value):
    """A number for the text, with a minus sign rather than a hyphen."""
    return f"{value:g}".replace("-", "$-$")


def main():
    FIGURES.mkdir(exist_ok=True)
    windows = choose_windows(mesh)
    mesh_figure(mesh, windows)
    refine_figure(mesh, refined, windows)
    close_up_figure(
        mesh, refined, windows, MAXIMUM_ANGLE, ("generated", "refined"), "close_ups.pdf"
    )
    traded = tu.improve(
        refined, maximum_angle=IMPROVE_ANGLE, minimum_angle=TRADED_ANGLE
    )
    improve_figure(refined, improved, traded, windows)
    close_up_figure(
        refined,
        improved,
        windows,
        IMPROVE_ANGLE,
        ("refined", "improved"),
        "improved_close_ups.pdf",
    )
    delaunay = delaunay_refined
    # the cells of the vertices on the lake's shore, which span both sides
    shore = np.unique(delaunay.segments[delaunay.segment_markers == LAKE_MARKER])
    voronoi_figure(grid, delaunay, windows)
    # each well is a vertex of the mesh, its ring's center, so its cell is
    # centered on it
    if not np.allclose(grid.centers[well_cells], WELLS):
        raise ValueError("a well's cell is not centered on the well")
    # the streams' marked segments hold the same cells and length as the
    # streams, though in chains that break at the confluence; the confluence
    # is the tributary's last cell, centered on the point it shares with the
    # river, and one of the river's
    on_segments = tu.cells_on_segments(grid, delaunay, RIVER_MARKER)
    along = np.concatenate([cells.cells for cells in stream_cells])
    if set(on_segments.cells) != set(along) or not np.isclose(
        on_segments.lengths.sum(), sum(cells.lengths.sum() for cells in stream_cells)
    ):
        raise ValueError("the streams' segments do not hold the streams' cells")
    confluence = stream_cells[1].cells[-1]
    if not np.allclose(grid.centers[confluence], TRIBUTARY[-1]) or (
        confluence not in stream_cells[0].cells
    ):
        raise ValueError("the confluence's cell is not where the streams meet")
    features_figure(grid, delaunay, windows)
    connections_figure(grid, tu.triangle_grid(delaunay))
    layers_figure(grid, delaunay, layers)
    cover_figure(refined)
    # the class puts this page in place of the agency logo on the cover
    blank = plt.figure(figsize=(1, 1))
    blank.savefig(FIGURES / "blank.pdf", transparent=True)
    plt.close(blank)

    # the values the text quotes, so they always match the figures
    values = {
        "voronoicells": f"{len(grid.cells):,}",
        "voronoivertices": f"{len(grid.vertices):,}",
        # each face is listed from both of its cells
        "voronoifaces": f"{len(connections.cells) // 2:,}",
        "delaunaytriangles": f"{len(delaunay.triangles):,}",
        "shorecells": len(shore),
        "shorelakecells": int((grid.attributes[shore, 0] == LAKE_REGION).sum()),
        "generatedvertices": len(mesh.vertices),
        "generatedtriangles": len(mesh.triangles),
        "generatedover": int((mesh.largest_angles() > MAXIMUM_ANGLE).sum()),
        "generatedlargest": f"{mesh.largest_angles().max():.1f}",
        "refinedtriangles": len(refined.triangles),
        "refinedover": int((refined.largest_angles() > MAXIMUM_ANGLE).sum()),
        "refinedlargest": f"{refined.largest_angles().max():.1f}",
        "improveangle": f"{IMPROVE_ANGLE:g}",
        "refinedoverninety": int((refined.largest_angles() > IMPROVE_ANGLE).sum()),
        "improvedoverninety": int((improved.largest_angles() > IMPROVE_ANGLE).sum()),
        "improvedboundaryover": obtuse_kinds(improved, IMPROVE_ANGLE)["boundary"],
        "improvedfixedover": obtuse_kinds(improved, IMPROVE_ANGLE)["fixed"],
        "improvedfreeover": obtuse_kinds(improved, IMPROVE_ANGLE)["free"],
        "refinedsmallest": f"{smallest_angle(refined):.1f}",
        "improvedsmallest": f"{smallest_angle(improved):.1f}",
        "tradedangle": f"{TRADED_ANGLE:g}",
        "tradedoverninety": int((traded.largest_angles() > IMPROVE_ANGLE).sum()),
        "tradedboundaryover": obtuse_kinds(traded, IMPROVE_ANGLE)["boundary"],
        "tradedfixedover": obtuse_kinds(traded, IMPROVE_ANGLE)["fixed"],
        "tradedfreeover": obtuse_kinds(traded, IMPROVE_ANGLE)["free"],
        "tradedsmallest": f"{smallest_angle(traded):.1f}",
        "valleyarea": f"{mesh.triangle_areas().sum() / 1e6:.2f}",
        # the merge tolerance, 1e-12 of the largest coordinate, in micrometers
        "valleytolerance": f"{1e-12 * np.abs(np.array(BOUNDARY)).max() * 1e6:.3f}",
        "boundaryspacing": f"{BOUNDARY_SPACING:,.0f}",
        "riverspacing": f"{RIVER_SPACING:,.0f}",
        "lakespacing": f"{LAKE_SPACING:,.0f}",
        "maximumarea": f"{MAXIMUM_AREA:,.0f}",
        "lakearea": f"{LAKE_AREA:,.0f}",
        "wellradius": f"{WELL_RADIUS:,.0f}",
        "wellpoints": f"{WELL_POINTS}",
        "wellarea": f"{WELL_AREA:,.0f}",
        "minimumangle": f"{MINIMUM_ANGLE:g}",
        "maximumangle": f"{MAXIMUM_ANGLE:g}",
        "wellx": f"{windows['B'][0]:,.0f}",
        "welly": f"{windows['B'][1]:,.0f}",
        **valley_section_lines(),
        "boundarymarker": BOUNDARY_MARKER,
        "rivercells": len(stream_cells[0].cells),
        "riverlength": f"{stream_cells[0].lengths.sum():,.0f}",
        "tributarycells": len(stream_cells[1].cells),
        "tributarylength": f"{stream_cells[1].lengths.sum():,.0f}",
        "boundarycells": len(boundary_cells.cells),
        "boundarylength": f"{boundary_cells.lengths.sum():,.0f}",
        "lakewindowover": over_in(mesh, MAXIMUM_ANGLE, windows["D"]),
        "lakewindowrefinedover": over_in(refined, MAXIMUM_ANGLE, windows["D"]),
        "lakewindowrefinedoverninety": over_in(refined, IMPROVE_ANGLE, windows["D"]),
        "lakewindowimprovedover": over_in(improved, IMPROVE_ANGLE, windows["D"]),
        "lakewindow": f"{LAKE_WINDOW[2]:,.0f}",
        "wellwindow": f"{WELL_WINDOW:,.0f}",
        "riverwindow": f"{RIVER_WINDOW:,.0f}",
        "wellwindowrefinedoverninety": int(
            in_window(
                centroids(refined)[refined.largest_angles() > IMPROVE_ANGLE],
                windows["B"],
            ).sum()
        ),
        "riverwindowrefinedoverninety": int(
            in_window(
                centroids(refined)[refined.largest_angles() > IMPROVE_ANGLE],
                windows["C"],
            ).sum()
        ),
        "wellwindowimprovedover": int(
            in_window(
                centroids(improved)[improved.largest_angles() > IMPROVE_ANGLE],
                windows["B"],
            ).sum()
        ),
        "riverwindowimprovedover": int(
            in_window(
                centroids(improved)[improved.largest_angles() > IMPROVE_ANGLE],
                windows["C"],
            ).sum()
        ),
        "wellwindowover": int(
            in_window(
                centroids(mesh)[mesh.largest_angles() > MAXIMUM_ANGLE], windows["B"]
            ).sum()
        ),
        "riverwindowover": int(
            in_window(
                centroids(mesh)[mesh.largest_angles() > MAXIMUM_ANGLE], windows["C"]
            ).sum()
        ),
        "wellwindowrefinedover": int(
            in_window(
                centroids(refined)[refined.largest_angles() > MAXIMUM_ANGLE],
                windows["B"],
            ).sum()
        ),
        "riverwindowrefinedover": int(
            in_window(
                centroids(refined)[refined.largest_angles() > MAXIMUM_ANGLE],
                windows["C"],
            ).sum()
        ),
        "rivermouth": f"{RIVER_MOUTH_ELEVATION:g}",
        "landsurfacemax": f"{layers.top.max():.0f}",
        "ridgeheight": f"{RIDGE_HEIGHT:g}",
        "lakedepth": f"{LAKE_DEPTH:g}",
        "upperbottom": signed(UPPER_BOTTOM),
        "contourinterval": f"{CONTOUR_INTERVAL:g}",
        "claythickness": f"{CLAY_THICKNESS:g}",
        "clayedge": f"{CLAY_EDGE:,.0f}",
        "basewest": signed(BASE_WEST),
        "baseeast": signed(BASE_EAST),
        "minimumthickness": f"{MINIMUM_THICKNESS:g}",
        "passthroughcells": int((layers.idomain < 0).sum()),
        "disvcells": f"{layers.idomain.size:,}",
        "disunodes": f"{len(layers.disu.cells):,}",
        "disuconnections": f"{len(layers.disu.ja):,}",
        "sectiony": f"{SECTION_Y:,.0f}",
        # without thousands separators, for the command-line example
        "maximumareavalue": f"{MAXIMUM_AREA:.0f}",
        "boundaryspacingvalue": f"{BOUNDARY_SPACING:.0f}",
        "tuversion": tu.version()[0],
        "triangleversion": tu.version()[1],
        "trianglerelease": tu.version()[2],
    }
    lines = [
        f"\\newcommand{{\\{name}}}{{{value:,}}}"
        if isinstance(value, int)
        else f"\\newcommand{{\\{name}}}{{{value}}}"
        for name, value in values.items()
    ]
    (HERE / "values.tex").write_text(
        "% written by make_figures.py\n" + "\n".join(lines) + "\n"
    )
    for name, value in values.items():
        print(f"{name}: {value}")


if __name__ == "__main__":
    main()
