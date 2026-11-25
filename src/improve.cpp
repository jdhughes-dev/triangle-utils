// Improving a mesh by moving vertices and flipping edges.

#include "improve.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <utility>
#include <vector>

#include "error.hpp"

namespace triangle_utils
{

namespace
{

// the margin, in degrees, by which a move or a flip must improve an angle,
// so neither repeats on rounding alone
constexpr double ANGLE_MARGIN = 1e-9;

// the fractions of the way to its target a vertex is tried at, in turn
constexpr std::array<double, 3> STEP_FRACTIONS = {1.0, 0.5, 0.25};

// the steps, as fractions of the mean length of a vertex's edges, of the
// search around a vertex whose triangles exceed the target angle
constexpr std::array<double, 4> SEARCH_STEPS = {0.2, 0.1, 0.05, 0.02};

// Twice the signed area of the triangle a, b, c.
double cross(const Point & a, const Point & b, const Point & c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// The angle in degrees at a between b and c.
double angle_at(const Point & a, const Point & b, const Point & c)
{
    const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - a.x, vy = c.y - a.y;
    return std::atan2(std::abs(ux * vy - uy * vx), ux * vx + uy * vy) * 180.0 / std::numbers::pi;
}

// The smallest and largest angles of a set of triangles.
struct Shape
{
    double smallest = 180.0;
    double largest = 0.0;
};

void add_triangle(Shape & shape, const Point & a, const Point & b, const Point & c)
{
    for (const double angle : {angle_at(a, b, c), angle_at(b, c, a), angle_at(c, a, b)})
    {
        shape.smallest = std::min(shape.smallest, angle);
        shape.largest = std::max(shape.largest, angle);
    }
}

// Whether a new area for a triangle would exceed its area limit, where it
// has a positive one, and its current area, so a triangle over its limit
// may still shrink.
bool over_limit(const Mesh & mesh, int triangle, double area, double current)
{
    return !mesh.triangle_area_limits.empty() && mesh.triangle_area_limits[triangle] > 0.0 &&
           area > mesh.triangle_area_limits[triangle] && area > current;
}

// Whether two triangles carry the same attributes and area limit, so an
// edge between them can be flipped without changing either.
bool same_values(const Mesh & mesh, int first, int second)
{
    const std::size_t count = static_cast<std::size_t>(mesh.triangle_attribute_count);
    for (std::size_t attribute = 0; attribute < count; attribute++)
        if (mesh.triangle_attributes[first * count + attribute] !=
            mesh.triangle_attributes[second * count + attribute])
            return false;
    return mesh.triangle_area_limits.empty() ||
           mesh.triangle_area_limits[first] == mesh.triangle_area_limits[second];
}

// Flip edges that are not segments until every one is locally Delaunay: the
// two angles opposite it sum to at most 180 degrees.
void flip_to_delaunay(Mesh & mesh, const std::set<std::pair<int, int>> & segments)
{
    // each flip reduces the sum of the opposite angles below 180 degrees, so
    // the passes end; the limit guards against a mesh where they do not
    constexpr int MAX_PASSES = 10000;
    for (int pass = 0; pass < MAX_PASSES; pass++)
    {
        const auto neighbors = find_neighbors(mesh);
        std::vector<bool> changed(mesh.triangles.size(), false);
        bool flipped = false;
        for (std::size_t first = 0; first < mesh.triangles.size(); first++)
        {
            for (int corner = 0; corner < 3 && !changed[first]; corner++)
            {
                const int second = neighbors[first][corner];
                if (second == NO_NEIGHBOR || changed[second]) continue;
                auto & one = mesh.triangles[first];
                auto & other = mesh.triangles[second];
                // the first triangle is c, a, b with the edge a-b opposite c;
                // the second runs b to a, with d opposite
                const int c = one[corner];
                const int a = one[(corner + 1) % 3];
                const int b = one[(corner + 2) % 3];
                if (segments.contains(std::minmax(a, b)) ||
                    !same_values(mesh, static_cast<int>(first), second))
                    continue;
                int d = -1;
                for (const int vertex : other)
                    if (vertex != a && vertex != b) d = vertex;
                const auto & v = mesh.vertices;
                if (angle_at(v[c], v[a], v[b]) + angle_at(v[d], v[b], v[a]) <= 180.0 + ANGLE_MARGIN)
                    continue;
                // the flipped triangles must both be counterclockwise
                if (!(cross(v[c], v[a], v[d]) > 0.0 && cross(v[d], v[b], v[c]) > 0.0)) continue;
                // the two share an area limit, which both flipped triangles must meet
                const double larger =
                    0.5 * std::max(cross(v[c], v[a], v[b]), cross(v[d], v[b], v[a]));
                if (over_limit(mesh, static_cast<int>(first), 0.5 * cross(v[c], v[a], v[d]),
                               larger) ||
                    over_limit(mesh, static_cast<int>(first), 0.5 * cross(v[d], v[b], v[c]),
                               larger))
                    continue;
                one = {c, a, d};
                other = {d, b, c};
                changed[first] = changed[second] = true;
                flipped = true;
            }
        }
        if (!flipped) return;
    }
}

} // namespace

Mesh improve_mesh(const Mesh & mesh, const ImproveOptions & options)
{
    if (!(options.maximum_angle == 0.0 ||
          (options.maximum_angle > 60.0 && options.maximum_angle < 180.0)))
        throw Error(message("the maximum angle is ", options.maximum_angle,
                            ", but it must be 0 or over 60 and under 180 degrees"));
    if (!(options.minimum_angle >= 0.0 && options.minimum_angle <= 60.0))
        throw Error(message("the minimum angle is ", options.minimum_angle,
                            ", but it must be 0 to 60 degrees"));
    if (options.iterations < 0)
        throw Error(
            message("the iterations are ", options.iterations, ", but they must be at least 0"));
    if (!(options.tolerance >= 0.0 && std::isfinite(options.tolerance)))
        throw Error(message("the tolerance is ", options.tolerance,
                            ", but it must be finite and at least 0"));
    if (mesh.triangles.empty()) throw Error("the mesh has no triangles to improve");
    check_sizes(mesh);
    check_triangles(mesh);

    Mesh result = mesh;
    std::set<std::pair<int, int>> segments;
    std::vector<bool> fixed(result.vertices.size(), false);
    for (const auto & segment : result.segments)
    {
        segments.insert(std::minmax(segment[0], segment[1]));
        fixed[segment[0]] = fixed[segment[1]] = true;
    }
    for (const int vertex : result.fixed_vertices) fixed[vertex] = true;
    // a vertex where triangles with different attributes or area limits meet,
    // which marks the edge of a region even without a segment
    {
        std::vector<int> first(result.vertices.size(), -1);
        for (std::size_t triangle = 0; triangle < result.triangles.size(); triangle++)
            for (const int vertex : result.triangles[triangle])
            {
                if (first[vertex] < 0)
                    first[vertex] = static_cast<int>(triangle);
                else if (!same_values(result, first[vertex], static_cast<int>(triangle)))
                    fixed[vertex] = true;
            }
    }
    // vertices on the boundary, where an edge has one triangle
    {
        const auto neighbors = find_neighbors(result);
        for (std::size_t triangle = 0; triangle < result.triangles.size(); triangle++)
            for (int corner = 0; corner < 3; corner++)
                if (neighbors[triangle][corner] == NO_NEIGHBOR)
                {
                    fixed[result.triangles[triangle][(corner + 1) % 3]] = true;
                    fixed[result.triangles[triangle][(corner + 2) % 3]] = true;
                }
    }

    // the smallest angle a move may leave: the option, or the mesh's own
    Shape whole;
    for (const auto & corners : result.triangles)
        add_triangle(whole, result.vertices[corners[0]], result.vertices[corners[1]],
                     result.vertices[corners[2]]);
    const double floor = options.minimum_angle > 0.0 ? options.minimum_angle : whole.smallest;
    const double target = options.maximum_angle;

    for (int iteration = 0; iteration < options.iterations; iteration++)
    {
        flip_to_delaunay(result, segments);
        // the triangles around each vertex, and the mean edge length
        std::vector<std::vector<int>> around(result.vertices.size());
        double total_length = 0.0;
        for (std::size_t triangle = 0; triangle < result.triangles.size(); triangle++)
        {
            const auto & corners = result.triangles[triangle];
            for (int corner = 0; corner < 3; corner++)
            {
                around[corners[corner]].push_back(static_cast<int>(triangle));
                const Point & a = result.vertices[corners[corner]];
                const Point & b = result.vertices[corners[(corner + 1) % 3]];
                total_length += std::hypot(b.x - a.x, b.y - a.y);
            }
        }
        const double mean_length = total_length / (3.0 * result.triangles.size());

        double largest_move = 0.0;
        for (std::size_t vertex = 0; vertex < result.vertices.size(); vertex++)
        {
            if (fixed[vertex] || around[vertex].empty()) continue;
            const Point here = result.vertices[vertex];
            // the mean length of the vertex's edges, the scale of its moves
            double local_length = 0.0;
            int edge_count = 0;
            for (const int triangle : around[vertex])
                for (const int corner : result.triangles[triangle])
                    if (corner != static_cast<int>(vertex))
                    {
                        const Point & other = result.vertices[corner];
                        local_length += std::hypot(other.x - here.x, other.y - here.y);
                        edge_count++;
                    }
            local_length /= edge_count;
            // the shape of the triangles around the vertex with it at a
            // point, or none if a triangle would not be counterclockwise or
            // would exceed its area limit
            auto shape_with = [&](const Point & at, Shape & shape)
            {
                const double minimum_area = 1e-12 * local_length * local_length;
                for (const int triangle : around[vertex])
                {
                    std::array<Point, 3> points;
                    for (int corner = 0; corner < 3; corner++)
                    {
                        const int index = result.triangles[triangle][corner];
                        points[corner] =
                            index == static_cast<int>(vertex) ? at : result.vertices[index];
                    }
                    const double twice_area = cross(points[0], points[1], points[2]);
                    if (twice_area <= minimum_area) return false;
                    const auto & corners = result.triangles[triangle];
                    const double current =
                        0.5 * cross(result.vertices[corners[0]], result.vertices[corners[1]],
                                    result.vertices[corners[2]]);
                    if (over_limit(result, triangle, 0.5 * twice_area, current)) return false;
                    add_triangle(shape, points[0], points[1], points[2]);
                }
                return true;
            };
            // the worst angle to reduce first: the excess over the target,
            // then the smallest angle
            auto excess = [&](const Shape & shape)
            { return target > 0.0 ? std::max(shape.largest, target) : 0.0; };

            Shape now;
            // a vertex whose triangles are already degenerate stays put
            if (!shape_with(here, now)) continue;

            Point goal;
            if (options.smoothing == Smoothing::LAPLACIAN)
            {
                std::set<int> neighbors;
                for (const int triangle : around[vertex])
                    for (const int corner : result.triangles[triangle])
                        if (corner != static_cast<int>(vertex)) neighbors.insert(corner);
                for (const int neighbor : neighbors)
                {
                    goal.x += result.vertices[neighbor].x / neighbors.size();
                    goal.y += result.vertices[neighbor].y / neighbors.size();
                }
            }
            else
            {
                double area_sum = 0.0;
                for (const int triangle : around[vertex])
                {
                    const auto & corners = result.triangles[triangle];
                    const Point & a = result.vertices[corners[0]];
                    const Point & b = result.vertices[corners[1]];
                    const Point & c = result.vertices[corners[2]];
                    const double area = 0.5 * cross(a, b, c);
                    goal.x += area * (a.x + b.x + c.x) / 3.0;
                    goal.y += area * (a.y + b.y + c.y) / 3.0;
                    area_sum += area;
                }
                goal.x /= area_sum;
                goal.y /= area_sum;
            }

            // whether the vertex at a point is better than now
            auto better = [&](const Point & at, Shape & then)
            {
                // the floor, or the vertex's own smallest angle if that is
                // lower, so a vertex beside a triangle below the floor can
                // still move without making it worse
                const double local_floor = std::min(floor, now.smallest);
                if (!shape_with(at, then) || then.smallest < local_floor - ANGLE_MARGIN)
                    return false;
                return excess(then) < excess(now) - ANGLE_MARGIN ||
                       (excess(then) <= excess(now) + ANGLE_MARGIN &&
                        then.smallest > now.smallest + ANGLE_MARGIN);
            };
            bool moved = false;
            Point best = here;
            for (const double fraction : STEP_FRACTIONS)
            {
                const Point at{here.x + fraction * (goal.x - here.x),
                               here.y + fraction * (goal.y - here.y)};
                Shape then;
                if (!better(at, then)) continue;
                best = at;
                moved = true;
                break;
            }
            // over the target, the smoothing may not reduce the largest angle,
            // so search steps in eight directions, shrinking, for the best move
            if (!moved && target > 0.0 && now.largest > target + ANGLE_MARGIN)
            {
                Shape best_shape = now;
                for (const double size : SEARCH_STEPS)
                {
                    for (int direction = 0; direction < 8; direction++)
                    {
                        const double theta = direction * std::numbers::pi / 4.0;
                        const Point at{here.x + size * local_length * std::cos(theta),
                                       here.y + size * local_length * std::sin(theta)};
                        Shape then;
                        if (!better(at, then)) continue;
                        if (excess(then) < excess(best_shape) - ANGLE_MARGIN ||
                            (excess(then) <= excess(best_shape) + ANGLE_MARGIN &&
                             then.smallest > best_shape.smallest + ANGLE_MARGIN) ||
                            !moved)
                        {
                            best = at;
                            best_shape = then;
                            moved = true;
                        }
                    }
                    if (moved) break;
                }
            }
            if (moved)
            {
                largest_move = std::max(largest_move, std::hypot(best.x - here.x, best.y - here.y));
                result.vertices[vertex] = best;
            }
        }
        if (largest_move <= options.tolerance * mean_length) break;
    }
    flip_to_delaunay(result, segments);
    result.neighbors = find_neighbors(result);
    return result;
}

} // namespace triangle_utils
