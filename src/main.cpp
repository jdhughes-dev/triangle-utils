// The triangle-utils command line: reads Triangle's files, calls the C API,
// and writes the results.

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "error.hpp"
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
holes, and regions. Write the mesh to <output>.node, .ele, .neigh, and .poly.

Options:
  -h, --help                show this help
  -o, --output <output>     the output base name (default <base>.1), which
                            must not be <base>
  -a, --maximum-area <a>    the largest triangle area (default 0, none)
  -q, --minimum-angle <q>   the smallest angle in degrees, at most 34
                            (default 0, none)
  -s, --spacing <s>         add points along the segments so none is
                            farther than s from the next (default 0, none)
  -m, --merge-distance <d>  merge points closer than d (default 0: only
                            identical points)
  -D, --conforming-delaunay make every triangle Delaunay, as a Voronoi grid
                            needs
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

// A number given as an option's value.
double option_number(const std::string & option, const std::string & text)
{
    char * end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(value))
        throw UsageError(
            triangle_utils::message("generate: ", option, " needs a number, not \"", text, "\""));
    return value;
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

int generate_command(const std::vector<std::string> & arguments)
{
    std::string output_base;
    double maximum_area = 0.0;
    double minimum_angle = 0.0;
    double spacing = 0.0;
    double merge_distance = 0.0;
    bool conforming_delaunay = false;
    std::vector<std::string> bases;
    for (std::size_t index = 0; index < arguments.size(); index++)
    {
        std::string option = arguments[index];
        std::string value;
        bool has_value = false;
        if (option.starts_with("--") && option.find('=') != std::string::npos)
        {
            value = option.substr(option.find('=') + 1);
            option = option.substr(0, option.find('='));
            has_value = true;
        }
        // the option's value, from after = or the next argument
        auto next_value = [&]()
        {
            if (has_value) return value;
            if (index + 1 >= arguments.size())
                throw UsageError(triangle_utils::message("generate: ", option, " needs a value"));
            return arguments[++index];
        };
        if (option == "-h" || option == "--help")
        {
            std::cout << GENERATE_USAGE;
            return EXIT_OK;
        }
        if (option == "-o" || option == "--output")
            output_base = next_value();
        else if (option == "-a" || option == "--maximum-area")
            maximum_area = option_number(option, next_value());
        else if (option == "-q" || option == "--minimum-angle")
            minimum_angle = option_number(option, next_value());
        else if (option == "-s" || option == "--spacing")
            spacing = option_number(option, next_value());
        else if (option == "-m" || option == "--merge-distance")
            merge_distance = option_number(option, next_value());
        else if (option == "-D" || option == "--conforming-delaunay")
            conforming_delaunay = true;
        else if (option.size() > 1 && option[0] == '-')
            throw UsageError(triangle_utils::message("generate: unknown option ", option));
        else
            bases.push_back(option);
    }
    if (bases.size() != 1) throw UsageError("generate: give one <base>");
    const std::string & base = bases[0];
    if (output_base.empty()) output_base = base + ".1";
    if (std::filesystem::absolute(output_base).lexically_normal() ==
        std::filesystem::absolute(base).lexically_normal())
        throw UsageError("generate: the output would replace the input; give another -o");

    // the .poly file's vertices, or the .node file's if it has none, as
    // Triangle reads them; a .poly file starts with a vertex section
    const std::string poly_name = base + ".poly";
    triangle_utils::VertexFile node;
    const triangle_utils::VertexFile * separate_vertices = nullptr;
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
    std::ifstream poly_file = open_input(poly_name);
    const triangle_utils::PolyFile poly =
        triangle_utils::read_poly(poly_file, poly_name, separate_vertices);
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

    TriangleUtilsMesh * generated = nullptr;
    check_status(triangle_utils_mesh_generate(geometry.get(), maximum_area, minimum_angle,
                                              merge_distance, conforming_delaunay ? 1 : 0,
                                              &generated));
    const MeshHandle mesh(generated);
    write_mesh(mesh.get(), output_base, vertices.first_number, poly);
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
