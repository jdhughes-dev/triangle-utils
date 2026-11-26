// Layers of a grid and their MODFLOW DISV and DISU data.

#include "layers.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "error.hpp"
#include "features.hpp"

namespace triangle_utils
{

namespace
{

// The area of a cell.
double cell_area(const Grid & grid, std::size_t cell)
{
    const auto & corners = grid.cells[cell];
    // relative to the first corner, so the area of a small cell far from the
    // origin keeps its precision
    const Point & origin = grid.vertices[corners[0]];
    double sum = 0.0;
    for (std::size_t i = 1; i + 1 < corners.size(); i++)
    {
        const Point & a = grid.vertices[corners[i]];
        const Point & b = grid.vertices[corners[i + 1]];
        sum += (a.x - origin.x) * (b.y - origin.y) - (b.x - origin.x) * (a.y - origin.y);
    }
    return 0.5 * std::abs(sum);
}

void check_finite(const std::vector<double> & values, const char * name)
{
    for (std::size_t i = 0; i < values.size(); i++)
        if (!std::isfinite(values[i]))
            throw Error(message("the ", name, " value ", i, " is not finite"));
}

} // namespace

Layers layer_grid(const Grid & grid, int layer_count, const std::vector<double> & top,
                  const std::vector<double> & bottoms, const std::vector<int> & idomain,
                  double minimum_thickness)
{
    check_grid(grid);
    const std::size_t cell_count = grid.cells.size();
    if (layer_count < 1) throw Error(message("the layer count is ", layer_count, ", not positive"));
    if (cell_count > static_cast<std::size_t>(std::numeric_limits<int>::max() / layer_count))
        throw Error(message(layer_count, " layers of ", cell_count, " cells are too many"));
    const std::size_t value_count = static_cast<std::size_t>(layer_count) * cell_count;
    if (top.size() != cell_count)
        throw Error(message("the grid has ", cell_count, " cells but ", top.size(), " top values"));
    if (bottoms.size() != value_count)
        throw Error(message(layer_count, " layers of ", cell_count, " cells need ", value_count,
                            " bottom values, not ", bottoms.size()));
    if (!idomain.empty() && idomain.size() != value_count)
        throw Error(message(layer_count, " layers of ", cell_count, " cells need ", value_count,
                            " idomain values, not ", idomain.size()));
    check_finite(top, "top");
    check_finite(bottoms, "bottom");
    if (!std::isfinite(minimum_thickness) || minimum_thickness < 0.0)
        throw Error(message("the minimum thickness ", minimum_thickness,
                            " is not a finite number of at least 0"));

    Layers layers;
    layers.layer_count = layer_count;
    layers.cell_count = static_cast<int>(cell_count);
    layers.top = top;
    layers.bottoms = bottoms;
    layers.idomain = idomain.empty() ? std::vector<int>(value_count, 1) : idomain;
    for (int layer = 0; layer < layer_count; layer++)
        for (std::size_t cell = 0; cell < cell_count; cell++)
        {
            const std::size_t i = layer * cell_count + cell;
            if (layers.idomain[i] <= 0) continue;
            const double above = layer == 0 ? top[cell] : bottoms[i - cell_count];
            const double thickness = above - bottoms[i];
            if (minimum_thickness > 0.0 && thickness < minimum_thickness)
                layers.idomain[i] = -1;
            else if (thickness <= 0.0)
                throw Error(message("cell ", cell, " in layer ", layer, " has a top of ", above,
                                    " and a bottom of ", bottoms[i],
                                    ", so it is not thicker than 0"));
        }
    return layers;
}

Disu layer_disu(const Grid & grid, const Connections & connections, const Layers & layers)
{
    const std::size_t cell_count = grid.cells.size();
    if (static_cast<std::size_t>(layers.cell_count) != cell_count ||
        connections.offsets.size() != cell_count + 1)
        throw Error(message("the layers have ", layers.cell_count, " cells and the connections ",
                            connections.offsets.size() - 1, ", but the grid has ", cell_count));
    const int layer_count = layers.layer_count;
    const auto & idomain = layers.idomain;

    // the node of each cell in the layers, or -1
    std::vector<int> node_of(idomain.size(), -1);
    Disu disu;
    for (std::size_t i = 0; i < idomain.size(); i++)
        if (idomain[i] > 0)
        {
            node_of[i] = static_cast<int>(disu.cells.size());
            disu.cells.push_back(static_cast<int>(i));
        }
    std::vector<double> areas(cell_count);
    for (std::size_t cell = 0; cell < cell_count; cell++) areas[cell] = cell_area(grid, cell);

    // the active cell reached from layer cell i going by step layers, through
    // pass-through cells, or -1
    auto vertical = [&](std::size_t i, int step) -> int
    {
        const int cell = static_cast<int>(i % cell_count);
        for (int layer = static_cast<int>(i / cell_count) + step; layer >= 0 && layer < layer_count;
             layer += step)
        {
            const std::size_t j = layer * cell_count + cell;
            if (idomain[j] > 0) return node_of[j];
            if (idomain[j] == 0) return -1;
        }
        return -1;
    };

    disu.offsets.push_back(0);
    for (std::size_t node = 0; node < disu.cells.size(); node++)
    {
        const std::size_t i = disu.cells[node];
        const std::size_t cell = i % cell_count;
        const std::size_t layer = i / cell_count;
        const double top = layer == 0 ? layers.top[cell] : layers.bottoms[i - cell_count];
        const double half_thickness = 0.5 * (top - layers.bottoms[i]);
        disu.top.push_back(top);
        disu.bottom.push_back(layers.bottoms[i]);
        disu.area.push_back(areas[cell]);

        auto add = [&](int other, int direction, double distance, double width, double angle)
        {
            disu.nodes.push_back(other);
            disu.directions.push_back(direction);
            disu.distances.push_back(distance);
            disu.widths.push_back(width);
            disu.angles.push_back(angle);
        };
        add(static_cast<int>(node), 0, 0.0, 0.0, 0.0);
        // above, beside, and below, in increasing order of node
        const int up = vertical(i, -1);
        if (up >= 0) add(up, 0, half_thickness, areas[cell], 0.0);
        for (int c = connections.offsets[cell]; c < connections.offsets[cell + 1]; c++)
        {
            const int other = node_of[layer * cell_count + connections.cells[c]];
            if (other >= 0)
                add(other, 1, connections.distances[c], connections.widths[c],
                    connections.angles[c]);
        }
        const int down = vertical(i, 1);
        if (down >= 0) add(down, 0, half_thickness, areas[cell], 0.0);
        if (disu.nodes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw Error("the layers have more connections than an int holds");
        disu.offsets.push_back(static_cast<int>(disu.nodes.size()));
    }
    return disu;
}

std::vector<double> values_at_centers(const Grid & grid, const Mesh & mesh,
                                      const std::vector<double> & vertex_values)
{
    if (vertex_values.size() != mesh.vertices.size())
        throw Error(message("the mesh has ", mesh.vertices.size(), " vertices but ",
                            vertex_values.size(), " values"));
    check_finite(vertex_values, "vertex");
    const Grid triangles = triangle_grid(mesh, TriangleCenter::CENTROID);
    const auto found = cells_at_points(triangles, grid.centers);
    std::vector<double> values(grid.centers.size());
    for (std::size_t cell = 0; cell < grid.centers.size(); cell++)
    {
        if (found[cell] < 0)
            throw Error(message("the center of cell ", cell, " is outside the mesh"));
        const auto & t = mesh.triangles[triangles.sources[found[cell]]];
        const Point & p = grid.centers[cell];
        const Point & a = mesh.vertices[t[0]];
        const Point & b = mesh.vertices[t[1]];
        const Point & c = mesh.vertices[t[2]];
        bool on_vertex = false;
        for (const int vertex : t)
            if (mesh.vertices[vertex].x == p.x && mesh.vertices[vertex].y == p.y)
            {
                values[cell] = vertex_values[vertex];
                on_vertex = true;
            }
        if (on_vertex) continue;
        // barycentric weights, from areas relative to a
        const double area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        const double wb = ((p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y)) / area;
        const double wc = ((b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y)) / area;
        values[cell] = (1.0 - wb - wc) * vertex_values[t[0]] + wb * vertex_values[t[1]] +
                       wc * vertex_values[t[2]];
    }
    return values;
}

} // namespace triangle_utils
