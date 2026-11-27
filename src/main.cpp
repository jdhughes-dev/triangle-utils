// The triangle-utils command line: reads Triangle's files, calls the C API,
// and writes the results.

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "error.hpp"
#include "modflow_files.hpp"
#include "triangle_files.hpp"
#include "triangle_utils.h"

namespace
{

constexpr int EXIT_OK = 0;
constexpr int EXIT_ERROR = 1;
constexpr int EXIT_USAGE = 2;

constexpr const char * PROGRAM = "triangle-utils";

constexpr const char * USAGE = R"(usage: triangle-utils <command> [options]
       triangle-utils -h | --help | --version | --license

Commands:
  check     check a mesh in Triangle's .node, .ele, and .neigh files
  generate  mesh the geometry in a .poly file
  refine    refine a mesh in Triangle's files by area and angle
  improve   improve a mesh in Triangle's files by moving vertices
  disv      write a layered MODFLOW 6 DISV grid of a mesh in Triangle's files
  disu      write a layered MODFLOW 6 or MODFLOW-USG DISU grid of a mesh
  cells     find the cells a line or marked segments pass through

Options:
  -h, --help    show this help
  --version     show the versions of triangle-utils, Triangle, and the
                MODFLOW-ORG/triangle release
  --license     show the license and Triangle's copyright and terms

Run 'triangle-utils <command> -h' for the options of a command.
)";

constexpr const char * CHECK_USAGE = R"(usage: triangle-utils check [-h] <base>

Check the mesh in <base>.node and <base>.ele, and its neighbors in
<base>.neigh if that file exists: the triangles are counterclockwise with a
positive area, and each neighbor is the triangle across its edge.

Options:
  -h, --help    show this help
)";

constexpr const char * GENERATE_USAGE = R"(usage: triangle-utils generate [options] <base>

Mesh the geometry in <base>.poly (with its vertices in <base>.node if the
.poly file has none): its segments, which the mesh follows, its vertices,
holes, and regions; and the features of shapefiles. Write the mesh to
<output>.node, .ele, .neigh, and .poly, without vertex attributes; the
vertices of the .poly file and the shapefiles' points, such as wells, have
marker 1, so improving keeps them in place, and the vertices Triangle adds
have 0. With a shapefile, <base>.poly is read only if it exists.

A shapefile's polygons are boundaries or internal boundaries the mesh
follows, its polygons' holes are not meshed, and its lines, such as rivers,
are followed; its points are vertices (-f), regions (-r), or points inside
holes (-H). --marker, --feature-spacing, --attribute, and --area-limit set
the values of the next shapefile, each a number or the name of a field of
its .dbf file: the marker of the segments, the spacing of the
points added along them, and a region's attribute and area limit (0, none).
Coordinates are taken as given; the .prj file is not read.

Options:
  -h, --help                show this help
  -o, --output <output>     the output base name (default <base>.1, or
                            x.2 for x.1), which must not be <base>
  -a, --maximum-area <a>    the largest triangle area (default 0, none)
  -q, --minimum-angle <q>   the smallest angle in degrees, at most 34
                            (default 0, none)
  -s, --spacing <s>         add points along the segments so none is
                            farther than s from the next (default 0, none)
  -m, --merge-distance <d>  merge points closer than d, and split a segment
                            a point is that close to (default 0: 1e-12 of
                            the largest coordinate)
  -D, --conforming-delaunay make every triangle Delaunay, as a Voronoi grid
                            needs
  -f, --features <shp>      add a shapefile's features, its points as
                            vertices
  -r, --regions <shp>       add a shapefile's points as regions
  -H, --holes <shp>         add a shapefile's points as holes
  --marker <m>              the marker of the next shapefile (default 0)
  --feature-spacing <s>     its spacing (default 0, none)
  --attribute <a>           its regions' attribute (default 0)
  --area-limit <l>          its regions' area limit (default 0, none)
)";

constexpr const char * REFINE_USAGE = R"(usage: triangle-utils refine [options] <base>

Refine the mesh in <base>.node and <base>.ele, keeping its boundary, the
segments in <base>.poly if it exists (each an edge of the mesh), and its
triangle attributes where segments bound them; triangles are split to meet
their limits in <base>.area if it exists, which, like the attributes, are
averaged across edges Triangle flips where no segment bounds them. Write the
mesh to <output>.node, .ele, .neigh, and .poly, without vertex attributes,
holes, or regions; a vertex with a nonzero marker in <base>.node, such as a
well, has marker 1, so improving keeps it in place.

Options:
  -h, --help                show this help
  -o, --output <output>     the output base name (default <base>.1, or
                            x.2 for x.1), which must not be <base>
  -a, --maximum-area <a>    the largest triangle area (default 0, none)
  -q, --minimum-angle <q>   the smallest angle in degrees, at most 34
                            (default 0, none)
  -A, --maximum-angle <A>   the largest angle in degrees, over 60 and under
                            180 (default 0, none); Triangle can reach about
                            95 to 100, not 90
  -n, --angle-splits <n>    the most times triangles are flagged for
                            splitting for the maximum angle (default 100000)
  -D, --conforming-delaunay make every triangle Delaunay
)";

constexpr const char * IMPROVE_USAGE = R"(usage: triangle-utils improve [options] <base>

Improve the mesh in <base>.node and <base>.ele by moving its vertices and
flipping edges, keeping in place the vertices on the segments of <base>.poly
if it exists, on the boundary, where triangles of different attributes or
area limits meet, and with a nonzero marker in <base>.node, such as wells;
no triangle grows beyond its limit in <base>.area if it exists. Write the
mesh to <output>.node, .ele, .neigh, and .poly.

Options:
  -h, --help                show this help
  -o, --output <output>     the output base name (default <base>.1, or
                            x.2 for x.1), which must not be <base>
  -A, --maximum-angle <A>   the largest angle in degrees to aim for, over 60
                            and under 180, such as 90 (default 0: improve
                            the smallest angle only)
  -q, --minimum-angle <q>   the smallest angle in degrees a move may leave,
                            up to 60 (default 0: the mesh's own, so it
                            does not decrease); lower trades smaller angles
                            for fewer over the maximum angle
  -s, --smoothing <s>       laplacian (default) or centroidal
  -n, --iterations <n>      the most sweeps over the vertices (default 100)
  -t, --tolerance <t>       stop when no vertex moves farther than t times
                            the mean edge length (default 1e-4)
)";

constexpr const char * DISV_USAGE = R"(usage: triangle-utils disv [options] <base>

Build a grid from the mesh in <base>.node and <base>.ele and write it, with
its layers, to <output>.disv for MODFLOW 6. The Voronoi grid has a cell
around each vertex, centered on it, so the line between two cells' centers
is perpendicular to their shared face; its mesh must be conforming Delaunay
(generate or refine with -D). A cell on the boundary is centered on its
edge, and is not convex where the boundary turns inward. A cell on a segment
inside the mesh, such as a lake's shore, spans both sides and takes the
attributes of the side with most of its area. A triangle grid has the
triangles as its cells. Each cell's vertex or triangle, numbered as in
<base>.node or <base>.ele, and its attributes are written to <output>.cells.

The top and each bottom is a number, or a file of numbers separated by white
space, with # comments: one for each cell, or one for each vertex of the
mesh, interpolated at the cells' centers; a file with one value for each
cell is read as such, and a Voronoi grid's cells are its vertices. Give -b
once for each layer, from the top down; -i, if given, once for each layer as
well, a whole number or a file of one for each cell: > 0 active, 0 not in the
model, and < 0 a vertical pass-through cell. With -m above 0, an active cell
thinner than the minimum thickness becomes a pass-through cell, and a bottom
above its top moves up to it; otherwise an active cell not thicker than 0
is an error.

Options:
  -h, --help              show this help
  -o, --output <output>   the output base name (default <base>)
  -g, --grid <g>          voronoi (default), or centroid or circumcenter
                          for the triangles centered on either
  -t, --top <top>         the top of the first layer (default 1)
  -b, --bottom <bottom>   the bottom of a layer (default one layer, 0)
  -i, --idomain <i>       the idomain of a layer (default 1)
  -m, --minimum-thickness <t>
                          the thinnest active cell (default 0, none)
)";

constexpr const char * DISU_USAGE = R"(usage: triangle-utils disu [options] <base>

Build a grid from the mesh in <base>.node and <base>.ele, as disv does, and
write its layers to <output>.disu: a node for each active cell, numbered
layer by layer, connected to the active cells beside it in its layer and
to the active cells above and below it, through any pass-through cells
between, as MODFLOW 6 connects DISV cells. Each node's layer and cell,
numbered as in the DISV file, are written to <output>.nodes, and each
cell's vertex or triangle and its attributes to <output>.cells. The layers'
options are those of disv.

The MODFLOW 6 file has each node's cell in its VERTICES and CELL2D blocks.
The MODFLOW-USG file, which MODFLOW-USG-Transport also reads, has one
steady-state stress period, with a length of 1, and leaves the time and
length units undefined. The angles of the horizontal connections, which
anisotropy and dispersion need, go in MODFLOW-USG's flow and transport
packages, in radians, rather than in DISU; ANGLDEGX of the MODFLOW 6 file
gives them in degrees.

Options:
  -h, --help              show this help
  -o, --output <output>   the output base name (default <base>)
  -g, --grid <g>          voronoi (default), or centroid or circumcenter
  -f, --format <f>        mf6 (default) for MODFLOW 6, or usg for
                          MODFLOW-USG and MODFLOW-USG-Transport
  -t, --top <top>         the top of the first layer (default 1)
  -b, --bottom <bottom>   the bottom of a layer (default one layer, 0)
  -i, --idomain <i>       the idomain of a layer (default 1)
  -m, --minimum-thickness <t>
                          the thinnest active cell (default 0, none)
)";

constexpr const char * CELLS_USAGE = R"(usage: triangle-utils cells [options] <base>

Find the cells of the grid of the mesh in <base>.node and <base>.ele that
points contain, or that a line or the segments of <base>.poly with a marker
pass through. For points, such as wells, print each point's number and its
cell, numbered from 1 as in the DISV file, or 0 outside the grid; a point on
a face or corner is in the lowest-numbered cell there. For a line or
segments, print each cell, where its part starts along the line or segments,
and its length. A .shp file of lines prints the cells of each line in turn,
and a .shp file of points those of its points. Along a line, such as a river, the cells are in order, and a
cell the line enters twice is printed twice; a part along a face is in the
cell on the line's left, or on its right where none is on the left. On
segments, such as a boundary, each cell is printed once, with its length of
segment summed, in the order the segments were digitized; a segment between
two cells is in only one of them.

Options:
  -h, --help              show this help
  -g, --grid <g>          voronoi (default), or centroid or circumcenter
                          for the triangles, as for disv
  -l, --line <file>       a line: x and y of a point on each line of the
                          file, with # comments, or a shapefile's lines
  -p, --points <file>     points: x and y of a point on each line of the
                          file, with # comments, or a shapefile's points
  -m, --marker <m>        the segments of <base>.poly with marker m
)";

// A command line that cannot be run as given.
class UsageError : public std::runtime_error
{
public:
    explicit UsageError(const std::string & message) : std::runtime_error(message) {}
};

// Throw Error with the C API's reason if a call did not succeed.
void check_status(int status)
{
    if (status != TRIANGLE_UTILS_OK) throw triangle_utils::Error(triangle_utils_last_error());
}

struct MeshFree
{
    void operator()(TriangleUtilsMesh * mesh) const { triangle_utils_mesh_free(mesh); }
};

using MeshHandle = std::unique_ptr<TriangleUtilsMesh, MeshFree>;

int check_command(const std::vector<std::string> & arguments)
{
    std::vector<std::string> bases;
    for (const std::string & argument : arguments)
    {
        if (argument == "-h" || argument == "--help")
        {
            std::cout << CHECK_USAGE;
            return EXIT_OK;
        }
        if (argument.size() > 1 && argument[0] == '-')
            throw UsageError(triangle_utils::message("check: unknown option ", argument));
        bases.push_back(argument);
    }
    if (bases.size() != 1) throw UsageError("check: give one <base>");
    const std::string & base = bases[0];

    const std::string node_name = base + ".node";
    std::ifstream node_file(node_name);
    if (!node_file) throw triangle_utils::Error(triangle_utils::message("cannot open ", node_name));
    const triangle_utils::VertexFile vertices = triangle_utils::read_node(node_file, node_name);

    const std::string ele_name = base + ".ele";
    std::ifstream ele_file(ele_name);
    if (!ele_file) throw triangle_utils::Error(triangle_utils::message("cannot open ", ele_name));
    const triangle_utils::TriangleFile triangles =
        triangle_utils::read_ele(ele_file, ele_name, vertices);

    std::vector<double> vertex_xy;
    for (const triangle_utils::Point & vertex : vertices.vertices)
    {
        vertex_xy.push_back(vertex.x);
        vertex_xy.push_back(vertex.y);
    }
    std::vector<int> corners;
    for (const auto & triangle : triangles.triangles)
    {
        corners.insert(corners.end(), triangle.begin(), triangle.end());
    }
    // the file readers limit the counts to what an int holds
    const int vertex_count = static_cast<int>(vertices.vertices.size());
    const int triangle_count = static_cast<int>(triangles.triangles.size());
    if (triangle_count == 0)
        throw triangle_utils::Error(triangle_utils::message(ele_name, " has no triangles"));
    TriangleUtilsMesh * created = nullptr;
    check_status(triangle_utils_mesh_create(vertex_count, vertex_xy.data(), triangle_count,
                                            corners.data(), &created));
    MeshHandle mesh(created);

    const std::string neigh_name = base + ".neigh";
    std::error_code exists_error;
    const bool has_neighbors = std::filesystem::exists(neigh_name, exists_error);
    if (exists_error)
        throw triangle_utils::Error(
            triangle_utils::message("cannot tell whether ", neigh_name, " exists"));
    if (has_neighbors)
    {
        std::ifstream neigh_file(neigh_name);
        if (!std::filesystem::is_regular_file(neigh_name, exists_error) || !neigh_file)
            throw triangle_utils::Error(triangle_utils::message("cannot open ", neigh_name));
        std::vector<int> neighbors;
        for (const auto & triangle : triangle_utils::read_neigh(
                 neigh_file, neigh_name, vertices.first_number, triangles.triangles.size()))
        {
            neighbors.insert(neighbors.end(), triangle.begin(), triangle.end());
        }
        check_status(
            triangle_utils_mesh_set_neighbors(mesh.get(), triangle_count, neighbors.data()));
    }

    std::cout << base << ": " << vertex_count << " vertices and " << triangle_count << " triangles"
              << (has_neighbors ? ", with neighbors" : "") << "; the mesh is valid\n";
    return EXIT_OK;
}

// A command's arguments: options as -x value, --name value, or --name=value,
// flags, and the other arguments.
class Options
{
public:
    Options(std::string command, const std::vector<std::string> & arguments)
        : m_command(std::move(command)), m_arguments(arguments)
    {
    }

    // Move to the next argument; false after the last.
    bool next()
    {
        if (++m_index >= m_arguments.size()) return false;
        m_option = m_arguments[m_index];
        m_has_value = false;
        const std::size_t equals = m_option.find('=');
        if (m_option.starts_with("--") && equals != std::string::npos)
        {
            m_value = m_option.substr(equals + 1);
            m_option.erase(equals);
            m_has_value = true;
        }
        return true;
    }

    bool is(const char * short_name, const char * long_name) const
    {
        return m_option == short_name || m_option == long_name;
    }

    // Whether the argument is this flag, which takes no value.
    bool is_flag(const char * short_name, const char * long_name) const
    {
        if (!is(short_name, long_name)) return false;
        if (m_has_value)
            throw UsageError(triangle_utils::message(m_command, ": ", m_option, " takes no value"));
        return true;
    }

    bool is_option() const { return m_option.size() > 1 && m_option[0] == '-'; }

    const std::string & argument() const { return m_option; }

    // The option's value, after = or in the next argument.
    std::string value()
    {
        if (m_has_value) return m_value;
        if (m_index + 1 >= m_arguments.size())
            throw UsageError(triangle_utils::message(m_command, ": ", m_option, " needs a value"));
        return m_arguments[++m_index];
    }

    double number()
    {
        const std::string text = value();
        char * end = nullptr;
        const double result = std::strtod(text.c_str(), &end);
        if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(result))
            throw UsageError(triangle_utils::message(m_command, ": ", m_option,
                                                     " needs a number, not \"", text, "\""));
        return result;
    }

    int integer()
    {
        const double result = number();
        if (result != std::floor(result) || result < 0.0 || result > INT_MAX)
            throw UsageError(triangle_utils::message(m_command, ": ", m_option,
                                                     " needs a whole number, not ", result));
        return static_cast<int>(result);
    }

    [[noreturn]] void unknown() const
    {
        throw UsageError(triangle_utils::message(m_command, ": unknown option ", m_option));
    }

private:
    std::string m_command;
    const std::vector<std::string> & m_arguments;
    std::size_t m_index = static_cast<std::size_t>(-1);
    std::string m_option;
    std::string m_value;
    bool m_has_value = false;
};

// The output base name after base, as Triangle numbers them: x.1 after x,
// and x.2 after x.1.
std::string next_base(const std::string & base)
{
    const std::size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < base.size() &&
        base.find_first_not_of("0123456789", dot + 1) == std::string::npos &&
        base.size() - dot - 1 < 9)
        return base.substr(0, dot + 1) + std::to_string(std::stoi(base.substr(dot + 1)) + 1);
    return base + ".1";
}

// Throw UsageError if writing to output would replace base's files.
void check_output(const std::string & command, const std::string & output, const std::string & base)
{
    if (std::filesystem::absolute(output).lexically_normal() ==
        std::filesystem::absolute(base).lexically_normal())
        throw UsageError(command + ": the output would replace the input; give another -o");
}

std::ifstream open_input(const std::string & name)
{
    std::ifstream file(name);
    if (!file) throw triangle_utils::Error(triangle_utils::message("cannot open ", name));
    return file;
}

std::ofstream open_output(const std::string & name)
{
    std::ofstream file(name);
    if (!file) throw triangle_utils::Error(triangle_utils::message("cannot create ", name));
    return file;
}

struct GeometryFree
{
    void operator()(TriangleUtilsGeometry * geometry) const
    {
        triangle_utils_geometry_free(geometry);
    }
};

// The mesh's vertex and triangle counts, through the C API.
std::pair<int, int> mesh_counts(const TriangleUtilsMesh * mesh)
{
    int vertex_count = 0;
    int triangle_count = 0;
    check_status(triangle_utils_mesh_counts(mesh, &vertex_count, &triangle_count));
    return {vertex_count, triangle_count};
}

std::string mesh_summary(const TriangleUtilsMesh * mesh)
{
    const auto [vertex_count, triangle_count] = mesh_counts(mesh);
    return triangle_utils::message(vertex_count, " vertices and ", triangle_count, " triangles");
}

// Write a mesh's .node, .ele, .neigh, and .poly files, with the input's
// holes and regions in the .poly file.
void write_mesh(const TriangleUtilsMesh * mesh, const std::string & base, int first_number,
                const triangle_utils::PolyFile & input)
{
    const auto [vertex_count, triangle_count] = mesh_counts(mesh);
    std::vector<double> vertex_xy(2 * static_cast<std::size_t>(vertex_count));
    check_status(triangle_utils_mesh_vertices(mesh, vertex_count, vertex_xy.data()));
    triangle_utils::VertexFile vertices;
    vertices.first_number = first_number;
    for (std::size_t vertex = 0; vertex < static_cast<std::size_t>(vertex_count); vertex++)
    {
        vertices.vertices.push_back({vertex_xy[2 * vertex], vertex_xy[2 * vertex + 1]});
    }
    // a fixed vertex, such as a well, has marker 1, which reading the mesh
    // again takes as fixed
    int fixed_count = 0;
    check_status(triangle_utils_mesh_fixed_vertex_count(mesh, &fixed_count));
    std::vector<int> fixed(static_cast<std::size_t>(fixed_count));
    check_status(triangle_utils_mesh_fixed_vertices(mesh, fixed_count, fixed.data()));
    vertices.markers.assign(vertices.vertices.size(), 0);
    for (const int vertex : fixed) vertices.markers[vertex] = 1;

    std::vector<int> corners(3 * static_cast<std::size_t>(triangle_count));
    check_status(triangle_utils_mesh_triangles(mesh, triangle_count, corners.data()));
    int attribute_count = 0;
    check_status(triangle_utils_mesh_triangle_attribute_count(mesh, &attribute_count));
    triangle_utils::TriangleFile triangles;
    triangles.attribute_count = attribute_count;
    triangles.attributes.resize(static_cast<std::size_t>(attribute_count) * triangle_count);
    check_status(triangle_utils_mesh_triangle_attributes(
        mesh, static_cast<int>(triangles.attributes.size()), triangles.attributes.data()));
    std::vector<int> neighbor_list(3 * static_cast<std::size_t>(triangle_count));
    check_status(triangle_utils_mesh_neighbors(mesh, triangle_count, neighbor_list.data()));
    std::vector<std::array<int, 3>> neighbors;
    for (std::size_t triangle = 0; triangle < static_cast<std::size_t>(triangle_count); triangle++)
    {
        triangles.triangles.push_back(
            {corners[3 * triangle], corners[3 * triangle + 1], corners[3 * triangle + 2]});
        neighbors.push_back({neighbor_list[3 * triangle], neighbor_list[3 * triangle + 1],
                             neighbor_list[3 * triangle + 2]});
    }

    int segment_count = 0;
    check_status(triangle_utils_mesh_segment_count(mesh, &segment_count));
    std::vector<int> ends(2 * static_cast<std::size_t>(segment_count));
    triangle_utils::PolyFile poly;
    poly.segment_markers.resize(static_cast<std::size_t>(segment_count));
    check_status(triangle_utils_mesh_segments(mesh, segment_count, ends.data(),
                                              poly.segment_markers.data()));
    for (std::size_t segment = 0; segment < static_cast<std::size_t>(segment_count); segment++)
    {
        poly.segments.push_back({ends[2 * segment], ends[2 * segment + 1]});
    }
    poly.holes = input.holes;
    poly.regions = input.regions;

    std::ofstream node_file = open_output(base + ".node");
    triangle_utils::write_node(node_file, vertices);
    std::ofstream ele_file = open_output(base + ".ele");
    triangle_utils::write_ele(ele_file, triangles, first_number);
    std::ofstream neigh_file = open_output(base + ".neigh");
    triangle_utils::write_neigh(neigh_file, neighbors, first_number);
    std::ofstream poly_file = open_output(base + ".poly");
    triangle_utils::write_poly(poly_file, poly, first_number);
}

// Read the mesh in <base>.node and <base>.ele, with the segments of
// <base>.poly and the area limits of <base>.area if they exist.
// The mesh in <base>.node and <base>.ele, with the segments of <base>.poly
// and the area limits of <base>.area if they exist and are asked for.
MeshHandle read_mesh(const std::string & base, triangle_utils::VertexFile & vertices,
                     bool with_segments_and_limits = true)
{
    const std::string node_name = base + ".node";
    std::ifstream node_file = open_input(node_name);
    vertices = triangle_utils::read_node(node_file, node_name);
    const std::string ele_name = base + ".ele";
    std::ifstream ele_file = open_input(ele_name);
    const triangle_utils::TriangleFile triangles =
        triangle_utils::read_ele(ele_file, ele_name, vertices);

    std::vector<double> vertex_xy;
    for (const triangle_utils::Point & vertex : vertices.vertices)
    {
        vertex_xy.push_back(vertex.x);
        vertex_xy.push_back(vertex.y);
    }
    std::vector<int> corners;
    for (const auto & triangle : triangles.triangles)
        corners.insert(corners.end(), triangle.begin(), triangle.end());
    // the file readers limit the counts to what an int holds
    const int triangle_count = static_cast<int>(triangles.triangles.size());
    TriangleUtilsMesh * created = nullptr;
    check_status(triangle_utils_mesh_create(static_cast<int>(vertices.vertices.size()),
                                            vertex_xy.data(), triangle_count, corners.data(),
                                            &created));
    MeshHandle mesh(created);
    check_status(triangle_utils_mesh_set_triangle_attributes(
        mesh.get(), triangle_count, triangles.attribute_count, triangles.attributes.data()));
    // a vertex with a nonzero marker, such as a well, stays in place when
    // the mesh is improved
    std::vector<int> fixed;
    for (std::size_t vertex = 0; vertex < vertices.markers.size(); vertex++)
        if (vertices.markers[vertex] != 0) fixed.push_back(static_cast<int>(vertex));
    check_status(triangle_utils_mesh_set_fixed_vertices(mesh.get(), static_cast<int>(fixed.size()),
                                                        fixed.data()));

    if (!with_segments_and_limits) return mesh;
    std::error_code exists_error;
    const std::string poly_name = base + ".poly";
    if (std::filesystem::exists(poly_name, exists_error))
    {
        std::ifstream poly_file = open_input(poly_name);
        const triangle_utils::PolyFile poly =
            triangle_utils::read_poly(poly_file, poly_name, &vertices);
        if (!poly.vertices.vertices.empty())
            throw triangle_utils::Error(triangle_utils::message(
                poly_name, " has vertices; its segments must use those of ", node_name));
        std::vector<int> ends;
        for (const auto & segment : poly.segments)
            ends.insert(ends.end(), segment.begin(), segment.end());
        check_status(triangle_utils_mesh_set_segments(
            mesh.get(), static_cast<int>(poly.segments.size()), ends.data(),
            poly.segment_markers.empty() ? nullptr : poly.segment_markers.data()));
    }
    const std::string area_name = base + ".area";
    if (std::filesystem::exists(area_name, exists_error))
    {
        std::ifstream area_file = open_input(area_name);
        const std::vector<double> limits =
            triangle_utils::read_area(area_file, area_name, triangles.triangles.size());
        check_status(
            triangle_utils_mesh_set_area_limits(mesh.get(), triangle_count, limits.data()));
    }
    return mesh;
}

int refine_command(const std::vector<std::string> & arguments)
{
    std::string output_base;
    double maximum_area = 0.0;
    double minimum_angle = 0.0;
    double maximum_angle = 0.0;
    int angle_splits = 100000;
    bool conforming_delaunay = false;
    std::vector<std::string> bases;
    Options options("refine", arguments);
    while (options.next())
    {
        if (options.is_flag("-h", "--help"))
        {
            std::cout << REFINE_USAGE;
            return EXIT_OK;
        }
        if (options.is("-o", "--output"))
            output_base = options.value();
        else if (options.is("-a", "--maximum-area"))
            maximum_area = options.number();
        else if (options.is("-q", "--minimum-angle"))
            minimum_angle = options.number();
        else if (options.is("-A", "--maximum-angle"))
            maximum_angle = options.number();
        else if (options.is("-n", "--angle-splits"))
            angle_splits = options.integer();
        else if (options.is_flag("-D", "--conforming-delaunay"))
            conforming_delaunay = true;
        else if (options.is_option())
            options.unknown();
        else
            bases.push_back(options.argument());
    }
    if (bases.size() != 1) throw UsageError("refine: give one <base>");
    const std::string & base = bases[0];
    if (output_base.empty()) output_base = next_base(base);
    check_output("refine", output_base, base);

    triangle_utils::VertexFile vertices;
    const MeshHandle mesh = read_mesh(base, vertices);
    TriangleUtilsMesh * refined = nullptr;
    check_status(triangle_utils_mesh_refine(mesh.get(), maximum_area, minimum_angle, maximum_angle,
                                            angle_splits, conforming_delaunay ? 1 : 0, &refined));
    const MeshHandle result(refined);
    write_mesh(result.get(), output_base, vertices.first_number, {});
    std::cout << output_base << ": " << mesh_summary(result.get());
    if (maximum_angle > 0.0)
    {
        int over = 0;
        check_status(triangle_utils_mesh_count_angles_over(result.get(), maximum_angle, &over));
        std::cout << "; " << over << " with an angle over " << maximum_angle << " degrees";
    }
    std::cout << "\n";
    return EXIT_OK;
}

int improve_command(const std::vector<std::string> & arguments)
{
    std::string output_base;
    double maximum_angle = 0.0;
    double minimum_angle = 0.0;
    int smoothing = TRIANGLE_UTILS_LAPLACIAN;
    int iterations = 100;
    double tolerance = 1e-4;
    std::vector<std::string> bases;
    Options options("improve", arguments);
    while (options.next())
    {
        if (options.is_flag("-h", "--help"))
        {
            std::cout << IMPROVE_USAGE;
            return EXIT_OK;
        }
        if (options.is("-o", "--output"))
            output_base = options.value();
        else if (options.is("-A", "--maximum-angle"))
            maximum_angle = options.number();
        else if (options.is("-q", "--minimum-angle"))
            minimum_angle = options.number();
        else if (options.is("-s", "--smoothing"))
        {
            const std::string name = options.value();
            if (name == "laplacian")
                smoothing = TRIANGLE_UTILS_LAPLACIAN;
            else if (name == "centroidal")
                smoothing = TRIANGLE_UTILS_CENTROIDAL;
            else
                throw UsageError(triangle_utils::message(
                    "improve: the smoothing must be laplacian or centroidal, not ", name));
        }
        else if (options.is("-n", "--iterations"))
            iterations = options.integer();
        else if (options.is("-t", "--tolerance"))
            tolerance = options.number();
        else if (options.is_option())
            options.unknown();
        else
            bases.push_back(options.argument());
    }
    if (bases.size() != 1) throw UsageError("improve: give one <base>");
    const std::string & base = bases[0];
    if (output_base.empty()) output_base = next_base(base);
    check_output("improve", output_base, base);

    triangle_utils::VertexFile vertices;
    const MeshHandle mesh = read_mesh(base, vertices);
    TriangleUtilsMesh * improved = nullptr;
    check_status(triangle_utils_mesh_improve(mesh.get(), smoothing, maximum_angle, minimum_angle,
                                             iterations, tolerance, &improved));
    const MeshHandle result(improved);
    write_mesh(result.get(), output_base, vertices.first_number, {});
    std::cout << output_base << ": " << mesh_summary(result.get());
    if (maximum_angle > 0.0)
    {
        int over = 0;
        check_status(triangle_utils_mesh_count_angles_over(result.get(), maximum_angle, &over));
        std::cout << "; " << over << " with an angle over " << maximum_angle << " degrees";
    }
    std::cout << "\n";
    return EXIT_OK;
}

struct GridFree
{
    void operator()(TriangleUtilsGrid * grid) const { triangle_utils_grid_free(grid); }
};

using GridHandle = std::unique_ptr<TriangleUtilsGrid, GridFree>;

// Throw UsageError if a grid name is not one the commands build.
void check_grid_name(const std::string & command, const std::string & grid_name)
{
    if (grid_name != "voronoi" && grid_name != "centroid" && grid_name != "circumcenter")
        throw UsageError(triangle_utils::message(
            command, ": the grid must be voronoi, centroid, or circumcenter, not ", grid_name));
}

// The Voronoi grid of a mesh, or its triangles centered on their centroids
// or circumcenters.
GridHandle build_grid(const TriangleUtilsMesh * mesh, const std::string & grid_name)
{
    TriangleUtilsGrid * built = nullptr;
    if (grid_name == "voronoi")
        check_status(triangle_utils_mesh_voronoi(mesh, &built));
    else
        check_status(triangle_utils_mesh_triangle_grid(
            mesh, grid_name == "centroid" ? TRIANGLE_UTILS_CENTROID : TRIANGLE_UTILS_CIRCUMCENTER,
            &built));
    return GridHandle(built);
}

struct LayersFree
{
    void operator()(TriangleUtilsLayers * layers) const { triangle_utils_layers_free(layers); }
};

using LayersHandle = std::unique_ptr<TriangleUtilsLayers, LayersFree>;

// A grid's cells, the mesh vertex or triangle of each, and their
// attributes, through the C API.
struct GridData
{
    triangle_utils::GridCells cells;
    std::vector<int> sources;
    int attribute_count = 0;
    std::vector<double> attributes;
};

GridData grid_data(const TriangleUtilsGrid * grid)
{
    int vertex_count = 0, cell_count = 0, corner_count = 0;
    check_status(triangle_utils_grid_counts(grid, &vertex_count, &cell_count, &corner_count));
    std::vector<double> vertex_xy(2 * static_cast<std::size_t>(vertex_count));
    check_status(triangle_utils_grid_vertices(grid, vertex_count, vertex_xy.data()));
    std::vector<int> offsets(static_cast<std::size_t>(cell_count) + 1);
    std::vector<int> corners(static_cast<std::size_t>(corner_count));
    check_status(
        triangle_utils_grid_cells(grid, cell_count, offsets.data(), corner_count, corners.data()));
    std::vector<double> center_xy(2 * static_cast<std::size_t>(cell_count));
    check_status(triangle_utils_grid_centers(grid, cell_count, center_xy.data()));
    GridData data;
    data.sources.resize(static_cast<std::size_t>(cell_count));
    check_status(triangle_utils_grid_sources(grid, cell_count, data.sources.data()));
    check_status(triangle_utils_grid_attribute_count(grid, &data.attribute_count));
    data.attributes.resize(static_cast<std::size_t>(cell_count) * data.attribute_count);
    check_status(triangle_utils_grid_attributes(grid, static_cast<int>(data.attributes.size()),
                                                data.attributes.data()));
    for (std::size_t vertex = 0; vertex < static_cast<std::size_t>(vertex_count); vertex++)
        data.cells.vertices.push_back({vertex_xy[2 * vertex], vertex_xy[2 * vertex + 1]});
    for (std::size_t cell = 0; cell < static_cast<std::size_t>(cell_count); cell++)
    {
        data.cells.cells.emplace_back(corners.begin() + offsets[cell],
                                      corners.begin() + offsets[cell + 1]);
        data.cells.centers.push_back({center_xy[2 * cell], center_xy[2 * cell + 1]});
    }
    return data;
}

// Write each cell's vertex or triangle, numbered as in Triangle's files,
// and its attributes to <output_base>.cells.
void write_cells(const std::string & output_base, const std::string & grid_name,
                 const GridData & data, int first_number)
{
    std::ofstream file = open_output(output_base + ".cells");
    file.precision(std::numeric_limits<double>::max_digits10);
    const std::size_t cell_count = data.sources.size();
    file << "# cell, " << (grid_name == "voronoi" ? "vertex" : "triangle") << ", and attributes\n"
         << cell_count << " " << data.attribute_count << "\n";
    for (std::size_t cell = 0; cell < cell_count; cell++)
    {
        file << cell + 1 << " " << data.sources[cell] + first_number;
        for (int attribute = 0; attribute < data.attribute_count; attribute++)
            file << " " << data.attributes[cell * data.attribute_count + attribute];
        file << "\n";
    }
    if (!file) throw triangle_utils::Error("the cells file could not be written");
}

// The numbers of a file, separated by white space, with # comments.
std::vector<double> read_numbers(const std::string & name)
{
    std::ifstream file = open_input(name);
    std::vector<double> numbers;
    std::string text;
    int number = 0;
    while (std::getline(file, text))
    {
        number++;
        const std::size_t comment = text.find('#');
        if (comment != std::string::npos) text.erase(comment);
        std::istringstream words(text);
        std::string word;
        while (words >> word)
        {
            char * end = nullptr;
            const double value = std::strtod(word.c_str(), &end);
            if (end != word.c_str() + word.size() || !std::isfinite(value))
                throw triangle_utils::Error(triangle_utils::message(
                    name, " line ", number, ": \"", word, "\" is not a finite number"));
            numbers.push_back(value);
        }
    }
    return numbers;
}

// The text as a number, if it is one; throws Error if it is not finite.
bool parse_number(const std::string & text, double & value)
{
    char * end = nullptr;
    value = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size()) return false;
    if (!std::isfinite(value))
        throw triangle_utils::Error(triangle_utils::message(text, " is not a finite number"));
    return true;
}

// The layers as the options give them: the top, and each layer's bottom and
// idomain, each a number or a file.
struct LayerOptions
{
    std::string top = "1";
    std::vector<std::string> bottoms;
    std::vector<std::string> idomain;
    double minimum_thickness = 0.0;

    // Take the option if it is one of these; true if it was.
    bool take(Options & options)
    {
        if (options.is("-t", "--top"))
            top = options.value();
        else if (options.is("-b", "--bottom"))
            bottoms.push_back(options.value());
        else if (options.is("-i", "--idomain"))
            idomain.push_back(options.value());
        else if (options.is("-m", "--minimum-thickness"))
            minimum_thickness = options.number();
        else
            return false;
        return true;
    }

    // Throw UsageError if the options do not give the same layers.
    void check(const std::string & command)
    {
        if (bottoms.empty()) bottoms.push_back("0");
        if (!idomain.empty() && idomain.size() != bottoms.size())
            throw UsageError(triangle_utils::message(command, ": give -i once for each layer, ",
                                                     bottoms.size(), ", not ", idomain.size(),
                                                     " times"));
        if (minimum_thickness < 0.0)
            throw UsageError(triangle_utils::message(command, ": the minimum thickness (",
                                                     minimum_thickness, ") must not be negative"));
    }
};

// A value of each cell: a number, the values of a file with one for each
// cell, or with one for each vertex of the mesh, interpolated at the cells'
// centers.
std::vector<double> cell_values(const std::string & text, const TriangleUtilsGrid * grid,
                                const TriangleUtilsMesh * mesh, int cell_count)
{
    double number = 0.0;
    if (parse_number(text, number))
        return std::vector<double>(static_cast<std::size_t>(cell_count), number);
    std::vector<double> values = read_numbers(text);
    if (values.size() == static_cast<std::size_t>(cell_count)) return values;
    const int vertex_count = mesh_counts(mesh).first;
    if (values.size() != static_cast<std::size_t>(vertex_count))
        throw triangle_utils::Error(
            triangle_utils::message(text, " has ", values.size(), " values, but the grid has ",
                                    cell_count, " cells and the mesh ", vertex_count, " vertices"));
    std::vector<double> at_centers(static_cast<std::size_t>(cell_count));
    check_status(triangle_utils_grid_vertex_values(grid, mesh, vertex_count, values.data(),
                                                   cell_count, at_centers.data()));
    return at_centers;
}

// An idomain of each cell: a whole number, or the values of a file with one
// for each cell.
std::vector<int> cell_idomain(const std::string & text, int cell_count)
{
    double number = 0.0;
    const bool is_number = parse_number(text, number);
    const std::vector<double> values =
        is_number ? std::vector<double>(cell_count, number) : read_numbers(text);
    if (values.size() != static_cast<std::size_t>(cell_count))
        throw triangle_utils::Error(triangle_utils::message(text, " has ", values.size(),
                                                            " idomain values, but the grid has ",
                                                            cell_count, " cells"));
    std::vector<int> idomain;
    for (const double value : values)
    {
        if (value != std::floor(value) || std::abs(value) > INT_MAX)
            throw triangle_utils::Error(triangle_utils::message(
                "the idomain ", value, is_number ? "" : " in " + text, " is not an integer"));
        idomain.push_back(static_cast<int>(value));
    }
    return idomain;
}

// The layers of a grid as the options give them, with the top of each cell,
// and the bottoms and idomain after the minimum thickness.
struct BuiltLayers
{
    LayersHandle layers;
    int layer_count = 0;
    std::vector<double> top;
    std::vector<double> bottoms;
    std::vector<int> idomain;
};

BuiltLayers build_layers(const LayerOptions & options, const TriangleUtilsGrid * grid,
                         const TriangleUtilsMesh * mesh, int cell_count)
{
    BuiltLayers built;
    built.layer_count = static_cast<int>(options.bottoms.size());
    built.top = cell_values(options.top, grid, mesh, cell_count);
    for (const std::string & bottom : options.bottoms)
    {
        const std::vector<double> values = cell_values(bottom, grid, mesh, cell_count);
        built.bottoms.insert(built.bottoms.end(), values.begin(), values.end());
    }
    for (const std::string & idomain : options.idomain)
    {
        const std::vector<int> values = cell_idomain(idomain, cell_count);
        built.idomain.insert(built.idomain.end(), values.begin(), values.end());
    }
    TriangleUtilsLayers * layers = nullptr;
    check_status(triangle_utils_grid_layers(grid, built.layer_count, cell_count, built.top.data(),
                                            built.bottoms.data(),
                                            built.idomain.empty() ? nullptr : built.idomain.data(),
                                            options.minimum_thickness, &layers));
    built.layers.reset(layers);
    const int value_count = static_cast<int>(built.bottoms.size());
    built.idomain.resize(built.bottoms.size());
    check_status(
        triangle_utils_layers_idomain(built.layers.get(), value_count, built.idomain.data()));
    check_status(
        triangle_utils_layers_bottoms(built.layers.get(), value_count, built.bottoms.data()));
    return built;
}

// The number of cells in the layers with idomain below 0.
int pass_through_count(const std::vector<int> & idomain)
{
    return static_cast<int>(
        std::count_if(idomain.begin(), idomain.end(), [](int value) { return value < 0; }));
}

int disv_command(const std::vector<std::string> & arguments)
{
    std::string output_base;
    std::string grid_name = "voronoi";
    LayerOptions layer_options;
    std::vector<std::string> bases;
    Options options("disv", arguments);
    while (options.next())
    {
        if (options.is_flag("-h", "--help"))
        {
            std::cout << DISV_USAGE;
            return EXIT_OK;
        }
        if (options.is("-o", "--output"))
            output_base = options.value();
        else if (options.is("-g", "--grid"))
            grid_name = options.value();
        else if (layer_options.take(options))
            continue;
        else if (options.is_option())
            options.unknown();
        else
            bases.push_back(options.argument());
    }
    if (bases.size() != 1) throw UsageError("disv: give one <base>");
    check_grid_name("disv", grid_name);
    layer_options.check("disv");
    const std::string & base = bases[0];
    if (output_base.empty()) output_base = base;

    triangle_utils::VertexFile vertices;
    // the grid needs neither segments nor area limits
    const MeshHandle mesh = read_mesh(base, vertices, false);
    const GridHandle grid = build_grid(mesh.get(), grid_name);
    GridData data = grid_data(grid.get());
    const int cell_count = static_cast<int>(data.sources.size());
    BuiltLayers built = build_layers(layer_options, grid.get(), mesh.get(), cell_count);

    triangle_utils::DisvFile disv;
    disv.grid = std::move(data.cells);
    disv.layer_count = built.layer_count;
    disv.top = std::move(built.top);
    disv.bottoms = std::move(built.bottoms);
    disv.idomain = std::move(built.idomain);
    // written in full before the file is opened, so an error leaves it as it was
    std::ostringstream text;
    triangle_utils::write_disv(text, disv);
    std::ofstream disv_file = open_output(output_base + ".disv");
    disv_file << text.str();
    if (!disv_file) throw triangle_utils::Error("the DISV file could not be written");
    write_cells(output_base, grid_name, data, vertices.first_number);

    std::cout << output_base << ".disv: " << cell_count << " cells and "
              << disv.grid.vertices.size() << " vertices in " << disv.layer_count
              << (disv.layer_count == 1 ? " layer" : " layers") << ", "
              << pass_through_count(disv.idomain) << " pass-through\n";
    return EXIT_OK;
}

int disu_command(const std::vector<std::string> & arguments)
{
    std::string output_base;
    std::string grid_name = "voronoi";
    std::string format = "mf6";
    LayerOptions layer_options;
    std::vector<std::string> bases;
    Options options("disu", arguments);
    while (options.next())
    {
        if (options.is_flag("-h", "--help"))
        {
            std::cout << DISU_USAGE;
            return EXIT_OK;
        }
        if (options.is("-o", "--output"))
            output_base = options.value();
        else if (options.is("-g", "--grid"))
            grid_name = options.value();
        else if (options.is("-f", "--format"))
            format = options.value();
        else if (layer_options.take(options))
            continue;
        else if (options.is_option())
            options.unknown();
        else
            bases.push_back(options.argument());
    }
    if (bases.size() != 1) throw UsageError("disu: give one <base>");
    check_grid_name("disu", grid_name);
    if (format != "mf6" && format != "usg")
        throw UsageError(
            triangle_utils::message("disu: the format must be mf6 or usg, not ", format));
    layer_options.check("disu");
    const std::string & base = bases[0];
    if (output_base.empty()) output_base = base;

    triangle_utils::VertexFile vertices;
    const MeshHandle mesh = read_mesh(base, vertices, false);
    const GridHandle grid = build_grid(mesh.get(), grid_name);
    GridData data = grid_data(grid.get());
    const int cell_count = static_cast<int>(data.sources.size());
    const BuiltLayers built = build_layers(layer_options, grid.get(), mesh.get(), cell_count);

    triangle_utils::DisuFile disu;
    disu.layer_count = built.layer_count;
    int node_count = 0, connection_count = 0;
    check_status(
        triangle_utils_layers_disu_count(built.layers.get(), &node_count, &connection_count));
    const std::size_t nodes = static_cast<std::size_t>(node_count);
    const std::size_t connections = static_cast<std::size_t>(connection_count);
    disu.node_cells.resize(nodes);
    disu.top.resize(nodes);
    disu.bottom.resize(nodes);
    disu.area.resize(nodes);
    check_status(triangle_utils_layers_disu_nodes(built.layers.get(), node_count,
                                                  disu.node_cells.data(), disu.top.data(),
                                                  disu.bottom.data(), disu.area.data()));
    disu.offsets.resize(nodes + 1);
    disu.nodes.resize(connections);
    disu.directions.resize(connections);
    disu.distances.resize(connections);
    disu.widths.resize(connections);
    disu.angles.resize(connections);
    check_status(triangle_utils_layers_disu_connections(
        built.layers.get(), node_count, disu.offsets.data(), connection_count, disu.nodes.data(),
        disu.directions.data(), disu.distances.data(), disu.widths.data(), disu.angles.data()));
    disu.grid = std::move(data.cells);
    // written in full before the file is opened, so an error leaves it as it was
    std::ostringstream text;
    if (format == "mf6")
        triangle_utils::write_disu(text, disu);
    else
        triangle_utils::write_usg_disu(text, disu);
    std::ofstream disu_file = open_output(output_base + ".disu");
    disu_file << text.str();
    if (!disu_file) throw triangle_utils::Error("the DISU file could not be written");
    write_cells(output_base, grid_name, data, vertices.first_number);

    // each node's layer and cell, numbered from 1
    std::ofstream nodes_file = open_output(output_base + ".nodes");
    nodes_file << "# node, layer, and cell\n" << node_count << "\n";
    for (std::size_t node = 0; node < nodes; node++)
        nodes_file << node + 1 << " " << disu.node_cells[node] / cell_count + 1 << " "
                   << disu.node_cells[node] % cell_count + 1 << "\n";
    if (!nodes_file) throw triangle_utils::Error("the nodes file could not be written");

    std::cout << output_base << ".disu: " << node_count << " nodes and " << connection_count
              << " connections in " << disu.layer_count
              << (disu.layer_count == 1 ? " layer" : " layers") << " of " << cell_count
              << " cells, " << pass_through_count(built.idomain) << " pass-through\n";
    return EXIT_OK;
}

struct CellLengthsFree
{
    void operator()(TriangleUtilsCellLengths * cell_lengths) const
    {
        triangle_utils_cell_lengths_free(cell_lengths);
    }
};

struct FeaturesFree
{
    void operator()(TriangleUtilsFeatures * features) const
    {
        triangle_utils_features_free(features);
    }
};

using FeaturesHandle = std::unique_ptr<TriangleUtilsFeatures, FeaturesFree>;

// The values of a shapefile's features as the options give them, each a
// number or the name of a .dbf field.
struct ShapefileValues
{
    std::string marker = "0";
    std::string spacing = "0";
    std::string attribute = "0";
    std::string area_limit = "0";

    // Take the option if it is one of these; true if it was.
    bool take(Options & options)
    {
        if (options.is("--marker", "--marker"))
            marker = options.value();
        else if (options.is("--feature-spacing", "--feature-spacing"))
            spacing = options.value();
        else if (options.is("--attribute", "--attribute"))
            attribute = options.value();
        else if (options.is("--area-limit", "--area-limit"))
            area_limit = options.value();
        else
            return false;
        return true;
    }
};

// Whether a file name ends with .shp, in any case.
bool is_shapefile(const std::string & name)
{
    std::string extension = std::filesystem::path(name).extension().string();
    for (char & character : extension)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return extension == ".shp";
}

FeaturesHandle read_shapefile(const std::string & name, const ShapefileValues & values)
{
    // a value that is a number is the number, and otherwise names a field
    std::array<std::string, 4> fields;
    std::array<double, 4> numbers{};
    const std::array<const std::string *, 4> texts = {&values.marker, &values.spacing,
                                                      &values.attribute, &values.area_limit};
    for (std::size_t i = 0; i < texts.size(); i++)
        if (!parse_number(*texts[i], numbers[i])) fields[i] = *texts[i];
    if (fields[0].empty() &&
        (numbers[0] != std::floor(numbers[0]) || std::abs(numbers[0]) > INT_MAX))
        throw triangle_utils::Error(
            triangle_utils::message("the marker ", values.marker, " is not a whole number"));
    const auto field = [&](std::size_t i)
    { return fields[i].empty() ? nullptr : fields[i].c_str(); };
    TriangleUtilsFeatures * read = nullptr;
    check_status(triangle_utils_shapefile_read(name.c_str(), field(0), static_cast<int>(numbers[0]),
                                               field(1), numbers[1], field(2), numbers[2], field(3),
                                               numbers[3], &read));
    return FeaturesHandle(read);
}

// The kinds, offsets, and points of features, through the C API.
struct FeatureData
{
    std::vector<int> kinds;
    std::vector<int> rows;
    std::vector<int> offsets;
    std::vector<double> xy;
    std::vector<int> markers;
    std::vector<double> spacings;
    std::vector<double> attributes;
    std::vector<double> area_limits;
};

FeatureData feature_data(const TriangleUtilsFeatures * features)
{
    int feature_count = 0, point_count = 0;
    check_status(triangle_utils_features_count(features, &feature_count, &point_count));
    const std::size_t n = static_cast<std::size_t>(feature_count);
    FeatureData data;
    data.kinds.resize(n);
    data.rows.resize(n);
    data.offsets.resize(n + 1);
    data.xy.resize(2 * static_cast<std::size_t>(point_count));
    data.markers.resize(n);
    data.spacings.resize(n);
    data.attributes.resize(n);
    data.area_limits.resize(n);
    check_status(triangle_utils_features(features, feature_count, data.kinds.data(),
                                         data.rows.data(), data.offsets.data(), point_count,
                                         data.xy.data(), data.markers.data(), data.spacings.data(),
                                         data.attributes.data(), data.area_limits.data()));
    return data;
}

// The points of a line file: x and y on each line, with # comments.
std::vector<double> read_line(const std::string & name)
{
    std::ifstream file = open_input(name);
    std::vector<double> xy;
    std::string text;
    int number = 0;
    while (std::getline(file, text))
    {
        number++;
        const std::size_t comment = text.find('#');
        if (comment != std::string::npos) text.erase(comment);
        std::istringstream words(text);
        double x = 0.0, y = 0.0;
        std::string extra;
        if (!(words >> x))
        {
            if (text.find_first_not_of(" \t\r") == std::string::npos) continue;
            throw triangle_utils::Error(
                triangle_utils::message(name, " line ", number, ": expected x and y"));
        }
        if (!(words >> y) || (words >> extra))
            throw triangle_utils::Error(
                triangle_utils::message(name, " line ", number, ": expected x and y"));
        xy.push_back(x);
        xy.push_back(y);
    }
    return xy;
}

int cells_command(const std::vector<std::string> & arguments)
{
    std::string grid_name = "voronoi";
    std::string line_name;
    std::string points_name;
    bool has_marker = false;
    int marker = 0;
    std::vector<std::string> bases;
    Options options("cells", arguments);
    while (options.next())
    {
        if (options.is_flag("-h", "--help"))
        {
            std::cout << CELLS_USAGE;
            return EXIT_OK;
        }
        if (options.is("-g", "--grid"))
            grid_name = options.value();
        else if (options.is("-l", "--line"))
            line_name = options.value();
        else if (options.is("-p", "--points"))
            points_name = options.value();
        else if (options.is("-m", "--marker"))
        {
            marker = options.integer();
            has_marker = true;
        }
        else if (options.is_option())
            options.unknown();
        else
            bases.push_back(options.argument());
    }
    if (bases.size() != 1) throw UsageError("cells: give one <base>");
    check_grid_name("cells", grid_name);
    if (!line_name.empty() + !points_name.empty() + has_marker != 1)
        throw UsageError("cells: give one of -l, -p, and -m");
    const std::string & base = bases[0];

    triangle_utils::VertexFile vertices;
    // a marker needs the segments of <base>.poly
    const MeshHandle mesh = read_mesh(base, vertices, has_marker);
    const GridHandle grid = build_grid(mesh.get(), grid_name);
    std::cout.precision(std::numeric_limits<double>::max_digits10);
    if (!points_name.empty())
    {
        std::vector<double> xy;
        if (is_shapefile(points_name))
        {
            // each point of the point features, in order
            const FeaturesHandle features = read_shapefile(points_name, {});
            const FeatureData data = feature_data(features.get());
            for (std::size_t i = 0; i < data.kinds.size(); i++)
                if (data.kinds[i] == TRIANGLE_UTILS_POINT)
                    xy.insert(xy.end(), data.xy.begin() + 2 * data.offsets[i],
                              data.xy.begin() + 2 * data.offsets[i + 1]);
            if (xy.empty())
                throw triangle_utils::Error(triangle_utils::message(points_name, " has no points"));
        }
        else
            xy = read_line(points_name);
        std::vector<int> cells(xy.size() / 2);
        check_status(triangle_utils_grid_point_cells(grid.get(), static_cast<int>(cells.size()),
                                                     xy.data(), cells.data()));
        // cells from 1, and 0 for a point outside the grid
        std::cout << "# point and cell of " << points_name << "\n";
        for (std::size_t point = 0; point < cells.size(); point++)
            std::cout << point + 1 << " " << cells[point] + 1 << "\n";
        return EXIT_OK;
    }
    // print the cells found along a line or segments, described
    const auto print = [](TriangleUtilsCellLengths * found, const std::string & along)
    {
        const std::unique_ptr<TriangleUtilsCellLengths, CellLengthsFree> result(found);
        int count = 0;
        check_status(triangle_utils_cell_lengths_count(result.get(), &count));
        std::vector<int> cells(static_cast<std::size_t>(count));
        std::vector<double> starts(cells.size()), lengths(cells.size());
        check_status(triangle_utils_cell_lengths(result.get(), count, cells.data(), starts.data(),
                                                 lengths.data()));
        std::cout << "# cell, start, and length along " << along << "\n";
        for (std::size_t i = 0; i < cells.size(); i++)
            std::cout << cells[i] + 1 << " " << starts[i] << " " << lengths[i] << "\n";
    };
    TriangleUtilsCellLengths * found = nullptr;
    if (has_marker)
    {
        check_status(triangle_utils_grid_segment_cells(grid.get(), mesh.get(), marker, &found));
        print(found, "the segments with marker " + std::to_string(marker));
    }
    else if (is_shapefile(line_name))
    {
        // each line of the shapefile in turn
        const FeaturesHandle features = read_shapefile(line_name, {});
        const FeatureData data = feature_data(features.get());
        int line = 0;
        for (std::size_t i = 0; i < data.kinds.size(); i++)
        {
            if (data.kinds[i] != TRIANGLE_UTILS_LINE) continue;
            line++;
            check_status(
                triangle_utils_grid_line_cells(grid.get(), data.offsets[i + 1] - data.offsets[i],
                                               data.xy.data() + 2 * data.offsets[i], &found));
            print(found,
                  triangle_utils::message(line_name, " line ", line, ", from row ", data.rows[i]));
        }
        if (line == 0)
            throw triangle_utils::Error(triangle_utils::message(line_name, " has no lines"));
    }
    else
    {
        const std::vector<double> xy = read_line(line_name);
        check_status(triangle_utils_grid_line_cells(grid.get(), static_cast<int>(xy.size() / 2),
                                                    xy.data(), &found));
        print(found, line_name);
    }
    return EXIT_OK;
}

int generate_command(const std::vector<std::string> & arguments)
{
    std::string output_base;
    double maximum_area = 0.0;
    double minimum_angle = 0.0;
    double spacing = 0.0;
    double merge_distance = 0.0;
    bool conforming_delaunay = false;
    std::vector<std::string> bases;
    // each shapefile, what its points add, and its values
    struct Shapefile
    {
        std::string name;
        int points;
        ShapefileValues values;
    };
    std::vector<Shapefile> shapefiles;
    ShapefileValues values;
    Options options("generate", arguments);
    while (options.next())
    {
        if (options.is_flag("-h", "--help"))
        {
            std::cout << GENERATE_USAGE;
            return EXIT_OK;
        }
        if (options.is("-o", "--output"))
            output_base = options.value();
        else if (options.is("-a", "--maximum-area"))
            maximum_area = options.number();
        else if (options.is("-q", "--minimum-angle"))
            minimum_angle = options.number();
        else if (options.is("-s", "--spacing"))
            spacing = options.number();
        else if (options.is("-m", "--merge-distance"))
            merge_distance = options.number();
        else if (options.is_flag("-D", "--conforming-delaunay"))
            conforming_delaunay = true;
        else if (options.is("-f", "--features") || options.is("-r", "--regions") ||
                 options.is("-H", "--holes"))
        {
            const int points = options.is("-f", "--features")  ? TRIANGLE_UTILS_VERTICES
                               : options.is("-r", "--regions") ? TRIANGLE_UTILS_REGIONS
                                                               : TRIANGLE_UTILS_HOLES;
            // the values are the next shapefile's only
            shapefiles.push_back({options.value(), points, values});
            values = {};
        }
        else if (values.take(options))
            continue;
        else if (options.is_option())
            options.unknown();
        else
            bases.push_back(options.argument());
    }
    if (bases.size() != 1) throw UsageError("generate: give one <base>");
    const std::string & base = bases[0];
    if (output_base.empty()) output_base = next_base(base);
    check_output("generate", output_base, base);

    // the .poly file's vertices, or the .node file's if it has none, as
    // Triangle reads them; a .poly file starts with a vertex section
    const std::string poly_name = base + ".poly";
    triangle_utils::VertexFile node;
    const triangle_utils::VertexFile * separate_vertices = nullptr;
    triangle_utils::PolyFile poly;
    const bool has_poly = shapefiles.empty() || std::filesystem::exists(poly_name);
    if (has_poly)
    {
        std::ifstream poly_vertices = open_input(poly_name);
        if (triangle_utils::read_node(poly_vertices, poly_name).vertices.empty())
        {
            const std::string node_name = base + ".node";
            std::ifstream node_file = open_input(node_name);
            node = triangle_utils::read_node(node_file, node_name);
            separate_vertices = &node;
        }
    }
    if (has_poly)
    {
        std::ifstream poly_file = open_input(poly_name);
        poly = triangle_utils::read_poly(poly_file, poly_name, separate_vertices);
    }
    const triangle_utils::VertexFile & vertices =
        poly.vertices.vertices.empty() ? node : poly.vertices;

    TriangleUtilsGeometry * created = nullptr;
    check_status(triangle_utils_geometry_create(&created));
    const std::unique_ptr<TriangleUtilsGeometry, GeometryFree> geometry(created);
    std::vector<double> point_xy;
    for (const triangle_utils::Point & vertex : vertices.vertices)
    {
        point_xy.push_back(vertex.x);
        point_xy.push_back(vertex.y);
    }
    // the file readers limit the counts to what an int holds
    check_status(triangle_utils_geometry_add_points(
        geometry.get(), static_cast<int>(vertices.vertices.size()), point_xy.data()));
    // each segment is a line, so the spacing and its marker apply to it
    for (std::size_t segment = 0; segment < poly.segments.size(); segment++)
    {
        const triangle_utils::Point & first = vertices.vertices[poly.segments[segment][0]];
        const triangle_utils::Point & second = vertices.vertices[poly.segments[segment][1]];
        const double ends[] = {first.x, first.y, second.x, second.y};
        const int marker = poly.segment_markers.empty() ? 0 : poly.segment_markers[segment];
        check_status(triangle_utils_geometry_add_line(geometry.get(), 2, ends, spacing, marker));
    }
    for (const triangle_utils::Point & hole : poly.holes)
    {
        check_status(triangle_utils_geometry_add_hole(geometry.get(), hole.x, hole.y));
    }
    for (const triangle_utils::Region & region : poly.regions)
    {
        check_status(triangle_utils_geometry_add_region(
            geometry.get(), region.point.x, region.point.y, region.attribute, region.area_limit));
    }
    // the shapefiles' regions and holes are written to the output's .poly
    // file, as the input's are
    triangle_utils::PolyFile output_poly = poly;
    for (const Shapefile & shapefile : shapefiles)
    {
        const FeaturesHandle features = read_shapefile(shapefile.name, shapefile.values);
        check_status(
            triangle_utils_geometry_add_features(geometry.get(), features.get(), shapefile.points));
        if (shapefile.points == TRIANGLE_UTILS_VERTICES) continue;
        const FeatureData data = feature_data(features.get());
        for (std::size_t i = 0; i < data.kinds.size(); i++)
        {
            if (data.kinds[i] != TRIANGLE_UTILS_POINT) continue;
            for (int point = data.offsets[i]; point < data.offsets[i + 1]; point++)
            {
                const triangle_utils::Point xy{data.xy[2 * point], data.xy[2 * point + 1]};
                if (shapefile.points == TRIANGLE_UTILS_REGIONS)
                    output_poly.regions.push_back({xy, data.attributes[i], data.area_limits[i]});
                else
                    output_poly.holes.push_back(xy);
            }
        }
    }

    TriangleUtilsMesh * generated = nullptr;
    check_status(triangle_utils_mesh_generate(geometry.get(), maximum_area, minimum_angle,
                                              merge_distance, conforming_delaunay ? 1 : 0,
                                              &generated));
    const MeshHandle mesh(generated);
    write_mesh(mesh.get(), output_base, has_poly ? vertices.first_number : 1, output_poly);
    std::cout << output_base << ": " << mesh_summary(mesh.get()) << "\n";
    return EXIT_OK;
}

int run(const std::vector<std::string> & arguments)
{
    if (arguments.empty()) throw UsageError("give a command or an option");
    const std::string & first = arguments[0];
    if (first == "-h" || first == "--help")
    {
        std::cout << USAGE;
        return EXIT_OK;
    }
    if (first == "--version")
    {
        if (arguments.size() > 1) throw UsageError("--version takes no arguments");
        std::cout << PROGRAM << " " << triangle_utils_version() << "\n"
                  << "Triangle " << triangle_utils_triangle_version() << " (MODFLOW-ORG/triangle "
                  << triangle_utils_triangle_release() << ")\n";
        return EXIT_OK;
    }
    if (first == "--license")
    {
        if (arguments.size() > 1) throw UsageError("--license takes no arguments");
        std::cout << triangle_utils_license() << "\n" << triangle_utils_triangle_copyright();
        return EXIT_OK;
    }
    const std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
    if (first == "check") return check_command(rest);
    if (first == "generate") return generate_command(rest);
    if (first == "refine") return refine_command(rest);
    if (first == "improve") return improve_command(rest);
    if (first == "disv") return disv_command(rest);
    if (first == "disu") return disu_command(rest);
    if (first == "cells") return cells_command(rest);
    if (!first.empty() && first[0] == '-')
        throw UsageError(triangle_utils::message("unknown option ", first));
    throw UsageError(triangle_utils::message("unknown command ", first));
}

} // namespace

int main(int argc, char * argv[])
{
    try
    {
        const int status = run(std::vector<std::string>(argv + 1, argv + argc));
        std::cout.flush();
        if (!std::cout) throw triangle_utils::Error("cannot write to standard output");
        return status;
    }
    catch (const UsageError & error)
    {
        std::cerr << PROGRAM << ": " << error.what() << "\n"
                  << "Run '" << PROGRAM << " -h' for help.\n";
        return EXIT_USAGE;
    }
    catch (const std::exception & error)
    {
        std::cerr << PROGRAM << ": error: " << error.what() << "\n";
        return EXIT_ERROR;
    }
    catch (...)
    {
        std::cerr << PROGRAM << ": error: an unknown error occurred\n";
        return EXIT_ERROR;
    }
}
