// Grids of polygonal cells built from a mesh, for MODFLOW 6 DISV.

#include "grid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <numeric>
#include <utility>
#include <vector>

#include "error.hpp"

namespace triangle_utils
{

namespace
{

// how far past 90 degrees an angle opposite a boundary edge, or past 180 the
// two opposite an interior edge, may be from rounding, in degrees
constexpr double ANGLE_TOLERANCE = 1e-6;
// the distance, as a fraction of an edge's length, within which the points
// on each side of it are the same Voronoi vertex, as for a rectangle's two
// triangles
constexpr double MERGE_FRACTION = 1e-9;

Point midpoint(const Point & a, const Point & b)
{
    return {0.5 * (a.x + b.x), 0.5 * (a.y + b.y)};
}

double distance(const Point & a, const Point & b)
{
    return std::hypot(b.x - a.x, b.y - a.y);
}

// The angle at a between b and c, in degrees.
double angle(const Point & a, const Point & b, const Point & c)
{
    const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - a.x, vy = c.y - a.y;
    return std::atan2(std::abs(ux * vy - uy * vx), ux * vx + uy * vy) * 180.0 / std::numbers::pi;
}

Point circumcenter(const Point & a, const Point & b, const Point & c)
{
    // relative to a, which keeps large coordinates from losing precision
    const double bx = b.x - a.x, by = b.y - a.y, cx = c.x - a.x, cy = c.y - a.y;
    const double twice = 2.0 * (bx * cy - by * cx);
    const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
    return {a.x + (cy * b2 - by * c2) / twice, a.y + (bx * c2 - cx * b2) / twice};
}

// Twice the signed area of a polygon, positive when counterclockwise.
double twice_area(const std::vector<Point> & polygon)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < polygon.size(); i++)
    {
        const Point & a = polygon[i];
        const Point & b = polygon[(i + 1) % polygon.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return sum;
}

void check_mesh(const Mesh & mesh)
{
    check_sizes(mesh);
    check_triangles(mesh);
    if (mesh.triangles.empty()) throw Error("the mesh has no triangles");
}

// Throw Error unless every triangle's circumcenter is in the mesh and on the
// side of each edge its Voronoi cells need: an angle opposite a boundary
// edge is at most 90 degrees, and two opposite an interior edge sum to at
// most 180.
void check_delaunay(const Mesh & mesh, const std::vector<std::array<int, 3>> & neighbors)
{
    const auto & v = mesh.vertices;
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        const auto & t = mesh.triangles[triangle];
        for (int corner = 0; corner < 3; corner++)
        {
            const int b = t[(corner + 1) % 3], c = t[(corner + 2) % 3];
            const double opposite = angle(v[t[corner]], v[b], v[c]);
            const int neighbor = neighbors[triangle][corner];
            if (neighbor == NO_NEIGHBOR)
            {
                if (opposite > 90.0 + ANGLE_TOLERANCE)
                    throw Error(message("the angle opposite the boundary edge from vertex ", b,
                                        " to vertex ", c, " is ", opposite,
                                        " degrees, over 90, so its triangle's circumcenter is "
                                        "outside the mesh; generate or refine the mesh with "
                                        "conforming Delaunay triangles"));
                continue;
            }
            if (neighbor < static_cast<int>(triangle)) continue;
            const auto & n = mesh.triangles[neighbor];
            int across = 0;
            while (n[across] == b || n[across] == c) across++;
            const double sum = opposite + angle(v[n[across]], v[b], v[c]);
            if (sum > 180.0 + ANGLE_TOLERANCE)
                throw Error(message("the angles opposite the edge from vertex ", b, " to vertex ",
                                    c, " sum to ", sum,
                                    " degrees, over 180, so the mesh is not Delaunay; generate "
                                    "or refine the mesh with conforming Delaunay triangles"));
        }
    }
}

// Sets of points that are one Voronoi vertex.
class Merged
{
public:
    explicit Merged(std::size_t count) : m_parent(count)
    {
        std::iota(m_parent.begin(), m_parent.end(), 0);
    }

    int root(int item)
    {
        while (m_parent[item] != item)
        {
            m_parent[item] = m_parent[m_parent[item]];
            item = m_parent[item];
        }
        return item;
    }

    void join(int a, int b)
    {
        a = root(a);
        b = root(b);
        // the smaller index is the root, so a set's point is its first
        if (a != b) m_parent[std::max(a, b)] = std::min(a, b);
    }

private:
    std::vector<int> m_parent;
};

// Copy the attributes of a triangle to the end of a cell's.
void add_attributes(const Mesh & mesh, std::size_t triangle, std::vector<double> & attributes)
{
    const auto first = mesh.triangle_attributes.begin() +
                       static_cast<std::ptrdiff_t>(triangle) * mesh.triangle_attribute_count;
    attributes.insert(attributes.end(), first, first + mesh.triangle_attribute_count);
}

} // namespace

Grid voronoi_grid(const Mesh & mesh)
{
    check_mesh(mesh);
    const auto neighbors = find_neighbors(mesh);
    check_delaunay(mesh, neighbors);
    const auto & v = mesh.vertices;
    const std::size_t triangle_count = mesh.triangles.size();

    // the points of the Voronoi vertices: each triangle's circumcenter, then
    // each boundary edge's midpoint
    std::vector<Point> points;
    for (const auto & t : mesh.triangles) points.push_back(circumcenter(v[t[0]], v[t[1]], v[t[2]]));
    std::vector<std::array<int, 3>> edge_point(triangle_count, {-1, -1, -1});
    for (std::size_t triangle = 0; triangle < triangle_count; triangle++)
    {
        const auto & t = mesh.triangles[triangle];
        for (int corner = 0; corner < 3; corner++)
            if (neighbors[triangle][corner] == NO_NEIGHBOR)
            {
                edge_point[triangle][corner] = static_cast<int>(points.size());
                points.push_back(midpoint(v[t[(corner + 1) % 3]], v[t[(corner + 2) % 3]]));
            }
    }
    Merged merged(points.size());
    for (std::size_t triangle = 0; triangle < triangle_count; triangle++)
    {
        const auto & t = mesh.triangles[triangle];
        for (int corner = 0; corner < 3; corner++)
        {
            const double length = distance(v[t[(corner + 1) % 3]], v[t[(corner + 2) % 3]]);
            const int neighbor = neighbors[triangle][corner];
            const int other = neighbor == NO_NEIGHBOR ? edge_point[triangle][corner] : neighbor;
            if (distance(points[triangle], points[other]) <= MERGE_FRACTION * length)
                merged.join(static_cast<int>(triangle), other);
        }
    }

    Grid grid;
    grid.attribute_count = mesh.triangle_attribute_count;
    std::vector<int> grid_vertex(points.size(), -1);
    auto vertex_of_point = [&](int point)
    {
        const int root = merged.root(point);
        if (grid_vertex[root] < 0)
        {
            grid_vertex[root] = static_cast<int>(grid.vertices.size());
            grid.vertices.push_back(points[root]);
        }
        return grid_vertex[root];
    };

    // a triangle and corner at each vertex, and how many triangles meet there
    std::vector<std::pair<int, int>> at_vertex(v.size(), {-1, -1});
    std::vector<int> triangle_counts(v.size(), 0);
    for (std::size_t triangle = 0; triangle < triangle_count; triangle++)
        for (int corner = 0; corner < 3; corner++)
        {
            const int vertex = mesh.triangles[triangle][corner];
            at_vertex[vertex] = {static_cast<int>(triangle), corner};
            triangle_counts[vertex]++;
        }
    auto corner_of = [&](int triangle, int vertex)
    {
        const auto & t = mesh.triangles[triangle];
        return t[0] == vertex ? 0 : (t[1] == vertex ? 1 : 2);
    };

    for (std::size_t vertex = 0; vertex < v.size(); vertex++)
    {
        if (at_vertex[vertex].first < 0)
            throw Error(message("vertex ", vertex, " is in no triangle, so it has no cell"));
        const int here = static_cast<int>(vertex);
        // turn clockwise to the first triangle after the boundary, if any
        auto [first, first_corner] = at_vertex[vertex];
        bool on_boundary = false;
        for (int turns = 0; turns < triangle_counts[vertex]; turns++)
        {
            const int previous = neighbors[first][(first_corner + 2) % 3];
            if (previous == NO_NEIGHBOR)
            {
                on_boundary = true;
                break;
            }
            first = previous;
            first_corner = corner_of(first, here);
        }

        // the cell counterclockwise, as points, and the area of each side
        // of the segments through the vertex
        std::vector<int> ring;
        std::vector<std::pair<std::size_t, double>> sides;
        int triangle = first, corner = first_corner, visited = 0;
        while (true)
        {
            const auto & t = mesh.triangles[triangle];
            ring.push_back(triangle);
            visited++;
            if (grid.attribute_count > 0)
            {
                // the part of the cell in this triangle, which is signed
                // when the circumcenter is outside it, so the parts sum
                const double part =
                    twice_area({v[here], midpoint(v[here], v[t[(corner + 1) % 3]]),
                                points[triangle], midpoint(v[here], v[t[(corner + 2) % 3]])});
                auto same = std::find_if(
                    sides.begin(), sides.end(),
                    [&](const auto & side)
                    {
                        return std::equal(
                            mesh.triangle_attributes.begin() + side.first * grid.attribute_count,
                            mesh.triangle_attributes.begin() +
                                (side.first + 1) * grid.attribute_count,
                            mesh.triangle_attributes.begin() + triangle * grid.attribute_count);
                    });
                if (same == sides.end())
                    sides.push_back({static_cast<std::size_t>(triangle), part});
                else
                    same->second += part;
            }
            const int next = neighbors[triangle][(corner + 1) % 3];
            if (next == NO_NEIGHBOR || next == first || visited > triangle_counts[vertex]) break;
            triangle = next;
            corner = corner_of(triangle, here);
        }
        if (visited != triangle_counts[vertex])
            throw Error(message("vertex ", vertex,
                                " is where parts of the mesh meet at a point, so its cell is "
                                "not one polygon"));

        std::vector<int> cell;
        if (on_boundary)
        {
            const Point first_middle = points[edge_point[first][(first_corner + 2) % 3]];
            const Point last_middle = points[edge_point[triangle][(corner + 1) % 3]];
            // the vertex is a corner of its cell only where the boundary
            // turns
            const double ux = first_middle.x - v[here].x, uy = first_middle.y - v[here].y;
            const double wx = last_middle.x - v[here].x, wy = last_middle.y - v[here].y;
            if (std::abs(ux * wy - uy * wx) >
                MERGE_FRACTION * std::hypot(ux, uy) * std::hypot(wx, wy))
            {
                cell.push_back(static_cast<int>(grid.vertices.size()));
                grid.vertices.push_back(v[here]);
            }
            cell.push_back(vertex_of_point(edge_point[first][(first_corner + 2) % 3]));
            for (const int part : ring) cell.push_back(vertex_of_point(part));
            cell.push_back(vertex_of_point(edge_point[triangle][(corner + 1) % 3]));
        }
        else
        {
            for (const int part : ring) cell.push_back(vertex_of_point(part));
        }
        // merged points appear once
        cell.erase(std::unique(cell.begin(), cell.end()), cell.end());
        while (cell.size() > 1 && cell.front() == cell.back()) cell.pop_back();
        if (cell.size() < 3)
            throw Error(message("the cell of vertex ", vertex, " has fewer than 3 corners"));
        std::reverse(cell.begin(), cell.end());
        grid.cells.push_back(std::move(cell));
        grid.centers.push_back(v[here]);
        grid.sources.push_back(here);
        if (grid.attribute_count > 0)
        {
            // the attributes of the side with the most of the cell
            const auto largest =
                std::max_element(sides.begin(), sides.end(), [](const auto & a, const auto & b)
                                 { return a.second < b.second; });
            add_attributes(mesh, largest->first, grid.attributes);
        }
    }
    return grid;
}

Grid triangle_grid(const Mesh & mesh, TriangleCenter center)
{
    check_mesh(mesh);
    const auto & v = mesh.vertices;
    Grid grid;
    grid.attribute_count = mesh.triangle_attribute_count;
    // the vertices the triangles use, in order
    std::vector<int> grid_vertex(v.size(), -1);
    for (const auto & t : mesh.triangles)
        for (const int vertex : t) grid_vertex[vertex] = 0;
    for (std::size_t vertex = 0; vertex < v.size(); vertex++)
        if (grid_vertex[vertex] == 0)
        {
            grid_vertex[vertex] = static_cast<int>(grid.vertices.size());
            grid.vertices.push_back(v[vertex]);
        }
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        const auto & t = mesh.triangles[triangle];
        grid.cells.push_back({grid_vertex[t[0]], grid_vertex[t[2]], grid_vertex[t[1]]});
        grid.centers.push_back(center == TriangleCenter::CENTROID
                                   ? Point{(v[t[0]].x + v[t[1]].x + v[t[2]].x) / 3.0,
                                           (v[t[0]].y + v[t[1]].y + v[t[2]].y) / 3.0}
                                   : circumcenter(v[t[0]], v[t[1]], v[t[2]]));
        grid.sources.push_back(static_cast<int>(triangle));
        if (grid.attribute_count > 0) add_attributes(mesh, triangle, grid.attributes);
    }
    return grid;
}

} // namespace triangle_utils
