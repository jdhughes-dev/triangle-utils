// Writing MODFLOW 6 and MODFLOW-USG files, for the command line; indices in
// memory are zero based and are written from 1.

#ifndef TRIANGLE_UTILS_MODFLOW_FILES_HPP
#define TRIANGLE_UTILS_MODFLOW_FILES_HPP

#include <ostream>
#include <vector>

#include "mesh.hpp"

namespace triangle_utils
{

// The cells of a grid, as DISV lists them.
struct GridCells
{
    std::vector<Point> vertices;
    // the vertices of each cell, clockwise
    std::vector<std::vector<int>> cells;
    std::vector<Point> centers;
};

// A layered DISV grid; values of each cell in each layer are layer by layer
// (layer * cell_count + cell).
struct DisvFile
{
    GridCells grid;
    int layer_count = 1;
    // the top of each cell
    std::vector<double> top;
    std::vector<double> bottoms;
    std::vector<int> idomain;
};

// A DISU grid of layers: a node for each active cell, numbered layer by
// layer, and its connections, the first to itself.
struct DisuFile
{
    GridCells grid;
    int layer_count = 1;
    // the cell of each node in the layers (layer * cell_count + cell)
    std::vector<int> node_cells;
    std::vector<double> top;
    std::vector<double> bottom;
    std::vector<double> area;
    // node count + 1 values; IAC is each difference
    std::vector<int> offsets;
    // JA, IHC (0 vertical, 1 horizontal), CL12, HWVA, and ANGLDEGX
    std::vector<int> nodes;
    std::vector<int> directions;
    std::vector<double> distances;
    std::vector<double> widths;
    std::vector<double> angles;
};

// Write a MODFLOW 6 DISV file. Throws Error if a cell refers to a vertex
// that does not exist or the counts do not match.
void write_disv(std::ostream & output, const DisvFile & disv);

// Write a MODFLOW 6 DISU file, with each node's cell in VERTICES and CELL2D.
// Throws Error if the counts do not match.
void write_disu(std::ostream & output, const DisuFile & disu);

// Write a MODFLOW-USG DISU file, with one steady-state stress period.
// Throws Error if the counts do not match or a layer has no nodes.
void write_usg_disu(std::ostream & output, const DisuFile & disu);

} // namespace triangle_utils

#endif
