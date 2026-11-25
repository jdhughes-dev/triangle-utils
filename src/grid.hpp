// Grids of polygonal cells built from a mesh, for MODFLOW 6 DISV.

#ifndef TRIANGLE_UTILS_GRID_HPP
#define TRIANGLE_UTILS_GRID_HPP

#include <vector>

#include "mesh.hpp"

namespace triangle_utils
{

// A grid of polygonal cells; indices are zero based.
struct Grid
{
    // each vertex once
    std::vector<Point> vertices;
    // the vertices of each cell, clockwise, as DISV lists them
    std::vector<std::vector<int>> cells;
    // the center of each cell
    std::vector<Point> centers;
    // the mesh vertex (Voronoi) or triangle (triangle grid) of each cell
    std::vector<int> sources;
    // number of attributes of each cell, from the mesh's triangles
    int attribute_count = 0;
    // attribute_count values for each cell in turn
    std::vector<double> attributes;
};

// Where a triangle cell is centered.
enum class TriangleCenter
{
    CENTROID,
    CIRCUMCENTER,
};

// The Voronoi grid of a conforming Delaunay mesh: a cell around each
// vertex, centered on it; a boundary vertex's cell is not convex where the
// boundary turns inward. Throws Error if the mesh is not Delaunay.
Grid voronoi_grid(const Mesh & mesh);

// A grid whose cells are the mesh's triangles.
Grid triangle_grid(const Mesh & mesh, TriangleCenter center);

// Throw Error if a point is not finite, a cell has fewer than 3 corners or
// refers to a vertex that does not exist, or an array's size does not match
// the cell count.
void check_grid(const Grid & grid);

} // namespace triangle_utils

#endif
