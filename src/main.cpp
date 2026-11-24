// The triangle-utils command line: reads Triangle's files, calls the C API,
// and writes the results.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
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
  check    check a mesh in Triangle's .node, .ele, and .neigh files

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
