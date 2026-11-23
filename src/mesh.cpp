// A two-dimensional triangular mesh and its conversion to and from Triangle.

#include "mesh.hpp"

#include <algorithm>
#include <map>
#include <span>
#include <utility>

#include "error.hpp"

namespace triangle_utils
{

namespace
{

// A span over an array Triangle filled, or an empty span when it has none.
template <typename T> std::span<const T> array_of(const T * array, int count, const char * name)
{
    if (count < 0) throw Error(message("Triangle's output has a negative ", name, " count"));
    if (count == 0) return {};
    if (array == nullptr)
        throw Error(message("Triangle's output has ", count, " ", name, " values but no array"));
    return {array, static_cast<std::size_t>(count)};
}

void check_index(int index, std::size_t count, const char * what, std::size_t item)
{
    if (index < 0 || static_cast<std::size_t>(index) >= count)
        throw Error(message(what, " ", item, " refers to vertex ", index, ", but the mesh has ",
                            count, " vertices"));
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
    auto points = array_of(triangle_mesh.pointlist, 2 * vertex_count, "vertex coordinate");
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
    if (triangle_count > 0 && triangle_mesh.numberofcorners != 3)
        throw Error(message("Triangle's output has ", triangle_mesh.numberofcorners,
                            " corners per triangle; only 3 are read"));
    auto corners = array_of(triangle_mesh.trianglelist, 3 * triangle_count, "triangle corner");
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
    auto attributes =
        array_of(triangle_mesh.triangleattributelist,
                 mesh.triangle_attribute_count * triangle_count, "triangle attribute");
    mesh.triangle_attributes.assign(attributes.begin(), attributes.end());

    if (triangle_mesh.neighborlist != nullptr)
    {
        auto neighbors = array_of(triangle_mesh.neighborlist, 3 * triangle_count, "neighbor");
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
    auto ends = array_of(triangle_mesh.segmentlist, 2 * segment_count, "segment end");
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

    triangulateio triangle_mesh{};
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
    triangle_mesh.neighborlist = mesh.neighbors.empty() ? nullptr : mesh.neighbors[0].data();
    triangle_mesh.segmentlist = mesh.segments.empty() ? nullptr : mesh.segments[0].data();
    triangle_mesh.numberofsegments = static_cast<int>(mesh.segments.size());
    triangle_mesh.segmentmarkerlist =
        mesh.segment_markers.empty() ? nullptr : mesh.segment_markers.data();
    return triangle_mesh;
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
        if (area <= 0.0)
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
        }
    }
}

} // namespace triangle_utils
