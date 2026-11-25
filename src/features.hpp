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
// start is that distance along the segments. The segments are joined into
// chains, each from its lowest-numbered vertex (toward its second-lowest,
// if closed), in the order of those vertices, which is the order they were
// digitized in a mesh triangle-utils generates. A segment along a face two
// cells share, as inside a triangle grid, is in only one of them. Throws
// Error if no segment has the marker.
std::vector<CellLength> cells_on_segments(const Grid & grid, const Mesh & mesh, int marker);

// The cell containing each point, such as a well, or -1 outside the grid; a
// point on a face or corner is in the lowest-numbered cell there. Throws
// Error if a point is not finite.
std::vector<int> cells_at_points(const Grid & grid, const std::vector<Point> & points);

} // namespace triangle_utils

#endif
