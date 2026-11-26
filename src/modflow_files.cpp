// Writing MODFLOW 6 and MODFLOW-USG files, for the command line.

#include "modflow_files.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>

#include "error.hpp"

namespace triangle_utils
{

namespace
{

// values on each line of an INTERNAL array
constexpr std::size_t VALUES_PER_LINE = 10;

void check_cells(const GridCells & grid)
{
    if (grid.centers.size() != grid.cells.size())
        throw Error(message("the grid has ", grid.cells.size(), " cells but ", grid.centers.size(),
                            " centers"));
    for (std::size_t cell = 0; cell < grid.cells.size(); cell++)
        for (const int vertex : grid.cells[cell])
            if (vertex < 0 || static_cast<std::size_t>(vertex) >= grid.vertices.size())
                throw Error(message("cell ", cell, " refers to vertex ", vertex,
                                    ", but the grid has ", grid.vertices.size(), " vertices"));
}

// Write values as a CONSTANT if they are all the same, or INTERNAL; added
// is 1 for indices written from 1.
template <typename T>
void write_values(std::ostream & output, std::span<const T> values, T added = 0)
{
    if (!values.empty() &&
        std::all_of(values.begin(), values.end(), [&](T value) { return value == values[0]; }))
    {
        output << "    CONSTANT " << values[0] + added << "\n";
        return;
    }
    output << "    INTERNAL\n";
    for (std::size_t i = 0; i < values.size(); i++)
        output << (i % VALUES_PER_LINE == 0 ? "      " : " ") << values[i] + added
               << (i % VALUES_PER_LINE == VALUES_PER_LINE - 1 || i + 1 == values.size() ? "\n"
                                                                                        : "");
}

void write_vertices(std::ostream & output, const GridCells & grid)
{
    output << "BEGIN VERTICES\n";
    for (std::size_t vertex = 0; vertex < grid.vertices.size(); vertex++)
        output << "  " << vertex + 1 << " " << grid.vertices[vertex].x << " "
               << grid.vertices[vertex].y << "\n";
    output << "END VERTICES\n\n";
}

// A CELL2D line of cell, numbered number.
void write_cell2d(std::ostream & output, const GridCells & grid, std::size_t number,
                  std::size_t cell)
{
    output << "  " << number << " " << grid.centers[cell].x << " " << grid.centers[cell].y << " "
           << grid.cells[cell].size();
    for (const int vertex : grid.cells[cell]) output << " " << vertex + 1;
    output << "\n";
}

// Throw Error if the DISU values do not match.
void check_disu(const DisuFile & disu)
{
    check_cells(disu.grid);
    const std::size_t cell_count = disu.grid.cells.size();
    const std::size_t node_count = disu.node_cells.size();
    const std::size_t connection_count = disu.nodes.size();
    if (disu.top.size() != node_count || disu.bottom.size() != node_count ||
        disu.area.size() != node_count || disu.offsets.size() != node_count + 1 ||
        disu.directions.size() != connection_count || disu.distances.size() != connection_count ||
        disu.widths.size() != connection_count || disu.angles.size() != connection_count)
        throw Error(message("the DISU values do not match ", node_count, " nodes and ",
                            connection_count, " connections"));
    for (const int cell : disu.node_cells)
        if (cell < 0 || static_cast<std::size_t>(cell) >=
                            static_cast<std::size_t>(disu.layer_count) * cell_count)
            throw Error(message("a node's cell ", cell, " is not in ", disu.layer_count,
                                " layers of ", cell_count, " cells"));
}

// Write values as MODFLOW-USG reads an array: a CONSTANT if they are all the
// same, or INTERNAL in free format; added is 1 for indices written from 1.
template <typename T>
void write_usg_values(std::ostream & output, std::span<const T> values, T added = 0)
{
    if (!values.empty() &&
        std::all_of(values.begin(), values.end(), [&](T value) { return value == values[0]; }))
    {
        output << "CONSTANT " << values[0] + added << "\n";
        return;
    }
    output << "INTERNAL 1 (FREE) -1\n";
    for (std::size_t i = 0; i < values.size(); i++)
        output << (i % VALUES_PER_LINE == 0 ? "" : " ") << values[i] + added
               << (i % VALUES_PER_LINE == VALUES_PER_LINE - 1 || i + 1 == values.size() ? "\n"
                                                                                        : "");
}

} // namespace

void write_disv(std::ostream & output, const DisvFile & disv)
{
    check_cells(disv.grid);
    const std::size_t cell_count = disv.grid.cells.size();
    const std::size_t value_count = static_cast<std::size_t>(disv.layer_count) * cell_count;
    if (disv.layer_count < 1 || disv.top.size() != cell_count ||
        disv.bottoms.size() != value_count || disv.idomain.size() != value_count)
        throw Error(message("the DISV values do not match ", disv.layer_count, " layers of ",
                            cell_count, " cells"));
    output.precision(std::numeric_limits<double>::max_digits10);
    output << "# MODFLOW 6 DISV written by triangle-utils\n"
           << "BEGIN OPTIONS\nEND OPTIONS\n\n"
           << "BEGIN DIMENSIONS\n"
           << "  NLAY " << disv.layer_count << "\n"
           << "  NCPL " << cell_count << "\n"
           << "  NVERT " << disv.grid.vertices.size() << "\n"
           << "END DIMENSIONS\n\n"
           << "BEGIN GRIDDATA\n"
           << "  TOP\n";
    write_values<double>(output, disv.top);
    output << "  BOTM LAYERED\n";
    for (std::size_t layer = 0; layer < static_cast<std::size_t>(disv.layer_count); layer++)
        write_values<double>(output,
                             std::span(disv.bottoms).subspan(layer * cell_count, cell_count));
    output << "  IDOMAIN LAYERED\n";
    for (std::size_t layer = 0; layer < static_cast<std::size_t>(disv.layer_count); layer++)
        write_values<int>(output, std::span(disv.idomain).subspan(layer * cell_count, cell_count));
    output << "END GRIDDATA\n\n";
    write_vertices(output, disv.grid);
    output << "BEGIN CELL2D\n";
    for (std::size_t cell = 0; cell < cell_count; cell++)
        write_cell2d(output, disv.grid, cell + 1, cell);
    output << "END CELL2D\n";
    if (!output) throw Error("the DISV file could not be written");
}

void write_disu(std::ostream & output, const DisuFile & disu)
{
    check_disu(disu);
    const std::size_t cell_count = disu.grid.cells.size();
    const std::size_t node_count = disu.node_cells.size();
    const std::size_t connection_count = disu.nodes.size();
    std::vector<int> iac(node_count);
    for (std::size_t node = 0; node < node_count; node++)
        iac[node] = disu.offsets[node + 1] - disu.offsets[node];

    output.precision(std::numeric_limits<double>::max_digits10);
    output << "# MODFLOW 6 DISU written by triangle-utils, " << disu.layer_count << " layers of "
           << cell_count << " cells\n"
           << "BEGIN OPTIONS\nEND OPTIONS\n\n"
           << "BEGIN DIMENSIONS\n"
           << "  NODES " << node_count << "\n"
           << "  NJA " << connection_count << "\n"
           << "  NVERT " << disu.grid.vertices.size() << "\n"
           << "END DIMENSIONS\n\n"
           << "BEGIN GRIDDATA\n"
           << "  TOP\n";
    write_values<double>(output, disu.top);
    output << "  BOT\n";
    write_values<double>(output, disu.bottom);
    output << "  AREA\n";
    write_values<double>(output, disu.area);
    output << "END GRIDDATA\n\n"
           << "BEGIN CONNECTIONDATA\n"
           << "  IAC\n";
    write_values<int>(output, iac);
    output << "  JA\n";
    write_values<int>(output, disu.nodes, 1);
    output << "  IHC\n";
    write_values<int>(output, disu.directions);
    output << "  CL12\n";
    write_values<double>(output, disu.distances);
    output << "  HWVA\n";
    write_values<double>(output, disu.widths);
    output << "  ANGLDEGX\n";
    write_values<double>(output, disu.angles);
    output << "END CONNECTIONDATA\n\n";
    write_vertices(output, disu.grid);
    output << "BEGIN CELL2D\n";
    for (std::size_t node = 0; node < node_count; node++)
        write_cell2d(output, disu.grid, node + 1, disu.node_cells[node] % cell_count);
    output << "END CELL2D\n";
    if (!output) throw Error("the DISU file could not be written");
}

void write_usg_disu(std::ostream & output, const DisuFile & disu)
{
    check_disu(disu);
    const std::size_t cell_count = disu.grid.cells.size();
    const std::size_t node_count = disu.node_cells.size();
    const std::size_t connection_count = disu.nodes.size();
    // the nodes of each layer, which are numbered layer by layer
    std::vector<int> layer_nodes(static_cast<std::size_t>(disu.layer_count));
    for (const int cell : disu.node_cells) layer_nodes[cell / cell_count]++;
    for (std::size_t layer = 0; layer < layer_nodes.size(); layer++)
        if (layer_nodes[layer] == 0)
            throw Error(message("layer ", layer, " has no active cells, which MODFLOW-USG needs"));
    std::vector<int> iac(node_count);
    for (std::size_t node = 0; node < node_count; node++)
        iac[node] = disu.offsets[node + 1] - disu.offsets[node];
    // FAHL is the area of each face: a vertical face's is HWVA, and a
    // horizontal face's its width times the mean of the two nodes' thickness,
    // which MODFLOW-USG divides by again
    std::vector<double> face_areas(connection_count);
    for (std::size_t node = 0; node < node_count; node++)
        for (int c = disu.offsets[node]; c < disu.offsets[node + 1]; c++)
        {
            const int other = disu.nodes[c];
            const double mean_thickness =
                0.5 * (disu.top[node] - disu.bottom[node] + disu.top[other] - disu.bottom[other]);
            face_areas[c] =
                disu.directions[c] == 1 ? disu.widths[c] * mean_thickness : disu.widths[c];
        }

    output.precision(std::numeric_limits<double>::max_digits10);
    // NODES NLAY NJAG IVSD (0, so MODFLOW-USG takes a connection to a later
    // layer as vertical) NPER ITMUNI LENUNI IDSYMRD (0, CL12 given)
    output << "# MODFLOW-USG DISU written by triangle-utils, " << disu.layer_count << " layers of "
           << cell_count << " cells\n"
           << node_count << " " << disu.layer_count << " " << connection_count << " 0 1 0 0 0\n";
    // LAYCBD: no confining beds
    for (int layer = 0; layer < disu.layer_count; layer++) output << (layer == 0 ? "" : " ") << 0;
    output << "\n";
    write_usg_values<int>(output, layer_nodes);
    // TOP, BOT, and AREA, each an array for each layer
    for (const auto * values : {&disu.top, &disu.bottom, &disu.area})
    {
        std::size_t first = 0;
        for (const int count : layer_nodes)
        {
            write_usg_values<double>(output, std::span(*values).subspan(first, count));
            first += count;
        }
    }
    write_usg_values<int>(output, iac);
    write_usg_values<int>(output, disu.nodes, 1);
    write_usg_values<double>(output, disu.distances);
    write_usg_values<double>(output, face_areas);
    // PERLEN NSTP TSMULT Ss/tr
    output << "1 1 1 SS\n";
    if (!output) throw Error("the DISU file could not be written");
}

} // namespace triangle_utils
