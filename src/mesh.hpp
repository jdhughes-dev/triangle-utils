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
    // attributes of each triangle, triangle_attribute_count values per
    // triangle, such as a region attribute
    int triangle_attribute_count = 0;
    std::vector<double> triangle_attributes;
    // for each triangle, the neighbor opposite each corner, or NO_NEIGHBOR,
    // or empty
    std::vector<std::array<int, 3>> neighbors;
    // boundary segments and a marker for each, or empty
    std::vector<std::array<int, 2>> segments;
    std::vector<int> segment_markers;
};

// A triangulateio for Triangle's output, whose arrays are freed when it goes
// out of scope.
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

// A triangulateio that points into the mesh's arrays, for Triangle's input;
// valid while the mesh is unchanged.
triangulateio to_triangulateio(Mesh & mesh);

// Signed area of a triangle; positive when its corners are counterclockwise.
double signed_area(const Mesh & mesh, std::size_t triangle);

// Sum of the triangle areas.
double total_area(const Mesh & mesh);

// Throw Error if a triangle is not counterclockwise, has a zero area, or
// refers to a vertex that does not exist.
void check_triangles(const Mesh & mesh);

// Throw Error if a triangle's neighbor does not share the edge opposite the
// corner or does not have the triangle as its neighbor, or if an edge two
// triangles share has no neighbor.
void check_neighbors(const Mesh & mesh);

} // namespace triangle_utils

#endif
