// A two-dimensional triangular mesh and its conversion to and from Triangle.

#ifndef TRIANGLE_UTILS_MESH_HPP
#define TRIANGLE_UTILS_MESH_HPP

#include <array>
#include <cstddef>
#include <vector>

#include "triangle_status.h"

namespace triangle_utils
{

// A vertex position.
struct Point
{
    double x = 0.0;
    double y = 0.0;
};

// No neighbor across a triangle's edge.
constexpr int NO_NEIGHBOR = -1;

// A triangular mesh; indices are zero based.
struct Mesh
{
    std::vector<Point> vertices;
    // a boundary marker for each vertex, or empty
    std::vector<int> vertex_markers;
    // corners of each triangle, counterclockwise
    std::vector<std::array<int, 3>> triangles;
    // number of attributes of each triangle, such as a region attribute
    int triangle_attribute_count = 0;
    // triangle_attribute_count values for each triangle in turn
    std::vector<double> triangle_attributes;
    // an area limit for each triangle, for refinement (r and a), or empty
    std::vector<double> triangle_area_limits;
    // for each triangle, the neighbor opposite each corner, or NO_NEIGHBOR,
    // or empty
    std::vector<std::array<int, 3>> neighbors;
    // boundary segments and a marker for each, or empty
    std::vector<std::array<int, 2>> segments;
    std::vector<int> segment_markers;
    // vertices that must not move when the mesh is improved, besides those on
    // segments and the boundary, such as wells
    std::vector<int> fixed_vertices;
};

// A triangulateio for Triangle's output, or its Voronoi output, whose arrays
// are freed when it goes out of scope.
class TriangleOutput
{
public:
    TriangleOutput() = default;
    TriangleOutput(const TriangleOutput &) = delete;
    TriangleOutput & operator=(const TriangleOutput &) = delete;
    ~TriangleOutput();

    triangulateio & get() { return m_output; }
    const triangulateio & get() const { return m_output; }

private:
    triangulateio m_output{};
};

// Copy a mesh from Triangle's output, which must be numbered from zero (z).
Mesh from_triangulateio(const triangulateio & triangle_mesh);

// A triangulateio that points into the mesh's arrays, for Triangle's input
// with the z switch; valid while the mesh is unchanged. Throws Error if a
// size or an index does not fit the mesh.
triangulateio to_triangulateio(Mesh & mesh);

// Throw Error if an array's size does not match the vertex, triangle, or
// segment count, or a count does not fit Triangle's int.
void check_sizes(const Mesh & mesh);

// Signed area of a triangle; positive when its corners are counterclockwise.
// The triangle and its corners must exist (check_triangles).
double signed_area(const Mesh & mesh, std::size_t triangle);

// Sum of the signed triangle areas; assumes check_triangles passes.
double total_area(const Mesh & mesh);

// Throw Error if a triangle refers to a vertex that does not exist or its
// area, computed in floating point rather than exactly, is not positive.
void check_triangles(const Mesh & mesh);

// Throw Error if a triangle's neighbor is not the triangle across the edge
// opposite the corner, the two run along the edge the same way, or an edge
// is shared by more than two triangles; assumes check_triangles passes.
void check_neighbors(const Mesh & mesh);

} // namespace triangle_utils

#endif
