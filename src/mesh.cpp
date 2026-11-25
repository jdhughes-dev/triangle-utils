// A two-dimensional triangular mesh and its conversion to and from Triangle.

#include "mesh.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <utility>

#include "error.hpp"

namespace triangle_utils
{

namespace
{

// A span over an array Triangle filled, or an empty span when it has none;
// the count is wide so that products of Triangle's counts do not overflow.
template <typename T>
std::span<const T> array_of(const T * array, std::int64_t count, const char * name)
{
    if (count < 0) throw Error(message("Triangle's output has a negative ", name, " count"));
    if (count == 0) return {};
    if (array == nullptr)
        throw Error(message("Triangle's output has ", count, " ", name, " values but no array"));
    return {array, static_cast<std::size_t>(count)};
}

// the largest vertex, triangle, or segment count whose index products (up
// to 3 * count) fit Triangle's int
constexpr std::int64_t MAX_TRIANGLE_COUNT = std::numeric_limits<int>::max() / 3;

void check_count(int count, const char * plural_name)
{
    if (count > MAX_TRIANGLE_COUNT)
        throw Error(message("Triangle's output has ", count, " ", plural_name, "; at most ",
                            MAX_TRIANGLE_COUNT, " can be read"));
}

void check_index(int index, std::size_t count, const char * what, std::size_t item)
{
    if (index < 0 || static_cast<std::size_t>(index) >= count)
        throw Error(message(what, " ", item, " refers to vertex ", index, ", but the mesh has ",
                            count, " vertices"));
}

// Whether a triangle's corners include the edge from start to end, in order.
bool has_directed_edge(const std::array<int, 3> & corners, int start, int end)
{
    for (int corner = 0; corner < 3; corner++)
    {
        if (corners[corner] == start && corners[(corner + 1) % 3] == end) return true;
    }
    return false;
}

} // namespace

TriangleOutput::~TriangleOutput()
{
    // arrays Triangle did not allocate are null; the hole and region lists
    // point to the input's arrays and are not freed
    for (void * array :
         {static_cast<void *>(m_output.pointlist), static_cast<void *>(m_output.pointattributelist),
          static_cast<void *>(m_output.pointmarkerlist), static_cast<void *>(m_output.trianglelist),
          static_cast<void *>(m_output.triangleattributelist),
          static_cast<void *>(m_output.trianglearealist),
          static_cast<void *>(m_output.neighborlist), static_cast<void *>(m_output.segmentlist),
          static_cast<void *>(m_output.segmentmarkerlist), static_cast<void *>(m_output.edgelist),
          static_cast<void *>(m_output.edgemarkerlist), static_cast<void *>(m_output.normlist)})
    {
        triangle_free(array);
    }
}

Mesh from_triangulateio(const triangulateio & triangle_mesh)
{
    Mesh mesh;
    const int vertex_count = triangle_mesh.numberofpoints;
    check_count(vertex_count, "vertices");
    auto points =
        array_of(triangle_mesh.pointlist, std::int64_t{2} * vertex_count, "vertex coordinate");
    mesh.vertices.resize(vertex_count);
    for (int vertex = 0; vertex < vertex_count; vertex++)
    {
        mesh.vertices[vertex] = {points[2 * vertex], points[2 * vertex + 1]};
    }
    if (triangle_mesh.pointmarkerlist != nullptr)
    {
        auto markers = array_of(triangle_mesh.pointmarkerlist, vertex_count, "vertex marker");
        mesh.vertex_markers.assign(markers.begin(), markers.end());
    }

    const int triangle_count = triangle_mesh.numberoftriangles;
    check_count(triangle_count, "triangles");
    if (triangle_count > 0 && triangle_mesh.numberofcorners != 3)
        throw Error(message("Triangle's output has ", triangle_mesh.numberofcorners,
                            " corners per triangle; only 3 are read"));
    auto corners =
        array_of(triangle_mesh.trianglelist, std::int64_t{3} * triangle_count, "triangle corner");
    mesh.triangles.resize(triangle_count);
    for (int triangle = 0; triangle < triangle_count; triangle++)
    {
        for (int corner = 0; corner < 3; corner++)
        {
            int vertex = corners[3 * triangle + corner];
            check_index(vertex, mesh.vertices.size(), "triangle", triangle);
            mesh.triangles[triangle][corner] = vertex;
        }
    }

    mesh.triangle_attribute_count = triangle_mesh.numberoftriangleattributes;
    if (mesh.triangle_attribute_count < 0)
        throw Error("Triangle's output has a negative triangle attribute count");
    auto attributes = array_of(triangle_mesh.triangleattributelist,
                               std::int64_t{mesh.triangle_attribute_count} * triangle_count,
                               "triangle attribute");
    mesh.triangle_attributes.assign(attributes.begin(), attributes.end());
    if (triangle_mesh.trianglearealist != nullptr)
    {
        auto limits = array_of(triangle_mesh.trianglearealist, triangle_count, "area limit");
        mesh.triangle_area_limits.assign(limits.begin(), limits.end());
    }

    if (triangle_mesh.neighborlist != nullptr)
    {
        auto neighbors =
            array_of(triangle_mesh.neighborlist, std::int64_t{3} * triangle_count, "neighbor");
        mesh.neighbors.resize(triangle_count);
        for (int triangle = 0; triangle < triangle_count; triangle++)
        {
            for (int corner = 0; corner < 3; corner++)
            {
                int neighbor = neighbors[3 * triangle + corner];
                if (neighbor < NO_NEIGHBOR || neighbor >= triangle_count)
                    throw Error(message("triangle ", triangle, " has neighbor ", neighbor,
                                        ", but the mesh has ", triangle_count, " triangles"));
                mesh.neighbors[triangle][corner] = neighbor;
            }
        }
    }

    const int segment_count = triangle_mesh.numberofsegments;
    check_count(segment_count, "segments");
    auto ends = array_of(triangle_mesh.segmentlist, std::int64_t{2} * segment_count, "segment end");
    mesh.segments.resize(segment_count);
    for (int segment = 0; segment < segment_count; segment++)
    {
        for (int end = 0; end < 2; end++)
        {
            int vertex = ends[2 * segment + end];
            check_index(vertex, mesh.vertices.size(), "segment", segment);
            mesh.segments[segment][end] = vertex;
        }
    }
    if (triangle_mesh.segmentmarkerlist != nullptr)
    {
        auto markers = array_of(triangle_mesh.segmentmarkerlist, segment_count, "segment marker");
        mesh.segment_markers.assign(markers.begin(), markers.end());
    }
    return mesh;
}

triangulateio to_triangulateio(Mesh & mesh)
{
    // Point, std::array<int, 3>, and std::array<int, 2> hold their values
    // contiguously, as Triangle's flat arrays do
    static_assert(sizeof(Point) == 2 * sizeof(double));
    static_assert(sizeof(std::array<int, 3>) == 3 * sizeof(int));
    static_assert(sizeof(std::array<int, 2>) == 2 * sizeof(int));
    check_sizes(mesh);
    // Triangle reads through every index, so they must refer to the mesh
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        for (int vertex : mesh.triangles[triangle])
        {
            check_index(vertex, mesh.vertices.size(), "triangle", triangle);
        }
    }
    for (std::size_t segment = 0; segment < mesh.segments.size(); segment++)
    {
        for (int vertex : mesh.segments[segment])
        {
            check_index(vertex, mesh.vertices.size(), "segment", segment);
        }
    }
    for (std::size_t triangle = 0; triangle < mesh.neighbors.size(); triangle++)
    {
        for (int neighbor : mesh.neighbors[triangle])
        {
            if (neighbor < NO_NEIGHBOR || neighbor >= static_cast<int>(mesh.triangles.size()))
                throw Error(message("triangle ", triangle, " has neighbor ", neighbor,
                                    ", but the mesh has ", mesh.triangles.size(), " triangles"));
        }
    }

    triangulateio triangle_mesh{};
    // each array's first element; Triangle reads the rows after it as one
    // flat array, which the layout checks above allow
    triangle_mesh.pointlist = mesh.vertices.empty() ? nullptr : &mesh.vertices[0].x;
    triangle_mesh.numberofpoints = static_cast<int>(mesh.vertices.size());
    triangle_mesh.pointmarkerlist =
        mesh.vertex_markers.empty() ? nullptr : mesh.vertex_markers.data();
    triangle_mesh.trianglelist = mesh.triangles.empty() ? nullptr : mesh.triangles[0].data();
    triangle_mesh.numberoftriangles = static_cast<int>(mesh.triangles.size());
    triangle_mesh.numberofcorners = 3;
    triangle_mesh.triangleattributelist =
        mesh.triangle_attributes.empty() ? nullptr : mesh.triangle_attributes.data();
    triangle_mesh.numberoftriangleattributes = mesh.triangle_attribute_count;
    triangle_mesh.trianglearealist =
        mesh.triangle_area_limits.empty() ? nullptr : mesh.triangle_area_limits.data();
    triangle_mesh.neighborlist = mesh.neighbors.empty() ? nullptr : mesh.neighbors[0].data();
    triangle_mesh.segmentlist = mesh.segments.empty() ? nullptr : mesh.segments[0].data();
    triangle_mesh.numberofsegments = static_cast<int>(mesh.segments.size());
    triangle_mesh.segmentmarkerlist =
        mesh.segment_markers.empty() ? nullptr : mesh.segment_markers.data();
    return triangle_mesh;
}

void check_sizes(const Mesh & mesh)
{
    constexpr auto MAX_COUNT = static_cast<std::size_t>(MAX_TRIANGLE_COUNT);
    if (mesh.vertices.size() > MAX_COUNT || mesh.triangles.size() > MAX_COUNT ||
        mesh.segments.size() > MAX_COUNT)
        throw Error(message("the mesh has more than ", MAX_COUNT,
                            " vertices, triangles, or segments, which Triangle cannot index"));
    auto check = [](std::size_t size, std::size_t expected, const char * what)
    {
        if (size != 0 && size != expected)
            throw Error(
                message("the mesh has ", size, " ", what, ", but ", expected, " are needed"));
    };
    check(mesh.vertex_markers.size(), mesh.vertices.size(), "vertex markers");
    check(mesh.neighbors.size(), mesh.triangles.size(), "neighbor rows");
    check(mesh.triangle_area_limits.size(), mesh.triangles.size(), "area limits");
    check(mesh.segment_markers.size(), mesh.segments.size(), "segment markers");
    if (mesh.triangle_attribute_count < 0)
        throw Error("the mesh has a negative triangle attribute count");
    if (mesh.triangle_attributes.size() !=
        static_cast<std::size_t>(mesh.triangle_attribute_count) * mesh.triangles.size())
        throw Error(message("the mesh has ", mesh.triangle_attributes.size(),
                            " triangle attributes, but ", mesh.triangle_attribute_count,
                            " for each of ", mesh.triangles.size(), " triangles are needed"));
    for (const int vertex : mesh.fixed_vertices)
        if (vertex < 0 || static_cast<std::size_t>(vertex) >= mesh.vertices.size())
            throw Error(message("fixed vertex ", vertex, " is not a vertex of the mesh, which has ",
                                mesh.vertices.size()));
}

double signed_area(const Mesh & mesh, std::size_t triangle)
{
    const auto & corners = mesh.triangles[triangle];
    const Point & first = mesh.vertices[corners[0]];
    const Point & second = mesh.vertices[corners[1]];
    const Point & third = mesh.vertices[corners[2]];
    return 0.5 * ((second.x - first.x) * (third.y - first.y) -
                  (third.x - first.x) * (second.y - first.y));
}

double total_area(const Mesh & mesh)
{
    double area = 0.0;
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        area += signed_area(mesh, triangle);
    }
    return area;
}

void check_triangles(const Mesh & mesh)
{
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        for (int vertex : mesh.triangles[triangle])
        {
            check_index(vertex, mesh.vertices.size(), "triangle", triangle);
        }
        double area = signed_area(mesh, triangle);
        // the negation also rejects a NaN area
        if (!(area > 0.0))
            throw Error(message("triangle ", triangle, " is not counterclockwise (signed area ",
                                area, ")"));
    }
}

void check_neighbors(const Mesh & mesh)
{
    if (mesh.neighbors.size() != mesh.triangles.size())
        throw Error(message("the mesh has neighbors for ", mesh.neighbors.size(), " of its ",
                            mesh.triangles.size(), " triangles"));

    // the triangles on each edge, keyed by its vertices in increasing order
    std::map<std::pair<int, int>, std::vector<int>> edge_triangles;
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        const auto & corners = mesh.triangles[triangle];
        for (int corner = 0; corner < 3; corner++)
        {
            // the edge opposite the corner
            int first = corners[(corner + 1) % 3];
            int second = corners[(corner + 2) % 3];
            edge_triangles[std::minmax(first, second)].push_back(static_cast<int>(triangle));
        }
    }

    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        const auto & corners = mesh.triangles[triangle];
        for (int corner = 0; corner < 3; corner++)
        {
            int first = corners[(corner + 1) % 3];
            int second = corners[(corner + 2) % 3];
            const auto & sharing = edge_triangles[std::minmax(first, second)];
            if (sharing.size() > 2)
                throw Error(message("edge ", first, "-", second, " is shared by ", sharing.size(),
                                    " triangles"));
            int expected = NO_NEIGHBOR;
            for (int other : sharing)
            {
                if (other != static_cast<int>(triangle)) expected = other;
            }
            int neighbor = mesh.neighbors[triangle][corner];
            if (neighbor != expected)
                throw Error(message("triangle ", triangle, " has neighbor ", neighbor,
                                    " opposite corner ", corner,
                                    ", but the triangle across that edge is ", expected));
            // a neighbor runs along the edge the other way; the same way means
            // the two triangles overlap
            if (expected != NO_NEIGHBOR &&
                !has_directed_edge(mesh.triangles[expected], second, first))
                throw Error(message("triangles ", triangle, " and ", expected,
                                    " overlap along edge ", first, "-", second));
        }
    }
}

} // namespace triangle_utils
