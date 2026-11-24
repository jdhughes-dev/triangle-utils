"""
Make the user guide's figures and the values its text quotes.

Meshes an example valley with triangle-utils (the library `pixi run build`
installs) and writes the figures to Figures/ and the quoted values to
values.tex, so the guide rebuilds from the repository alone.
"""

import sys
from pathlib import Path

import flopy.plot.styles as styles
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
from matplotlib.colors import to_rgba
from matplotlib.patches import Patch

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
RIVER = [
    (1800, 7800),
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

# region attributes
AQUIFER, LAKE_REGION, WELL_REGION = 1, 2, 3
REGION_NAMES = {AQUIFER: "Aquifer", LAKE_REGION: "Lake", WELL_REGION: "Well ring"}
REGION_COLORS = {AQUIFER: "#e8e3d3", LAKE_REGION: "#a6cee3", WELL_REGION: "#fdbf6f"}

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
    """Draw the triangles colored by region, and the river and boundary."""
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
    ax.set_aspect("equal")
    ax.set_xlim(-100, 4700)
    # room above the valley for the explanation
    ax.set_ylim(-100, 9600)


def mesh_figure(mesh):
    """The generated mesh of the valley, and the mesh around a well."""
    with styles.USGSMap():
        fig, axd = plt.subplot_mosaic(
            [["A", "B"]], figsize=(6.7, 6.0), layout="constrained"
        )
        draw_mesh(axd["A"], mesh)
        styles.heading(ax=axd["A"], letter="A", heading="Valley")
        styles.xlabel(ax=axd["A"], label="x, in meters")
        styles.ylabel(ax=axd["A"], label="y, in meters")
        draw_mesh(axd["B"], mesh)
        x, y = WELLS[0]
        # the valley panel's shape, 4,800 by 9,700 m, at a tenth of its size
        axd["B"].set_xlim(x - 240, x + 240)
        axd["B"].set_ylim(y - 485, y + 485)
        axd["B"].plot(x, y, marker="o", color="black", ms=3)
        styles.heading(ax=axd["B"], letter="B", heading="Around a well")
        styles.xlabel(ax=axd["B"], label="x, in meters")
        handles = [
            Patch(facecolor=REGION_COLORS[region], edgecolor="0.35", label=name)
            for region, name in REGION_NAMES.items()
        ]
        handles.append(plt.Line2D([], [], color="#1f78b4", label="River"))
        styles.graph_legend(
            ax=axd["A"],
            handles=handles,
            labels=[h.get_label() for h in handles],
            loc="upper right",
        )
        fig.savefig(FIGURES / "generated_mesh.pdf")
        plt.close(fig)


def refine_figure(mesh, refined):
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
            styles.heading(ax=axd[letter], letter=letter, heading=heading)
            styles.xlabel(ax=axd[letter], label="x, in meters")
        styles.ylabel(ax=axd["A"], label="y, in meters")
        handles = [
            Patch(facecolor="#e31a1c", label=f"Angle over {MAXIMUM_ANGLE:g} degrees")
        ]
        styles.graph_legend(
            ax=axd["A"],
            handles=handles,
            labels=[h.get_label() for h in handles],
            loc="upper right",
        )
        fig.savefig(FIGURES / "refined_mesh.pdf")
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
    ax.set_xlim(-100, 4700)
    ax.set_ylim(-100, 8100)
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
    mesh_figure(mesh)
    refine_figure(mesh, refined)
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
