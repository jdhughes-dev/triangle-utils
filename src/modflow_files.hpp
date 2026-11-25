// Writing MODFLOW 6 files, for the command line; indices in memory are zero
// based and are written from 1.

#ifndef TRIANGLE_UTILS_MODFLOW_FILES_HPP
#define TRIANGLE_UTILS_MODFLOW_FILES_HPP

#include <ostream>
#include <vector>

#include "mesh.hpp"

namespace triangle_utils
{

// A one-layer DISV grid.
struct DisvFile
{
    std::vector<Point> vertices;
    // the vertices of each cell, clockwise
    std::vector<std::vector<int>> cells;
    std::vector<Point> centers;
    double top = 1.0;
    double bottom = 0.0;
};

// Write a MODFLOW 6 DISV file. Throws Error if a cell refers to a vertex
// that does not exist or the counts do not match.
void write_disv(std::ostream & output, const DisvFile & disv);

} // namespace triangle_utils

#endif
