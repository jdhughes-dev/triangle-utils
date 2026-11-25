// The faces cells of a grid share, for MODFLOW DISU.

#ifndef TRIANGLE_UTILS_CONNECTIONS_HPP
#define TRIANGLE_UTILS_CONNECTIONS_HPP

#include <vector>

#include "grid.hpp"

namespace triangle_utils
{

// The connections of each cell to the cells it shares a face with; those of
// cell i are offsets[i] up to offsets[i + 1], in increasing order of the
// other cell, and each is listed from both of its cells.
struct Connections
{
    // cell count + 1 values
    std::vector<int> offsets;
    // the other cell (DISU JA, without the cell itself)
    std::vector<int> cells;
    // the distance from the cell's center to the face, normal to it (CL12)
    std::vector<double> distances;
    // the width of the face (HWVA)
    std::vector<double> widths;
    // the direction of the face's normal out of the cell, in degrees
    // counterclockwise from the x axis, 0 up to 360 (ANGLDEGX)
    std::vector<double> angles;
};

// The connections of a grid's cells; cells that share a face share its two
// corners, so a corner of one cell inside another's side is not a face, and the edges two cells share are one face, with their summed
// width and mean normal. Throws Error if the grid is not valid, an edge is a
// side of more than two cells, two cells overlap, or a center is on a face,
// within 1e-9 of its width, or beyond it, as an obtuse triangle's
// circumcenter is.
Connections connect_cells(const Grid & grid);

} // namespace triangle_utils

#endif
