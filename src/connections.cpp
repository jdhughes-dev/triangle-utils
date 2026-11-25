// The faces cells of a grid share, for MODFLOW DISU.

#include "connections.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <tuple>
#include <vector>

#include "error.hpp"

namespace triangle_utils
{

namespace
{

// the smallest distance from a center to a face, as a fraction of the face's
// width; a right triangle's circumcenter, on its longest side, is within it
constexpr double DISTANCE_FRACTION = 1e-9;

// One side of a cell, between two of its corners.
struct Edge
{
    // the corners, lower vertex first
    int low = 0;
    int high = 0;
    int cell = 0;
    // the corner the side starts from, going around the cell
    int from = 0;
};

// A part of a face, seen from one of its cells.
struct Part
{
    int cell = 0;
    int other = 0;
    double width = 0.0;
    // the outward normal and the midpoint, each times the width
    double normal_x = 0.0;
    double normal_y = 0.0;
    double middle_x = 0.0;
    double middle_y = 0.0;
};

// +1 for a counterclockwise cell, -1 for a clockwise one.
double orientation(const Grid & grid, std::size_t cell)
{
    const auto & corners = grid.cells[cell];
    double sum = 0.0;
    for (std::size_t i = 0; i < corners.size(); i++)
    {
        const Point & a = grid.vertices[corners[i]];
        const Point & b = grid.vertices[corners[(i + 1) % corners.size()]];
        sum += a.x * b.y - b.x * a.y;
    }
    return sum > 0.0 ? 1.0 : -1.0;
}

// The part of a face on a cell's side from one corner to the next.
Part part_of(const Grid & grid, const Edge & edge, int other, double sign)
{
    const auto & corners = grid.cells[edge.cell];
    const Point & a = grid.vertices[corners[edge.from]];
    const Point & b = grid.vertices[corners[(edge.from + 1) % corners.size()]];
    const double dx = b.x - a.x, dy = b.y - a.y;
    Part part;
    part.cell = edge.cell;
    part.other = other;
    part.width = std::hypot(dx, dy);
    // the outside is on the right going counterclockwise
    part.normal_x = sign * dy;
    part.normal_y = -sign * dx;
    part.middle_x = 0.5 * (a.x + b.x) * part.width;
    part.middle_y = 0.5 * (a.y + b.y) * part.width;
    return part;
}

} // namespace

Connections connect_cells(const Grid & grid)
{
    check_grid(grid);
    std::vector<Edge> edges;
    for (std::size_t cell = 0; cell < grid.cells.size(); cell++)
    {
        const auto & corners = grid.cells[cell];
        for (std::size_t i = 0; i < corners.size(); i++)
        {
            const int a = corners[i], b = corners[(i + 1) % corners.size()];
            if (a == b) continue;
            edges.push_back(
                {std::min(a, b), std::max(a, b), static_cast<int>(cell), static_cast<int>(i)});
        }
    }
    std::sort(edges.begin(), edges.end(), [](const Edge & e, const Edge & f)
              { return std::tie(e.low, e.high, e.cell) < std::tie(f.low, f.high, f.cell); });

    std::vector<double> signs(grid.cells.size());
    for (std::size_t cell = 0; cell < grid.cells.size(); cell++)
        signs[cell] = orientation(grid, cell);
    std::vector<Part> parts;
    for (std::size_t first = 0; first < edges.size();)
    {
        std::size_t last = first + 1;
        while (last < edges.size() && edges[last].low == edges[first].low &&
               edges[last].high == edges[first].high)
            last++;
        const Edge & e = edges[first];
        if (last - first > 2)
            throw Error(message("the edge from vertex ", e.low, " to vertex ", e.high,
                                " is a side of more than two cells"));
        if (last - first == 2)
        {
            const Edge & f = edges[first + 1];
            if (e.cell == f.cell)
                throw Error(message("the edge from vertex ", e.low, " to vertex ", e.high,
                                    " is a side of cell ", e.cell, " twice"));
            const Part p = part_of(grid, e, f.cell, signs[e.cell]);
            const Part q = part_of(grid, f, e.cell, signs[f.cell]);
            if (p.normal_x * q.normal_x + p.normal_y * q.normal_y > 0.0)
                throw Error(message("cells ", e.cell, " and ", f.cell,
                                    " overlap along the edge from vertex ", e.low, " to vertex ",
                                    e.high));
            if (p.width > 0.0)
            {
                parts.push_back(p);
                parts.push_back(q);
            }
        }
        first = last;
    }
    // the parts of a face, as where two cells share more than one edge, are
    // summed
    std::sort(parts.begin(), parts.end(), [](const Part & p, const Part & q)
              { return std::tie(p.cell, p.other) < std::tie(q.cell, q.other); });

    Connections connections;
    connections.offsets.assign(grid.cells.size() + 1, 0);
    for (std::size_t first = 0; first < parts.size();)
    {
        Part face = parts[first];
        std::size_t last = first + 1;
        for (; last < parts.size() && parts[last].cell == face.cell &&
               parts[last].other == face.other;
             last++)
        {
            face.width += parts[last].width;
            face.normal_x += parts[last].normal_x;
            face.normal_y += parts[last].normal_y;
            face.middle_x += parts[last].middle_x;
            face.middle_y += parts[last].middle_y;
        }
        first = last;
        const double length = std::hypot(face.normal_x, face.normal_y);
        if (!(length > 0.0))
            throw Error(message("the faces cell ", face.cell, " shares with cell ", face.other,
                                " point opposite ways, so they have no one direction"));
        const double nx = face.normal_x / length, ny = face.normal_y / length;
        const Point & center = grid.centers[face.cell];
        const double distance = (face.middle_x / face.width - center.x) * nx +
                                (face.middle_y / face.width - center.y) * ny;
        if (!(distance > DISTANCE_FRACTION * face.width))
            throw Error(
                message("the center of cell ", face.cell,
                        " is not on its side of the face it shares "
                        "with cell ",
                        face.other, ", so the distance from the center to the face is ", distance,
                        "; for a triangle grid, center the cells on their centroids, or use a "
                        "mesh with no angle of 90 degrees or more"));
        double angle = std::atan2(ny, nx) * 180.0 / std::numbers::pi;
        if (angle < 0.0) angle += 360.0;
        // a direction just below the x axis rounds to 360
        if (angle >= 360.0) angle -= 360.0;
        connections.offsets[face.cell + 1]++;
        connections.cells.push_back(face.other);
        connections.distances.push_back(distance);
        connections.widths.push_back(face.width);
        // + 0.0 turns -0.0 into 0.0
        connections.angles.push_back(angle + 0.0);
    }
    for (std::size_t cell = 0; cell < grid.cells.size(); cell++)
        connections.offsets[cell + 1] += connections.offsets[cell];
    return connections;
}

} // namespace triangle_utils
