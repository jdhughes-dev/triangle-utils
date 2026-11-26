// Layers of a grid and their MODFLOW DISV and DISU data.

#ifndef TRIANGLE_UTILS_LAYERS_HPP
#define TRIANGLE_UTILS_LAYERS_HPP

#include <vector>

#include "connections.hpp"
#include "grid.hpp"
#include "mesh.hpp"

namespace triangle_utils
{

// The layers of a grid, each with the grid's cells; values of each cell in
// each layer are layer by layer (layer * cell_count + cell), as DISV lists
// them.
struct Layers
{
    int layer_count = 0;
    int cell_count = 0;
    // the top of each cell
    std::vector<double> top;
    // the bottom of each cell in each layer
    std::vector<double> bottoms;
    // > 0 active, 0 not in the model, < 0 vertical pass-through
    std::vector<int> idomain;
};

// DISU data of layers: a node for each active cell, numbered layer by layer.
// The connections of node n are offsets[n] up to offsets[n + 1], the first
// to n itself and the rest in increasing order of the other node.
struct Disu
{
    // the cell of each node in the layers (layer * cell_count + cell)
    std::vector<int> cells;
    std::vector<double> top;
    std::vector<double> bottom;
    std::vector<double> area;
    // node count + 1 values; IAC is the difference of each pair
    std::vector<int> offsets;
    // JA
    std::vector<int> nodes;
    // IHC: 0 vertical, 1 horizontal
    std::vector<int> directions;
    // CL12, HWVA, and ANGLDEGX; 0 for a node's connection to itself
    std::vector<double> distances;
    std::vector<double> widths;
    std::vector<double> angles;
};

// Layers of a grid, from the top of each cell and the bottom of each cell in
// each layer; idomain is all 1 if empty. A cell's top is the bottom of the
// cell above it, as in MODFLOW. An active cell thinner than
// minimum_thickness, if it is positive, becomes a pass-through cell. Throws
// Error if a size does not match, a value is not finite, minimum_thickness
// is negative, or an active cell is not thicker than 0, naming the cell and
// layer.
Layers layer_grid(const Grid & grid, int layer_count, const std::vector<double> & top,
                  const std::vector<double> & bottoms, const std::vector<int> & idomain,
                  double minimum_thickness);

// The DISU data of layers of a grid with its connections. A node connects
// to the active cells beside it in its layer and to the active cells above
// and below it, through any pass-through cells between, as MODFLOW 6
// connects DISV cells; a vertical connection's CL12 is half of each node's
// thickness and its HWVA the cell's area.
Disu layer_disu(const Grid & grid, const Connections & connections, const Layers & layers);

// The value at each cell's center, interpolated linearly from values at the
// mesh's vertices over the triangle that holds it; a center on a vertex, as
// in a Voronoi grid, gets the vertex's value. Throws Error if the values do
// not match the vertices or are not finite, or a center is outside the mesh.
std::vector<double> values_at_centers(const Grid & grid, const Mesh & mesh,
                                      const std::vector<double> & vertex_values);

} // namespace triangle_utils

#endif
