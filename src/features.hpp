// Lines and marked segments mapped to the cells of a grid.

#ifndef TRIANGLE_UTILS_FEATURES_HPP
#define TRIANGLE_UTILS_FEATURES_HPP

#include <vector>

#include "grid.hpp"
#include "mesh.hpp"

namespace triangle_utils
{

// The part of a line in one cell: where it starts along the line, and its
// length.
struct CellLength
{
    int cell = 0;
    double start = 0.0;
    double length = 0.0;
};

// The cells along a line, in order, with the length of line in each; a cell
// the line enters twice appears twice. A part along a face is in the cell
// on the line's left, or on its right where none is on the left, as along
// the boundary. Parts outside the grid are left out.
// Throws Error if the line has fewer than 2 points or a point not finite.
std::vector<CellLength> cells_along_line(const Grid & grid, const std::vector<Point> & line);

// The cells on the mesh's segments with a marker, each once, in order of
// where they first meet the segments, with the length of segment in each;
// start is that distance along the segments, taken in turn. A segment along
// a face two cells share, as inside a triangle grid, is in only one of them.
// Throws Error if no segment has the marker.
std::vector<CellLength> cells_on_segments(const Grid & grid, const Mesh & mesh, int marker);

} // namespace triangle_utils

#endif
