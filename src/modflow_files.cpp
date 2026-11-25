// Writing MODFLOW 6 files, for the command line.

#include "modflow_files.hpp"

#include <limits>

#include "error.hpp"

namespace triangle_utils
{

void write_disv(std::ostream & output, const DisvFile & disv)
{
    if (disv.centers.size() != disv.cells.size())
        throw Error(message("the grid has ", disv.cells.size(), " cells but ", disv.centers.size(),
                            " centers"));
    for (std::size_t cell = 0; cell < disv.cells.size(); cell++)
        for (const int vertex : disv.cells[cell])
            if (vertex < 0 || static_cast<std::size_t>(vertex) >= disv.vertices.size())
                throw Error(message("cell ", cell, " refers to vertex ", vertex,
                                    ", but the grid has ", disv.vertices.size(), " vertices"));
    output.precision(std::numeric_limits<double>::max_digits10);
    output << "# MODFLOW 6 DISV written by triangle-utils\n"
           << "BEGIN OPTIONS\nEND OPTIONS\n\n"
           << "BEGIN DIMENSIONS\n"
           << "  NLAY 1\n"
           << "  NCPL " << disv.cells.size() << "\n"
           << "  NVERT " << disv.vertices.size() << "\n"
           << "END DIMENSIONS\n\n"
           << "BEGIN GRIDDATA\n"
           << "  TOP\n    CONSTANT " << disv.top << "\n"
           << "  BOTM LAYERED\n    CONSTANT " << disv.bottom << "\n"
           << "END GRIDDATA\n\n"
           << "BEGIN VERTICES\n";
    for (std::size_t vertex = 0; vertex < disv.vertices.size(); vertex++)
        output << "  " << vertex + 1 << " " << disv.vertices[vertex].x << " "
               << disv.vertices[vertex].y << "\n";
    output << "END VERTICES\n\nBEGIN CELL2D\n";
    for (std::size_t cell = 0; cell < disv.cells.size(); cell++)
    {
        output << "  " << cell + 1 << " " << disv.centers[cell].x << " " << disv.centers[cell].y
               << " " << disv.cells[cell].size();
        for (const int vertex : disv.cells[cell]) output << " " << vertex + 1;
        output << "\n";
    }
    output << "END CELL2D\n";
    if (!output) throw Error("the DISV file could not be written");
}

} // namespace triangle_utils
