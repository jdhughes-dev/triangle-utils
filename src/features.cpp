// Lines and marked segments mapped to the cells of a grid.

#include "features.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

#include "error.hpp"

namespace triangle_utils
{

namespace
{

// the offset from a line, as a fraction of the part's length, at which the
// cell beside a part along a face is found, and its least, as a number of
// rounding steps of the coordinates
constexpr double SIDE_FRACTION = 1e-6;
constexpr double SIDE_STEPS = 64.0;
// how much smaller each next offset is, where neither side is in a cell
constexpr double SIDE_SHRINK = 1e-3;
// breakpoints along a line segment closer than this fraction of its length
// are one
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

// Finds the cells a point or segment meets through a uniform grid of bins,
// each listing the cells whose boxes overlap it.
class CellFinder
{
public:
    explicit CellFinder(const Grid & grid) : m_grid(grid), m_seen(grid.cells.size(), 0)
    {
        for (const auto & cell : grid.cells)
        {
            const Point & first = grid.vertices[cell[0]];
            Box box{first.x, first.y, first.x, first.y};
            for (const int vertex : cell)
            {
                const Point & p = grid.vertices[vertex];
                box = {std::min(box.xmin, p.x), std::min(box.ymin, p.y), std::max(box.xmax, p.x),
                       std::max(box.ymax, p.y)};
            }
            m_boxes.push_back(box);
            m_extent = {std::min(m_extent.xmin, box.xmin), std::min(m_extent.ymin, box.ymin),
                        std::max(m_extent.xmax, box.xmax), std::max(m_extent.ymax, box.ymax)};
        }
        if (m_boxes.empty()) return;
        // about one cell per bin
        const double width = std::max(m_extent.xmax - m_extent.xmin, 1e-300);
        const double height = std::max(m_extent.ymax - m_extent.ymin, 1e-300);
        const double side = std::sqrt(width * height / static_cast<double>(m_boxes.size()));
        // clamped before the cast, which a very thin grid would overflow
        m_columns = static_cast<int>(std::clamp(width / side, 0.0, 4095.0)) + 1;
        m_rows = static_cast<int>(std::clamp(height / side, 0.0, 4095.0)) + 1;
        m_bins.resize(static_cast<std::size_t>(m_columns) * m_rows);
        for (std::size_t cell = 0; cell < m_boxes.size(); cell++)
        {
            const Box & box = m_boxes[cell];
            for (int row = row_of(box.ymin); row <= row_of(box.ymax); row++)
                for (int column = column_of(box.xmin); column <= column_of(box.xmax); column++)
                    m_bins[static_cast<std::size_t>(row) * m_columns + column].push_back(
                        static_cast<int>(cell));
        }
    }

    // The cell containing a point, or -1; the point must not be on a face.
    int cell_at(const Point & point) const
    {
        if (m_boxes.empty() || point.x < m_extent.xmin || point.x > m_extent.xmax ||
            point.y < m_extent.ymin || point.y > m_extent.ymax)
            return -1;
        for (const int cell :
             m_bins[static_cast<std::size_t>(row_of(point.y)) * m_columns + column_of(point.x)])
        {
            const Box & box = m_boxes[cell];
            if (point.x < box.xmin || point.x > box.xmax || point.y < box.ymin ||
                point.y > box.ymax)
                continue;
            if (contains(m_grid.cells[cell], point)) return cell;
        }
        return -1;
    }

    // The lowest-numbered cell containing a point or with it on a face, or -1.
    int lowest_cell_at(const Point & point) const
    {
        if (m_boxes.empty() || point.x < m_extent.xmin || point.x > m_extent.xmax ||
            point.y < m_extent.ymin || point.y > m_extent.ymax)
            return -1;
        int lowest = -1;
        for (const int cell :
             m_bins[static_cast<std::size_t>(row_of(point.y)) * m_columns + column_of(point.x)])
        {
            if (lowest >= 0 && cell > lowest) continue;
            const Box & box = m_boxes[cell];
            if (point.x < box.xmin || point.x > box.xmax || point.y < box.ymin ||
                point.y > box.ymax)
                continue;
            if (contains(m_grid.cells[cell], point) || on_face(m_grid.cells[cell], point))
                lowest = cell;
        }
        return lowest;
    }

    // The parameters along the segment from p to q, between 0 and 1, where it
    // meets a face of a cell, in order.
    std::vector<double> breakpoints(const Point & p, const Point & q)
    {
        const double dx = q.x - p.x, dy = q.y - p.y;
        const double length2 = dx * dx + dy * dy;
        std::vector<double> at{0.0, 1.0};
        m_stamp++;
        for (const int cell : candidates(p, q))
        {
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
    int column_of(double x) const
    {
        const double f = (x - m_extent.xmin) / (m_extent.xmax - m_extent.xmin);
        return std::clamp(static_cast<int>(f * m_columns), 0, m_columns - 1);
    }

    int row_of(double y) const
    {
        const double f = (y - m_extent.ymin) / (m_extent.ymax - m_extent.ymin);
        return std::clamp(static_cast<int>(f * m_rows), 0, m_rows - 1);
    }

    // The cells, each once, whose boxes overlap the bins the segment from p
    // to q passes through.
    std::vector<int> candidates(const Point & p, const Point & q)
    {
        std::vector<int> cells;
        if (m_boxes.empty()) return cells;
        const double xmin = std::min(p.x, q.x), xmax = std::max(p.x, q.x);
        const double ymin = std::min(p.y, q.y), ymax = std::max(p.y, q.y);
        if (xmax < m_extent.xmin || xmin > m_extent.xmax || ymax < m_extent.ymin ||
            ymin > m_extent.ymax)
            return cells;
        // the bins the segment's box covers; along a column of bins, only
        // those between the segment's heights at the column's sides
        const double bin_width = (m_extent.xmax - m_extent.xmin) / m_columns;
        // a margin for rounding at the bins' edges
        const double margin = 1e-9 * (m_extent.ymax - m_extent.ymin);
        for (int column = column_of(xmin); column <= column_of(xmax); column++)
        {
            double low = ymin, high = ymax;
            if (q.x != p.x)
            {
                const double column_left = m_extent.xmin + column * bin_width;
                const double left = std::max(xmin, column_left);
                const double right = std::min(xmax, column_left + bin_width);
                const double y_left = p.y + (left - p.x) * (q.y - p.y) / (q.x - p.x);
                const double y_right = p.y + (right - p.x) * (q.y - p.y) / (q.x - p.x);
                low = std::max(ymin, std::min(y_left, y_right) - margin);
                high = std::min(ymax, std::max(y_left, y_right) + margin);
            }
            for (int row = row_of(low); row <= row_of(high); row++)
                for (const int cell : m_bins[static_cast<std::size_t>(row) * m_columns + column])
                {
                    if (m_seen[cell] == m_stamp) continue;
                    m_seen[cell] = m_stamp;
                    const Box & box = m_boxes[cell];
                    if (xmax < box.xmin || xmin > box.xmax || ymax < box.ymin || ymin > box.ymax)
                        continue;
                    cells.push_back(cell);
                }
        }
        return cells;
    }

    // Whether a point is on a face of a polygon, within rounding.
    bool on_face(const std::vector<int> & corners, const Point & point) const
    {
        for (std::size_t i = 0; i < corners.size(); i++)
        {
            const Point & a = m_grid.vertices[corners[i]];
            const Point & b = m_grid.vertices[corners[(i + 1) % corners.size()]];
            const double ex = b.x - a.x, ey = b.y - a.y;
            const double px = point.x - a.x, py = point.y - a.y;
            const double length = std::hypot(ex, ey);
            if (std::abs(cross(ex, ey, px, py)) <= SAME_FRACTION * length * length &&
                px * ex + py * ey >= 0.0 && px * ex + py * ey <= length * length)
                return true;
        }
        return false;
    }

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
    int m_columns = 1, m_rows = 1;
    std::vector<std::vector<int>> m_bins;
    // the last search each cell was found in, so it is listed once
    std::vector<unsigned> m_seen;
    unsigned m_stamp = 0;
};

// Add the parts of the segment from p to q, which starts at distance along
// the line, to parts. A part joins the one before if it is in the same cell
// and none of the line between them is outside the grid, which outside
// records.
void add_parts(CellFinder & finder, const Point & p, const Point & q, double distance,
               std::vector<CellLength> & parts, bool & outside)
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
        const double part = (at[i + 1] - at[i]) * length;
        // the offset is a small fraction of the part, but more than rounding
        // of the coordinates; smaller in turn, for a cell thinner than it
        const double least = SIDE_STEPS * std::numeric_limits<double>::epsilon() *
                             std::max(std::abs(m.x), std::abs(m.y));
        int cell = -1;
        for (double offset = std::max(SIDE_FRACTION * part, least); cell < 0;
             offset = std::max(offset * SIDE_SHRINK, least))
        {
            // the cell on the left, or on the right where none is on the left
            cell = finder.cell_at({m.x + offset * nx, m.y + offset * ny});
            if (cell < 0) cell = finder.cell_at({m.x - offset * nx, m.y - offset * ny});
            if (offset == least) break;
        }
        if (cell < 0)
        {
            outside = true;
            continue;
        }
        if (!parts.empty() && parts.back().cell == cell && !outside)
            parts.back().length += part;
        else
            parts.push_back({cell, distance + at[i] * length, part});
        outside = false;
    }
}

} // namespace

std::vector<CellLength> cells_along_line(const Grid & grid, const std::vector<Point> & line)
{
    check_grid(grid);
    if (line.size() < 2)
        throw Error(message("the line has ", line.size(), " points, but it needs at least 2"));
    for (std::size_t point = 0; point < line.size(); point++)
        if (!std::isfinite(line[point].x) || !std::isfinite(line[point].y))
            throw Error(message("point ", point, " of the line is not finite"));
    CellFinder finder(grid);
    std::vector<CellLength> parts;
    double distance = 0.0;
    bool outside = true;
    for (std::size_t i = 0; i + 1 < line.size(); i++)
    {
        add_parts(finder, line[i], line[i + 1], distance, parts, outside);
        distance += std::hypot(line[i + 1].x - line[i].x, line[i + 1].y - line[i].y);
    }
    return parts;
}

namespace
{

// The segments with a marker, joined end to end into chains of vertices, in
// the order of the mesh's vertex numbers: each chain starts at its
// lowest-numbered vertex, a closed chain runs toward its second-lowest, and
// the chains follow their lowest vertices. A mesh triangle-utils generates
// numbers its input vertices first, in the order given, so this is the order
// the features were digitized.
std::vector<std::vector<int>> chains_of(const Mesh & mesh, int marker)
{
    std::vector<std::array<int, 2>> segments;
    for (std::size_t segment = 0; segment < mesh.segments.size(); segment++)
    {
        if (mesh.segment_markers.empty() || mesh.segment_markers[segment] != marker) continue;
        for (const int end : mesh.segments[segment])
            if (end < 0 || static_cast<std::size_t>(end) >= mesh.vertices.size())
                throw Error(message("segment ", segment, " refers to vertex ", end,
                                    ", but the mesh has ", mesh.vertices.size(), " vertices"));
        segments.push_back(mesh.segments[segment]);
    }
    if (segments.empty()) throw Error(message("no segment of the mesh has marker ", marker));
    std::map<int, std::vector<int>> at;
    for (std::size_t segment = 0; segment < segments.size(); segment++)
        for (const int end : segments[segment]) at[end].push_back(static_cast<int>(segment));
    std::vector<bool> used(segments.size(), false);
    auto unused_at = [&](int vertex)
    {
        std::vector<int> found;
        for (const int segment : at[vertex])
            if (!used[segment]) found.push_back(segment);
        return found;
    };
    auto other_end = [&](int segment, int vertex)
    { return segments[segment][0] == vertex ? segments[segment][1] : segments[segment][0]; };
    // the vertices from a vertex along a segment, through vertices where the
    // chain does not branch, to an end, a branch, or the start again
    auto walk = [&](int start, int first)
    {
        std::vector<int> vertices{start};
        std::vector<int> taken{first};
        int segment = first, vertex = start;
        while (true)
        {
            vertex = other_end(segment, vertex);
            vertices.push_back(vertex);
            if (vertex == start) break;
            std::vector<int> next;
            for (const int candidate : unused_at(vertex))
                if (std::find(taken.begin(), taken.end(), candidate) == taken.end())
                    next.push_back(candidate);
            if (next.size() != 1 || at[vertex].size() != 2) break;
            segment = next[0];
            taken.push_back(segment);
        }
        return std::pair(vertices, taken);
    };

    std::vector<std::vector<int>> chains;
    while (std::find(used.begin(), used.end(), false) != used.end())
    {
        // an end or a branch, if any, otherwise the lowest vertex of a loop
        int start = -1;
        for (const auto & [vertex, segments_at] : at)
        {
            const auto free = unused_at(vertex);
            if (free.empty()) continue;
            if (start < 0) start = vertex;
            if (at[vertex].size() != 2)
            {
                start = vertex;
                break;
            }
        }
        auto choices = unused_at(start);
        std::pair<std::vector<int>, std::vector<int>> chosen;
        if (choices.size() == 2 && at[start].size() == 2)
        {
            // a loop: toward its second-lowest vertex, the nearer way
            auto one = walk(start, choices[0]);
            auto other = walk(start, choices[1]);
            // the lowest vertex but the start, which the loop ends at too
            int second = -1;
            for (const int vertex : one.first)
                if (vertex != start && (second < 0 || vertex < second)) second = vertex;
            const auto steps = [&](const std::vector<int> & vertices)
            { return std::find(vertices.begin(), vertices.end(), second) - vertices.begin(); };
            chosen = steps(one.first) <= steps(other.first) ? one : other;
        }
        else
        {
            // from an end or a branch, toward the lowest neighbor
            const int first =
                *std::min_element(choices.begin(), choices.end(), [&](int a, int b)
                                  { return other_end(a, start) < other_end(b, start); });
            chosen = walk(start, first);
        }
        for (const int segment : chosen.second) used[segment] = true;
        chains.push_back(chosen.first);
    }
    // ends and branches were taken first; the chains follow their lowest
    // vertices
    std::stable_sort(
        chains.begin(), chains.end(), [](const std::vector<int> & a, const std::vector<int> & b)
        { return *std::min_element(a.begin(), a.end()) < *std::min_element(b.begin(), b.end()); });
    return chains;
}

} // namespace

std::vector<CellLength> cells_on_segments(const Grid & grid, const Mesh & mesh, int marker)
{
    check_grid(grid);
    check_sizes(mesh);
    const auto chains = chains_of(mesh, marker);
    CellFinder finder(grid);
    std::vector<CellLength> parts;
    double distance = 0.0;
    for (const auto & chain : chains)
    {
        bool outside = true;
        for (std::size_t i = 0; i + 1 < chain.size(); i++)
        {
            const Point & p = mesh.vertices[chain[i]];
            const Point & q = mesh.vertices[chain[i + 1]];
            add_parts(finder, p, q, distance, parts, outside);
            distance += std::hypot(q.x - p.x, q.y - p.y);
        }
    }
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

std::vector<int> cells_at_points(const Grid & grid, const std::vector<Point> & points)
{
    check_grid(grid);
    for (std::size_t point = 0; point < points.size(); point++)
        if (!std::isfinite(points[point].x) || !std::isfinite(points[point].y))
            throw Error(message("point ", point, " is not finite"));
    const CellFinder finder(grid);
    std::vector<int> cells;
    for (const Point & point : points) cells.push_back(finder.lowest_cell_at(point));
    return cells;
}

} // namespace triangle_utils
