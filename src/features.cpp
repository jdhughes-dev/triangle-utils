// Lines and marked segments mapped to the cells of a grid.

#include "features.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "error.hpp"

namespace triangle_utils
{

namespace
{

// the offset from a line, as a fraction of the grid's size, at which the
// cell beside a part along a face is found
constexpr double SIDE_FRACTION = 1e-9;
// breakpoints along a line segment closer than this fraction of its length
// are one, and parts shorter than it are left out
constexpr double SAME_FRACTION = 1e-12;

double cross(double ax, double ay, double bx, double by)
{
    return ax * by - ay * bx;
}

// A box around each cell, for finding the cells a point or segment may meet.
struct Box
{
    double xmin, ymin, xmax, ymax;
};

class CellFinder
{
public:
    explicit CellFinder(const Grid & grid) : m_grid(grid)
    {
        for (const auto & cell : grid.cells)
        {
            Box box{grid.vertices[cell[0]].x, grid.vertices[cell[0]].y, grid.vertices[cell[0]].x,
                    grid.vertices[cell[0]].y};
            for (const int vertex : cell)
            {
                const Point & p = grid.vertices[vertex];
                box = {std::min(box.xmin, p.x), std::min(box.ymin, p.y), std::max(box.xmax, p.x),
                       std::max(box.ymax, p.y)};
            }
            m_boxes.push_back(box);
            m_extent.xmin = std::min(m_extent.xmin, box.xmin);
            m_extent.ymin = std::min(m_extent.ymin, box.ymin);
            m_extent.xmax = std::max(m_extent.xmax, box.xmax);
            m_extent.ymax = std::max(m_extent.ymax, box.ymax);
        }
        m_offset = SIDE_FRACTION *
                   std::hypot(m_extent.xmax - m_extent.xmin, m_extent.ymax - m_extent.ymin);
    }

    double offset() const { return m_offset; }

    // The cell containing a point, or -1; the point must not be on a face.
    int cell_at(const Point & point) const
    {
        for (std::size_t cell = 0; cell < m_boxes.size(); cell++)
        {
            const Box & box = m_boxes[cell];
            if (point.x < box.xmin || point.x > box.xmax || point.y < box.ymin ||
                point.y > box.ymax)
                continue;
            if (contains(m_grid.cells[cell], point)) return static_cast<int>(cell);
        }
        return -1;
    }

    // The parameters along the segment from p to q, between 0 and 1, where it
    // meets a face of a cell, in order.
    std::vector<double> breakpoints(const Point & p, const Point & q) const
    {
        const double dx = q.x - p.x, dy = q.y - p.y;
        const double length2 = dx * dx + dy * dy;
        std::vector<double> at{0.0, 1.0};
        for (std::size_t cell = 0; cell < m_boxes.size(); cell++)
        {
            const Box & box = m_boxes[cell];
            if (std::max(p.x, q.x) < box.xmin || std::min(p.x, q.x) > box.xmax ||
                std::max(p.y, q.y) < box.ymin || std::min(p.y, q.y) > box.ymax)
                continue;
            const auto & corners = m_grid.cells[cell];
            for (std::size_t i = 0; i < corners.size(); i++)
            {
                const Point & a = m_grid.vertices[corners[i]];
                const Point & b = m_grid.vertices[corners[(i + 1) % corners.size()]];
                const double ex = b.x - a.x, ey = b.y - a.y;
                const double denominator = cross(dx, dy, ex, ey);
                const double apx = a.x - p.x, apy = a.y - p.y;
                if (std::abs(denominator) >
                    SAME_FRACTION * std::sqrt(length2 * (ex * ex + ey * ey)))
                {
                    const double t = cross(apx, apy, ex, ey) / denominator;
                    const double u = cross(apx, apy, dx, dy) / denominator;
                    if (u >= 0.0 && u <= 1.0 && t > 0.0 && t < 1.0) at.push_back(t);
                }
                else
                {
                    // a face along the segment: its ends are breakpoints
                    for (const Point & end : {a, b})
                    {
                        const double t = ((end.x - p.x) * dx + (end.y - p.y) * dy) / length2;
                        if (t > 0.0 && t < 1.0) at.push_back(t);
                    }
                }
            }
        }
        std::sort(at.begin(), at.end());
        std::vector<double> unique{at.front()};
        for (const double t : at)
            if (t - unique.back() > SAME_FRACTION) unique.push_back(t);
        if (unique.back() < 1.0) unique.back() = 1.0;
        return unique;
    }

private:
    // Whether a polygon contains a point, by the crossings of a ray to +x.
    bool contains(const std::vector<int> & corners, const Point & point) const
    {
        bool inside = false;
        for (std::size_t i = 0, j = corners.size() - 1; i < corners.size(); j = i++)
        {
            const Point & a = m_grid.vertices[corners[i]];
            const Point & b = m_grid.vertices[corners[j]];
            if ((a.y > point.y) != (b.y > point.y) &&
                point.x < a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y))
                inside = !inside;
        }
        return inside;
    }

    const Grid & m_grid;
    std::vector<Box> m_boxes;
    Box m_extent{HUGE_VAL, HUGE_VAL, -HUGE_VAL, -HUGE_VAL};
    double m_offset = 0.0;
};

// Add the parts of the segment from p to q, which starts at distance along
// the line, to parts, joining a part to the one before in the same cell.
void add_parts(const CellFinder & finder, const Point & p, const Point & q, double distance,
               std::vector<CellLength> & parts)
{
    const double length = std::hypot(q.x - p.x, q.y - p.y);
    if (length == 0.0) return;
    // the unit normal to the segment's left
    const double nx = -(q.y - p.y) / length, ny = (q.x - p.x) / length;
    const auto at = finder.breakpoints(p, q);
    for (std::size_t i = 0; i + 1 < at.size(); i++)
    {
        const double middle = 0.5 * (at[i] + at[i + 1]);
        const Point m{p.x + middle * (q.x - p.x), p.y + middle * (q.y - p.y)};
        // the cell on the left, or on the right where none is on the left
        int cell = finder.cell_at({m.x + finder.offset() * nx, m.y + finder.offset() * ny});
        if (cell < 0)
            cell = finder.cell_at({m.x - finder.offset() * nx, m.y - finder.offset() * ny});
        if (cell < 0) continue;
        const double start = distance + at[i] * length;
        const double part = (at[i + 1] - at[i]) * length;
        if (!parts.empty() && parts.back().cell == cell &&
            std::abs(parts.back().start + parts.back().length - start) <= SAME_FRACTION * length)
            parts.back().length += part;
        else
            parts.push_back({cell, start, part});
    }
}

} // namespace

std::vector<CellLength> cells_along_line(const Grid & grid, const std::vector<Point> & line)
{
    if (line.size() < 2)
        throw Error(message("the line has ", line.size(), " points, but it needs at least 2"));
    for (std::size_t point = 0; point < line.size(); point++)
        if (!std::isfinite(line[point].x) || !std::isfinite(line[point].y))
            throw Error(message("point ", point, " of the line is not finite"));
    const CellFinder finder(grid);
    std::vector<CellLength> parts;
    double distance = 0.0;
    for (std::size_t i = 0; i + 1 < line.size(); i++)
    {
        add_parts(finder, line[i], line[i + 1], distance, parts);
        distance += std::hypot(line[i + 1].x - line[i].x, line[i + 1].y - line[i].y);
    }
    return parts;
}

std::vector<CellLength> cells_on_segments(const Grid & grid, const Mesh & mesh, int marker)
{
    check_sizes(mesh);
    const CellFinder finder(grid);
    std::vector<CellLength> parts;
    double distance = 0.0;
    bool found = false;
    for (std::size_t segment = 0; segment < mesh.segments.size(); segment++)
    {
        if (mesh.segment_markers.empty() || mesh.segment_markers[segment] != marker) continue;
        found = true;
        for (const int end : mesh.segments[segment])
            if (end < 0 || static_cast<std::size_t>(end) >= mesh.vertices.size())
                throw Error(message("segment ", segment, " refers to vertex ", end,
                                    ", but the mesh has ", mesh.vertices.size(), " vertices"));
        const Point & p = mesh.vertices[mesh.segments[segment][0]];
        const Point & q = mesh.vertices[mesh.segments[segment][1]];
        add_parts(finder, p, q, distance, parts);
        distance += std::hypot(q.x - p.x, q.y - p.y);
    }
    if (!found) throw Error(message("no segment of the mesh has marker ", marker));
    // each cell once, with its parts' lengths summed
    std::vector<CellLength> cells;
    std::map<int, std::size_t> index;
    for (const CellLength & part : parts)
    {
        const auto [entry, added] = index.try_emplace(part.cell, cells.size());
        if (added)
            cells.push_back(part);
        else
            cells[entry->second].length += part.length;
    }
    return cells;
}

} // namespace triangle_utils
