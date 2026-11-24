"""
Make the user guide's figures and the values its text quotes.

Meshes an example valley with triangle-utils (the library `pixi run build`
installs) and writes the figures to Figures/ and the quoted values to
values.tex, so the guide rebuilds from the repository alone.
"""

import sys
from itertools import pairwise
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

FIGURES = HERE / "Figures"

# the example valley, in meters: its boundary, a river that crosses it from
# north to south, a lake, and three pumping wells
BOUNDARY = [
    (0, 0),
    (4000, 0),
    (4600, 2200),
    (4200, 6000),
    (2600, 8000),
    (600, 7600),
    (0, 4000),
]
# the river starts and ends on boundary edges, between their points
RIVER = [
    (1800, 7840),
    (1900, 6500),
    (2300, 5200),
    (2100, 3800),
    (2500, 2400),
    (2600, 1000),
    (2400, 0),
]
LAKE = [(700, 5200), (1300, 4700), (1500, 5400), (1200, 6200), (700, 6000)]
WELLS = [(3300, 4400), (1000, 2000), (3400, 1500)]

# the spacing of points along each feature, in meters
BOUNDARY_SPACING = 200.0
RIVER_SPACING = 50.0
LAKE_SPACING = 60.0
# the largest triangle area in the aquifer and the lake, in square meters
MAXIMUM_AREA = 200.0 * 200.0
LAKE_AREA = 60.0 * 60.0
# each well's ring: radius (m), points, and largest triangle area (m2)
WELL_RADIUS = 50.0
WELL_POINTS = 16
WELL_AREA = 300.0
# the smallest angle of the generated mesh, and the largest angle refining
# aims for, in degrees
MINIMUM_ANGLE = 30.0
MAXIMUM_ANGLE = 100.0

# the axis limits of a valley panel: a margin around the boundary, and room
# above it for the explanation
_x, _y = np.array(BOUNDARY, dtype=float).T
MARGIN = 100.0
X_LIMITS = (_x.min() - MARGIN, _x.max() + MARGIN)
Y_LIMITS = (_y.min() - MARGIN, _y.max() + 0.3 * (_y.max() - _y.min()))
# the sides of the square close-ups around a well and along the river, in
# meters
WELL_WINDOW = 600.0
RIVER_WINDOW = 800.0

# region attributes
AQUIFER, LAKE_REGION, WELL_REGION = 1, 2, 3
REGION_NAMES = {AQUIFER: "Aquifer", LAKE_REGION: "Lake", WELL_REGION: "Well ring"}
REGION_COLORS = {AQUIFER: "#e8e3d3", LAKE_REGION: "#a6cee3", WELL_REGION: "#fdbf6f"}

# tick labels with thousands separators, as the text writes numbers
THOUSANDS = FuncFormatter(lambda value, _: f"{value:,.0f}")

# segment markers
BOUNDARY_MARKER, RIVER_MARKER, LAKE_MARKER, WELL_MARKER = 1, 2, 3, 4


def valley_geometry():
    """The example valley's geometry."""
    geometry = tu.Geometry()
    geometry.add_polygon(BOUNDARY, spacing=BOUNDARY_SPACING, marker=BOUNDARY_MARKER)
    geometry.add_line(RIVER, spacing=RIVER_SPACING, marker=RIVER_MARKER)
    geometry.add_polygon(LAKE, spacing=LAKE_SPACING, marker=LAKE_MARKER)
    # the river divides the aquifer, so it needs a region on each side
    geometry.add_region(500, 1000, attribute=AQUIFER)
    geometry.add_region(4000, 1000, attribute=AQUIFER)
    geometry.add_region(1100, 5400, attribute=LAKE_REGION, area_limit=LAKE_AREA)
    for x, y in WELLS:
        geometry.add_ring(
            x, y, WELL_RADIUS, WELL_POINTS, WELL_REGION, WELL_AREA, WELL_MARKER
        )
    return geometry


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
    """The close-up windows: the well, and the place along the river, whose
    window holds the most triangles over the maximum angle."""
    over = centroids(mesh)[mesh.largest_angles() > MAXIMUM_ANGLE]
    wells = [(x, y, WELL_WINDOW) for x, y in WELLS]
    well = max(wells, key=lambda window: in_window(over, window).sum())
    # candidate centers every river spacing along the river
    along = []
    for start, end in pairwise(RIVER):
        pieces = max(
            1,
            int(
                np.ceil(np.hypot(end[0] - start[0], end[1] - start[1]) / RIVER_SPACING)
            ),
        )
        for piece in range(pieces):
            fraction = piece / pieces
            along.append(
                (
                    start[0] + fraction * (end[0] - start[0]),
                    start[1] + fraction * (end[1] - start[1]),
                    RIVER_WINDOW,
                )
            )
    river = max(along, key=lambda window: in_window(over, window).sum())
    return {"B": well, "C": river}


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


def explanation(ax, handles):
    styles.graph_legend(
        ax=ax,
        handles=handles,
        labels=[h.get_label() for h in handles],
        loc="upper right",
    )


def well_handle():
    return plt.Line2D(
        [], [], color="black", marker="o", ms=2.5, ls="none", label="Pumping well"
    )


def mesh_figure(mesh, windows):
    """The generated mesh of the valley, with close-ups of a well and the river."""
    with styles.USGSMap():
        fig, axd = plt.subplot_mosaic(
            [["A", "B"], ["A", "C"]], figsize=(6.7, 6.6), layout="constrained"
        )
        draw_mesh(axd["A"], mesh)
        for letter, window in windows.items():
            show_window(axd["A"], window, letter)
        styles.heading(ax=axd["A"], letter="A", heading="Valley")
        styles.ylabel(ax=axd["A"], label="y, in meters")
        styles.xlabel(ax=axd["A"], label="x, in meters")
        for letter, heading in (("B", "Around a well"), ("C", "Along the river")):
            draw_mesh(axd[letter], mesh)
            zoom(axd[letter], windows[letter])
            styles.heading(ax=axd[letter], letter=letter, heading=heading)
        styles.xlabel(ax=axd["C"], label="x, in meters")
        handles = [
            Patch(facecolor=REGION_COLORS[region], edgecolor="0.35", label=name)
            for region, name in REGION_NAMES.items()
        ]
        handles += [plt.Line2D([], [], color="#1f78b4", label="River"), well_handle()]
        explanation(axd["A"], handles)
        fig.savefig(FIGURES / "generated_mesh.pdf")
        plt.close(fig)


def over_handles():
    return [
        Patch(facecolor="#e31a1c", label=f"Angle over {MAXIMUM_ANGLE:g} degrees"),
        Patch(facecolor=REGION_COLORS[LAKE_REGION], edgecolor="0.35", label="Lake"),
        plt.Line2D([], [], color="#1f78b4", label="River"),
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
        explanation(axd["A"], over_handles())
        fig.savefig(FIGURES / "refined_mesh.pdf")
        plt.close(fig)


def close_up_figure(mesh, refined, windows):
    """The close-up windows before and after refining."""
    with styles.USGSMap():
        fig, axd = plt.subplot_mosaic(
            [["A", "B"], ["C", "D"]], figsize=(6.7, 6.9), layout="constrained"
        )
        panels = (
            ("A", mesh, "B", "Around a well, generated"),
            ("B", refined, "B", "Around a well, refined"),
            ("C", mesh, "C", "Along the river, generated"),
            ("D", refined, "C", "Along the river, refined"),
        )
        for letter, item, window, heading in panels:
            draw_mesh(
                axd[letter], item, highlight=item.largest_angles() > MAXIMUM_ANGLE
            )
            zoom(axd[letter], windows[window])
            styles.heading(ax=axd[letter], letter=letter, heading=heading)
        for letter in ("A", "C"):
            styles.ylabel(ax=axd[letter], label="y, in meters")
        for letter in ("C", "D"):
            styles.xlabel(ax=axd[letter], label="x, in meters")
        fig.savefig(FIGURES / "close_ups.pdf")
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


def main():
    FIGURES.mkdir(exist_ok=True)
    mesh = tu.generate(
        valley_geometry(), maximum_area=MAXIMUM_AREA, minimum_angle=MINIMUM_ANGLE
    )
    refined = tu.refine(mesh, minimum_angle=MINIMUM_ANGLE, maximum_angle=MAXIMUM_ANGLE)
    windows = choose_windows(mesh)
    mesh_figure(mesh, windows)
    refine_figure(mesh, refined, windows)
    close_up_figure(mesh, refined, windows)
    cover_figure(refined)
    # the class puts this page in place of the agency logo on the cover
    blank = plt.figure(figsize=(1, 1))
    blank.savefig(FIGURES / "blank.pdf", transparent=True)
    plt.close(blank)

    # the values the text quotes, so they always match the figures
    values = {
        "generatedvertices": len(mesh.vertices),
        "generatedtriangles": len(mesh.triangles),
        "generatedover": int((mesh.largest_angles() > MAXIMUM_ANGLE).sum()),
        "generatedlargest": f"{mesh.largest_angles().max():.1f}",
        "refinedtriangles": len(refined.triangles),
        "refinedover": int((refined.largest_angles() > MAXIMUM_ANGLE).sum()),
        "refinedlargest": f"{refined.largest_angles().max():.1f}",
        "valleyarea": f"{mesh.triangle_areas().sum() / 1e6:.2f}",
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
        "wellwindow": f"{WELL_WINDOW:,.0f}",
        "riverwindow": f"{RIVER_WINDOW:,.0f}",
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
