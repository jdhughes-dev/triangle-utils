// Geometry to mesh: polygons, holes, regions, points, lines, and rings, and
// the triangular mesh Triangle builds from them.

#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <locale>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include "error.hpp"
#include "run_triangle.hpp"

namespace triangle_utils
{

namespace
{

// the most vertices or segments the input can have; Triangle indexes them
// with int, with room for the triangles it adds
constexpr double MAX_INPUT_COUNT = std::numeric_limits<int>::max() / 3;

// the largest minimum angle; Triangle may not finish above it
constexpr double MAX_MINIMUM_ANGLE = 34.0;

void check_finite(const Point & point, const char * what, std::size_t item)
{
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
        throw Error(message(what, " ", item, " has a coordinate that is not finite"));
}

void check_chain(const Chain & chain, std::size_t minimum, const char * what, std::size_t item)
{
    if (chain.points.size() < minimum)
        throw Error(message(what, " ", item, " has ", chain.points.size(), " points, but at least ",
                            minimum, " are needed"));
    if (!std::isfinite(chain.spacing))
        throw Error(message(what, " ", item, " has a spacing that is not finite"));
    for (const Point & point : chain.points) check_finite(point, what, item);
}

// Triangle's input: vertices with near or identical points merged, and
// segments without repeats.
class Builder
{
public:
    explicit Builder(double merge_distance) : m_merge_distance(merge_distance) {}

    int add_vertex(const Point & point)
    {
        if (m_merge_distance == 0.0)
        {
            const auto [found, added] = m_exact.try_emplace({point.x, point.y}, vertex_count());
            if (added) m_vertices.push_back(point);
            return found->second;
        }
        const std::int64_t cell_x = cell(point.x);
        const std::int64_t cell_y = cell(point.y);
        for (std::int64_t dx = -1; dx <= 1; dx++)
        {
            for (std::int64_t dy = -1; dy <= 1; dy++)
            {
                const auto found = m_cells.find({cell_x + dx, cell_y + dy});
                if (found == m_cells.end()) continue;
                for (const int vertex : found->second)
                {
                    const Point & other = m_vertices[vertex];
                    if (std::hypot(other.x - point.x, other.y - point.y) < m_merge_distance)
                        return vertex;
                }
            }
        }
        const int vertex = vertex_count();
        m_vertices.push_back(point);
        m_cells[{cell_x, cell_y}].push_back(vertex);
        return vertex;
    }

    void add_segment(int first, int second, int marker)
    {
        // a segment whose ends were merged, or a repeat, adds nothing
        if (first == second) return;
        if (!m_segment_keys.insert(std::minmax(first, second)).second) return;
        if (m_segments.size() >= MAX_INPUT_COUNT)
            throw Error("the geometry has too many segments for Triangle");
        m_segments.push_back({first, second});
        m_markers.push_back(marker);
    }

    void add_chain(const std::vector<Point> & points, bool closed, int marker)
    {
        std::vector<int> indices;
        for (const Point & point : points) indices.push_back(add_vertex(point));
        for (std::size_t index = 0; index + 1 < indices.size(); index++)
        {
            add_segment(indices[index], indices[index + 1], marker);
        }
        if (closed) add_segment(indices.back(), indices.front(), marker);
    }

    std::vector<Point> & vertices() { return m_vertices; }
    std::vector<std::array<int, 2>> & segments() { return m_segments; }
    std::vector<int> & markers() { return m_markers; }

private:
    int vertex_count() const
    {
        if (m_vertices.size() >= MAX_INPUT_COUNT)
            throw Error("the geometry has too many points for Triangle");
        return static_cast<int>(m_vertices.size());
    }

    std::int64_t cell(double coordinate) const
    {
        const double index = std::floor(coordinate / m_merge_distance);
        // beyond this, cells are not distinct integers
        constexpr double LARGEST_CELL = 1e15;
        if (std::abs(index) > LARGEST_CELL)
            throw Error(message("the merge distance ", m_merge_distance,
                                " is too small for a coordinate of ", coordinate));
        return static_cast<std::int64_t>(index);
    }

    double m_merge_distance;
    std::vector<Point> m_vertices;
    std::map<std::pair<double, double>, int> m_exact;
    std::map<std::pair<std::int64_t, std::int64_t>, std::vector<int>> m_cells;
    std::vector<std::array<int, 2>> m_segments;
    std::vector<int> m_markers;
    std::set<std::pair<int, int>> m_segment_keys;
};

// A number for Triangle's switches, written so it reads back exactly.
std::string switch_number(double value)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text.precision(std::numeric_limits<double>::max_digits10);
    text << value;
    return text.str();
}

} // namespace

std::vector<Point> densify(const Chain & chain, bool closed)
{
    std::vector<Point> result;
    const std::size_t count = chain.points.size();
    const std::size_t edge_count = closed ? count : (count > 0 ? count - 1 : 0);
    double total = static_cast<double>(count);
    for (std::size_t edge = 0; edge < edge_count; edge++)
    {
        const Point & start = chain.points[edge];
        const Point & end = chain.points[(edge + 1) % count];
        double pieces = 1.0;
        if (chain.spacing > 0.0)
            pieces = std::max(
                1.0, std::ceil(std::hypot(end.x - start.x, end.y - start.y) / chain.spacing));
        total += pieces - 1.0;
        if (total > MAX_INPUT_COUNT)
            throw Error(message("the spacing ", chain.spacing, " adds too many points"));
        result.push_back(start);
        for (double piece = 1.0; piece < pieces; piece++)
        {
            const double fraction = piece / pieces;
            result.push_back(
                {start.x + fraction * (end.x - start.x), start.y + fraction * (end.y - start.y)});
        }
    }
    if (!closed && count > 0) result.push_back(chain.points.back());
    return result;
}

Point point_inside(const std::vector<Point> & polygon)
{
    // a horizontal line through the middle of the widest gap between the
    // vertices' y values, which passes through no vertex
    std::vector<double> heights;
    for (const Point & point : polygon) heights.push_back(point.y);
    std::sort(heights.begin(), heights.end());
    double best_gap = 0.0;
    double y = 0.0;
    for (std::size_t index = 0; index + 1 < heights.size(); index++)
    {
        const double gap = heights[index + 1] - heights[index];
        if (gap > best_gap)
        {
            best_gap = gap;
            y = heights[index] + 0.5 * gap;
        }
    }
    if (best_gap == 0.0) throw Error("a hole polygon has no area");

    // the polygon is inside between pairs of crossings; take the middle of
    // the widest
    std::vector<double> crossings;
    for (std::size_t index = 0; index < polygon.size(); index++)
    {
        const Point & start = polygon[index];
        const Point & end = polygon[(index + 1) % polygon.size()];
        if ((start.y < y) != (end.y < y))
            crossings.push_back(start.x + (y - start.y) * (end.x - start.x) / (end.y - start.y));
    }
    std::sort(crossings.begin(), crossings.end());
    double best_width = 0.0;
    double x = 0.0;
    for (std::size_t index = 0; index + 1 < crossings.size(); index += 2)
    {
        const double width = crossings[index + 1] - crossings[index];
        if (width > best_width)
        {
            best_width = width;
            x = crossings[index] + 0.5 * width;
        }
    }
    if (best_width == 0.0) throw Error("a hole polygon has no area");
    return {x, y};
}

Mesh generate_mesh(const Geometry & geometry, const MeshOptions & options)
{
    if (!std::isfinite(options.maximum_area)) throw Error("the maximum area is not finite");
    if (!(options.minimum_angle >= 0.0 && options.minimum_angle <= MAX_MINIMUM_ANGLE))
        throw Error(message("the minimum angle is ", options.minimum_angle,
                            ", but it must be 0 to ", MAX_MINIMUM_ANGLE, " degrees"));
    if (!(options.merge_distance >= 0.0 && std::isfinite(options.merge_distance)))
        throw Error(message("the merge distance is ", options.merge_distance,
                            ", but it must be finite and at least 0"));

    Builder builder(options.merge_distance);
    std::vector<Point> holes = geometry.holes;
    std::vector<Region> regions = geometry.regions;

    for (std::size_t polygon = 0; polygon < geometry.polygons.size(); polygon++)
    {
        const Chain & chain = geometry.polygons[polygon];
        check_chain(chain, 3, "polygon", polygon);
        builder.add_chain(densify(chain, true), true, chain.marker);
    }
    for (std::size_t polygon = 0; polygon < geometry.hole_polygons.size(); polygon++)
    {
        const Chain & chain = geometry.hole_polygons[polygon];
        check_chain(chain, 3, "hole polygon", polygon);
        builder.add_chain(densify(chain, true), true, chain.marker);
        try
        {
            holes.push_back(point_inside(chain.points));
        }
        catch (const Error &)
        {
            throw Error(message("hole polygon ", polygon, " has no area"));
        }
    }
    for (std::size_t line = 0; line < geometry.lines.size(); line++)
    {
        const Chain & chain = geometry.lines[line];
        check_chain(chain, 2, "line", line);
        builder.add_chain(densify(chain, false), false, chain.marker);
    }
    for (std::size_t ring = 0; ring < geometry.rings.size(); ring++)
    {
        const Ring & item = geometry.rings[ring];
        check_finite(item.center, "ring", ring);
        if (!(item.radius > 0.0 && std::isfinite(item.radius)))
            throw Error(message("ring ", ring, " has a radius of ", item.radius,
                                ", but it must be positive"));
        if (item.point_count < 3)
            throw Error(message("ring ", ring, " has ", item.point_count,
                                " points, but at least 3 are needed"));
        std::vector<Point> circle;
        for (int point = 0; point < item.point_count; point++)
        {
            const double angle = 2.0 * std::numbers::pi * point / item.point_count;
            circle.push_back({item.center.x + item.radius * std::cos(angle),
                              item.center.y + item.radius * std::sin(angle)});
        }
        builder.add_chain(circle, true, item.marker);
        builder.add_vertex(item.center);
        regions.push_back({item.center, item.attribute, item.area_limit});
    }
    for (std::size_t point = 0; point < geometry.points.size(); point++)
    {
        check_finite(geometry.points[point], "point", point);
        builder.add_vertex(geometry.points[point]);
    }
    for (std::size_t hole = 0; hole < holes.size(); hole++) check_finite(holes[hole], "hole", hole);

    std::vector<double> region_list;
    bool has_region_limits = false;
    for (std::size_t region = 0; region < regions.size(); region++)
    {
        const Region & item = regions[region];
        check_finite(item.point, "region", region);
        if (!std::isfinite(item.attribute) || !std::isfinite(item.area_limit))
            throw Error(message("region ", region, " has an attribute or area that is not finite"));
        // Triangle reads a negative area as no limit
        const double area_limit = item.area_limit > 0.0 ? item.area_limit : -1.0;
        has_region_limits = has_region_limits || item.area_limit > 0.0;
        region_list.insert(region_list.end(),
                           {item.point.x, item.point.y, item.attribute, area_limit});
    }

    triangulateio input{};
    input.pointlist = builder.vertices().empty() ? nullptr : &builder.vertices()[0].x;
    input.numberofpoints = static_cast<int>(builder.vertices().size());
    input.segmentlist = builder.segments().empty() ? nullptr : builder.segments()[0].data();
    input.segmentmarkerlist = builder.markers().empty() ? nullptr : builder.markers().data();
    input.numberofsegments = static_cast<int>(builder.segments().size());
    input.holelist = holes.empty() ? nullptr : &holes[0].x;
    input.numberofholes = static_cast<int>(holes.size());
    input.regionlist = region_list.empty() ? nullptr : region_list.data();
    input.numberofregions = static_cast<int>(regions.size());

    // p: segments, z: numbered from 0, n: neighbors, A: region attributes,
    // a: region limits, a<area>: the maximum area, q<angle>: the minimum angle
    std::string switches = "pzn";
    if (!regions.empty()) switches += "A";
    if (has_region_limits) switches += "a";
    if (options.maximum_area > 0.0) switches += "a" + switch_number(options.maximum_area);
    if (options.minimum_angle > 0.0) switches += "q" + switch_number(options.minimum_angle);
    if (options.conforming_delaunay) switches += "D";

    TriangleOutput output;
    run_triangle(switches, input, output);
    Mesh mesh = from_triangulateio(output.get());
    // Triangle removes the triangles outside the segments, so a domain
    // whose boundary is not closed by them has none
    if (mesh.triangles.empty())
        throw Error("the mesh has no triangles; close the domain's boundary with polygons or "
                    "segments");
    return mesh;
}

} // namespace triangle_utils
