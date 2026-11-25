// Tests the Voronoi and triangle grids: cells are clockwise and convex, do
// not overlap, cover the mesh, write each vertex once, and connect centers
// perpendicular to their shared faces.

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "error.hpp"
#include "geometry.hpp"
#include "grid.hpp"

using namespace triangle_utils;

namespace
{

constexpr double TOLERANCE = 1e-9;
constexpr double AQUIFER = 1.0;
constexpr double LAKE = 2.0;

// A square with a lake, a river, a well, and a square hole, meshed with
// conforming Delaunay triangles.
Mesh valley_mesh()
{
    Geometry geometry;
    geometry.polygons.push_back({{{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}}, 0.0, 1});
    geometry.polygons.push_back({{{6.0, 6.0}, {8.5, 6.0}, {8.5, 8.5}, {6.0, 8.5}}, 0.0, 2});
    geometry.hole_polygons.push_back({{{6.0, 1.5}, {8.0, 1.5}, {8.0, 3.0}, {6.0, 3.0}}});
    geometry.lines.push_back({{{2.0, 0.0}, {3.0, 5.0}, {2.5, 10.0}}, 0.5, 3});
    geometry.points.push_back({4.5, 4.5});
    // the river divides the aquifer, so it has a region on each side
    geometry.regions.push_back({{1.0, 1.0}, AQUIFER, 0.0});
    geometry.regions.push_back({{5.0, 1.0}, AQUIFER, 0.0});
    geometry.regions.push_back({{7.0, 7.0}, LAKE, 0.1});
    MeshOptions options;
    options.maximum_area = 0.3;
    options.minimum_angle = 28.0;
    options.conforming_delaunay = true;
    return generate_mesh(geometry, options);
}

double cross(const Point & a, const Point & b, const Point & c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

double twice_area(const Grid & grid, std::size_t cell)
{
    const auto & corners = grid.cells[cell];
    double sum = 0.0;
    for (std::size_t i = 0; i < corners.size(); i++)
    {
        const Point & a = grid.vertices[corners[i]];
        const Point & b = grid.vertices[corners[(i + 1) % corners.size()]];
        sum += a.x * b.y - b.x * a.y;
    }
    return sum;
}

// The length of the mesh's boundary: the edges of one triangle.
double boundary_length(const Mesh & mesh)
{
    const auto neighbors = find_neighbors(mesh);
    double length = 0.0;
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
        for (int corner = 0; corner < 3; corner++)
            if (neighbors[triangle][corner] == NO_NEIGHBOR)
            {
                const Point & a = mesh.vertices[mesh.triangles[triangle][(corner + 1) % 3]];
                const Point & b = mesh.vertices[mesh.triangles[triangle][(corner + 2) % 3]];
                length += std::hypot(b.x - a.x, b.y - a.y);
            }
    return length;
}

// Check that the cells tile the mesh: each is clockwise, the faces are
// shared by at most two cells running opposite ways, the unshared faces
// make up the boundary, and the areas sum to the mesh's. Returns the shared
// faces as (cell, cell, start, end).
std::vector<std::array<int, 4>> check_tiling(const Grid & grid, const Mesh & mesh)
{
    const double scale = std::sqrt(total_area(mesh));
    EXPECT_EQ(grid.cells.size(), grid.centers.size());
    EXPECT_EQ(grid.cells.size(), grid.sources.size());
    EXPECT_EQ(grid.attributes.size(), grid.cells.size() * grid.attribute_count);
    double area = 0.0;
    std::map<std::pair<int, int>, int> faces;
    for (std::size_t cell = 0; cell < grid.cells.size(); cell++)
    {
        const auto & corners = grid.cells[cell];
        EXPECT_GE(corners.size(), 3u);
        EXPECT_LT(twice_area(grid, cell), 0.0) << "cell " << cell << " is not clockwise";
        area -= 0.5 * twice_area(grid, cell);
        for (std::size_t i = 0; i < corners.size(); i++)
        {
            const auto face = std::make_pair(corners[i], corners[(i + 1) % corners.size()]);
            EXPECT_EQ(faces.count(face), 0u) << "two cells run along a face the same way";
            faces[face] = static_cast<int>(cell);
        }
    }
    EXPECT_NEAR(area, total_area(mesh), TOLERANCE * total_area(mesh));

    std::vector<std::array<int, 4>> shared;
    double unshared = 0.0;
    for (const auto & [face, cell] : faces)
    {
        const auto other = faces.find({face.second, face.first});
        if (other == faces.end())
        {
            const Point & a = grid.vertices[face.first];
            const Point & b = grid.vertices[face.second];
            unshared += std::hypot(b.x - a.x, b.y - a.y);
        }
        else if (cell < other->second)
            shared.push_back({cell, other->second, face.first, face.second});
    }
    EXPECT_NEAR(unshared, boundary_length(mesh), TOLERANCE * scale);

    // each vertex once
    std::vector<Point> sorted = grid.vertices;
    std::sort(sorted.begin(), sorted.end(), [](const Point & a, const Point & b)
              { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    for (std::size_t i = 0; i + 1 < sorted.size(); i++)
        for (std::size_t j = i + 1; j < sorted.size() && sorted[j].x - sorted[i].x <= 1e-9 * scale;
             j++)
            EXPECT_GT(std::hypot(sorted[j].x - sorted[i].x, sorted[j].y - sorted[i].y),
                      1e-9 * scale);
    return shared;
}

} // namespace

TEST(Grid, VoronoiCellsTileTheMeshAndAreConvex)
{
    const Mesh mesh = valley_mesh();
    const Grid grid = voronoi_grid(mesh);
    ASSERT_EQ(grid.cells.size(), mesh.vertices.size());
    check_tiling(grid, mesh);
    for (std::size_t cell = 0; cell < grid.cells.size(); cell++)
    {
        const Point & center = mesh.vertices[grid.sources[cell]];
        EXPECT_EQ(grid.centers[cell].x, center.x);
        EXPECT_EQ(grid.centers[cell].y, center.y);
        // clockwise corners turn right, except at a corner of a hole, where
        // the cell's own vertex is a reflex corner
        const auto & corners = grid.cells[cell];
        for (std::size_t i = 0; i < corners.size(); i++)
        {
            const Point & a = grid.vertices[corners[i]];
            const Point & b = grid.vertices[corners[(i + 1) % corners.size()]];
            const Point & c = grid.vertices[corners[(i + 2) % corners.size()]];
            if (b.x == center.x && b.y == center.y) continue;
            EXPECT_LE(cross(a, b, c), TOLERANCE) << "cell " << cell << " is not convex";
        }
    }
}

TEST(Grid, VoronoiCentersJoinPerpendicularToTheirFaces)
{
    const Mesh mesh = valley_mesh();
    const Grid grid = voronoi_grid(mesh);
    const auto shared = check_tiling(grid, mesh);
    ASSERT_FALSE(shared.empty());
    for (const auto & [first, second, start, end] : shared)
    {
        const Point & a = grid.centers[first];
        const Point & b = grid.centers[second];
        const Point & p = grid.vertices[start];
        const Point & q = grid.vertices[end];
        const double dx = b.x - a.x, dy = b.y - a.y, fx = q.x - p.x, fy = q.y - p.y;
        EXPECT_LE(std::abs(dx * fx + dy * fy), 1e-6 * std::hypot(dx, dy) * std::hypot(fx, fy));
        // the face crosses the line between the centers at its midpoint
        EXPECT_LT(cross(p, q, a) * cross(p, q, b), 0.0);
    }
}

TEST(Grid, VoronoiCellsTakeTheAttributeOfMostOfTheirArea)
{
    const Mesh mesh = valley_mesh();
    const Grid grid = voronoi_grid(mesh);
    ASSERT_EQ(grid.attribute_count, 1);
    int lake = 0;
    for (std::size_t cell = 0; cell < grid.cells.size(); cell++)
    {
        const Point & c = grid.centers[cell];
        const bool inside = c.x > 6.0 && c.x < 8.5 && c.y > 6.0 && c.y < 8.5;
        const bool outside = c.x < 6.0 || c.x > 8.5 || c.y < 6.0 || c.y > 8.5;
        if (inside) EXPECT_EQ(grid.attributes[cell], LAKE);
        if (outside) EXPECT_EQ(grid.attributes[cell], AQUIFER);
        if (grid.attributes[cell] == LAKE) lake++;
    }
    EXPECT_GT(lake, 0);
    // a cell on the shore's corner is mostly outside the lake
    for (std::size_t cell = 0; cell < grid.cells.size(); cell++)
        if (grid.centers[cell].x == 6.0 && grid.centers[cell].y == 6.0)
            EXPECT_EQ(grid.attributes[cell], AQUIFER);
}

TEST(Grid, RectangleCellsShareTheirCircumcenter)
{
    // two triangles of a rectangle share a circumcenter, one Voronoi vertex
    Mesh mesh;
    mesh.vertices = {{0.0, 0.0}, {2.0, 0.0}, {2.0, 1.0}, {0.0, 1.0}};
    mesh.triangles = {{0, 1, 2}, {0, 2, 3}};
    const Grid grid = voronoi_grid(mesh);
    check_tiling(grid, mesh);
    // the center, the four edge midpoints, and the four corners
    EXPECT_EQ(grid.vertices.size(), 9u);
    for (const auto & cell : grid.cells) EXPECT_EQ(cell.size(), 4u);
}

TEST(Grid, TriangleCellsAreTheTriangles)
{
    Mesh mesh = valley_mesh();
    // a vertex no triangle uses is left out
    mesh.vertices.push_back({20.0, 20.0});
    if (!mesh.vertex_markers.empty()) mesh.vertex_markers.push_back(0);
    for (const TriangleCenter center : {TriangleCenter::CENTROID, TriangleCenter::CIRCUMCENTER})
    {
        const Grid grid = triangle_grid(mesh, center);
        ASSERT_EQ(grid.cells.size(), mesh.triangles.size());
        EXPECT_EQ(grid.vertices.size(), mesh.vertices.size() - 1);
        const auto shared = check_tiling(grid, mesh);
        for (std::size_t cell = 0; cell < grid.cells.size(); cell++)
        {
            EXPECT_EQ(grid.sources[cell], static_cast<int>(cell));
            EXPECT_EQ(grid.attributes[cell], mesh.triangle_attributes[cell]);
            const auto & t = mesh.triangles[cell];
            const Point & a = mesh.vertices[t[0]];
            const Point & b = mesh.vertices[t[1]];
            const Point & c = mesh.vertices[t[2]];
            const Point & at = grid.centers[cell];
            if (center == TriangleCenter::CENTROID)
            {
                EXPECT_NEAR(at.x, (a.x + b.x + c.x) / 3.0, TOLERANCE);
                EXPECT_NEAR(at.y, (a.y + b.y + c.y) / 3.0, TOLERANCE);
            }
            else
            {
                const double ra = std::hypot(at.x - a.x, at.y - a.y);
                EXPECT_NEAR(std::hypot(at.x - b.x, at.y - b.y), ra, TOLERANCE * ra);
                EXPECT_NEAR(std::hypot(at.x - c.x, at.y - c.y), ra, TOLERANCE * ra);
            }
        }
        // circumcenters join perpendicular to the faces, as centroids do not
        if (center == TriangleCenter::CIRCUMCENTER)
            for (const auto & [first, second, start, end] : shared)
            {
                const Point & a = grid.centers[first];
                const Point & b = grid.centers[second];
                const Point & p = grid.vertices[start];
                const Point & q = grid.vertices[end];
                const double dx = b.x - a.x, dy = b.y - a.y, fx = q.x - p.x, fy = q.y - p.y;
                EXPECT_LE(std::abs(dx * fx + dy * fy),
                          1e-6 * std::hypot(fx, fy) * std::hypot(fx, fy));
            }
    }
}

TEST(Grid, ErrorsGiveTheReason)
{
    auto reason = [](const Mesh & mesh)
    {
        try
        {
            voronoi_grid(mesh);
        }
        catch (const Error & error)
        {
            return std::string(error.what());
        }
        return std::string();
    };
    Mesh obtuse;
    obtuse.vertices = {{0.0, 0.0}, {2.0, 0.0}, {1.0, 0.2}};
    obtuse.triangles = {{0, 1, 2}};
    EXPECT_NE(reason(obtuse).find("the angle opposite the boundary edge from vertex 0 to vertex 1"),
              std::string::npos)
        << reason(obtuse);

    Mesh thin;
    thin.vertices = {{-1.0, 0.0}, {1.0, 0.0}, {0.0, 0.2}, {0.0, -0.2}};
    thin.triangles = {{0, 1, 2}, {1, 0, 3}};
    EXPECT_NE(reason(thin).find("the angles opposite the edge from vertex 0 to vertex 1 sum to"),
              std::string::npos)
        << reason(thin);
    EXPECT_NE(reason(thin).find("conforming Delaunay"), std::string::npos);

    Mesh unused;
    unused.vertices = {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {5.0, 5.0}};
    unused.triangles = {{0, 1, 2}};
    EXPECT_EQ(reason(unused), "vertex 3 is in no triangle, so it has no cell");

    EXPECT_EQ(reason(Mesh{}), "the mesh has no triangles");
}
