// Tests the connections of grid cells: a structured grid has the cell sizes,
// connections are symmetric and normal to their faces, cell areas follow from
// them, and grids whose centers are not between their faces are errors.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "connections.hpp"
#include "error.hpp"
#include "geometry.hpp"
#include "grid.hpp"

using namespace triangle_utils;

namespace
{

constexpr double TOLERANCE = 1e-9;
// the lattice of the structured grid
constexpr int COLUMNS = 5;
constexpr int ROWS = 4;
constexpr double DX = 2.0;
constexpr double DY = 1.0;

// A rectangle meshed with only the points of a lattice, so its Voronoi cells
// are rectangles.
Mesh lattice_mesh()
{
    Geometry geometry;
    std::vector<Point> boundary;
    for (int i = 0; i < COLUMNS; i++) boundary.push_back({i * DX, 0.0});
    for (int j = 1; j < ROWS; j++) boundary.push_back({(COLUMNS - 1) * DX, j * DY});
    for (int i = COLUMNS - 2; i >= 0; i--) boundary.push_back({i * DX, (ROWS - 1) * DY});
    for (int j = ROWS - 2; j > 0; j--) boundary.push_back({0.0, j * DY});
    geometry.polygons.push_back({boundary, 0.0, 1});
    for (int i = 1; i < COLUMNS - 1; i++)
        for (int j = 1; j < ROWS - 1; j++) geometry.points.push_back({i * DX, j * DY});
    MeshOptions options;
    options.conforming_delaunay = true;
    return generate_mesh(geometry, options);
}

// A square with a lake and a hole, meshed with conforming Delaunay
// triangles.
Mesh valley_mesh()
{
    Geometry geometry;
    geometry.polygons.push_back({{{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}}, 0.0, 1});
    geometry.polygons.push_back({{{6.0, 6.0}, {8.5, 6.0}, {8.5, 8.5}, {6.0, 8.5}}, 0.0, 2});
    geometry.hole_polygons.push_back({{{6.0, 1.5}, {8.0, 1.5}, {8.0, 3.0}, {6.0, 3.0}}});
    geometry.lines.push_back({{{2.0, 0.0}, {3.0, 5.0}, {2.5, 10.0}}, 0.5, 3});
    MeshOptions options;
    options.maximum_area = 0.3;
    options.minimum_angle = 28.0;
    options.conforming_delaunay = true;
    return generate_mesh(geometry, options);
}

// A mesh of two triangles that share the edge from (0, 0) to (4, 0), with
// the angle opposite it in the upper one at the apex.
Mesh two_triangles(double apex)
{
    Mesh mesh;
    mesh.vertices = {{0.0, 0.0}, {4.0, 0.0}, {2.0, apex}, {2.0, -5.0}};
    mesh.triangles = {{0, 1, 2}, {1, 0, 3}};
    return mesh;
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
    return std::abs(sum);
}

// The connection from cell n to cell m, or -1.
int find(const Connections & connections, int n, int m)
{
    for (int k = connections.offsets[n]; k < connections.offsets[n + 1]; k++)
        if (connections.cells[k] == m) return k;
    return -1;
}

std::string reason(const Grid & grid)
{
    try
    {
        connect_cells(grid);
    }
    catch (const Error & error)
    {
        return error.what();
    }
    return "";
}

// Two unit squares side by side, clockwise, sharing the edge at x = 1, which
// both split at (1, 0.5).
Grid split_face_grid()
{
    Grid grid;
    grid.vertices = {{0.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}, {1.0, 0.5},
                     {1.0, 0.0}, {2.0, 1.0}, {2.0, 0.0}};
    grid.cells = {{0, 1, 2, 3, 4}, {4, 3, 2, 5, 6}};
    grid.centers = {{0.5, 0.5}, {1.5, 0.5}};
    grid.sources = {-1, -1};
    return grid;
}

} // namespace

TEST(Connections, StructuredGridHasTheCellSizes)
{
    const Grid grid = voronoi_grid(lattice_mesh());
    ASSERT_EQ(grid.cells.size(), static_cast<std::size_t>(COLUMNS * ROWS));
    const Connections connections = connect_cells(grid);
    const double top = (ROWS - 1) * DY, right = (COLUMNS - 1) * DX;
    std::size_t count = 0;
    for (std::size_t n = 0; n < grid.cells.size(); n++)
    {
        const Point & c = grid.centers[n];
        int neighbors = 0;
        for (int k = connections.offsets[n]; k < connections.offsets[n + 1]; k++)
        {
            const Point & d = grid.centers[connections.cells[k]];
            const double dx = d.x - c.x, dy = d.y - c.y;
            neighbors++;
            if (std::abs(dy) < TOLERANCE)
            {
                // the face spans the cell in y, which is half as wide on the
                // edge of the grid
                const double width = std::min(c.y + 0.5 * DY, top) - std::max(c.y - 0.5 * DY, 0.0);
                EXPECT_NEAR(std::abs(dx), DX, TOLERANCE);
                EXPECT_NEAR(connections.distances[k], 0.5 * DX, TOLERANCE);
                EXPECT_NEAR(connections.widths[k], width, TOLERANCE);
                EXPECT_EQ(connections.angles[k], dx > 0.0 ? 0.0 : 180.0);
            }
            else
            {
                const double width =
                    std::min(c.x + 0.5 * DX, right) - std::max(c.x - 0.5 * DX, 0.0);
                EXPECT_NEAR(std::abs(dx), 0.0, TOLERANCE);
                EXPECT_NEAR(std::abs(dy), DY, TOLERANCE);
                EXPECT_NEAR(connections.distances[k], 0.5 * DY, TOLERANCE);
                EXPECT_NEAR(connections.widths[k], width, TOLERANCE);
                EXPECT_NEAR(connections.angles[k], dy > 0.0 ? 90.0 : 270.0, TOLERANCE);
            }
        }
        // a cell has a neighbor on each side within the lattice
        const int expected = (c.x > TOLERANCE) + (c.x < right - TOLERANCE) + (c.y > TOLERANCE) +
                             (c.y < top - TOLERANCE);
        EXPECT_EQ(neighbors, expected) << "cell " << n;
        count += neighbors;
    }
    // each of the lattice's interior edges, from both of its cells
    EXPECT_EQ(count, static_cast<std::size_t>(2 * ((COLUMNS - 1) * ROWS + COLUMNS * (ROWS - 1))));
}

TEST(Connections, AreSymmetricAndNormalToTheirFaces)
{
    const Mesh mesh = valley_mesh();
    for (const bool voronoi : {true, false})
    {
        const Grid grid =
            voronoi ? voronoi_grid(mesh) : triangle_grid(mesh, TriangleCenter::CENTROID);
        const Connections connections = connect_cells(grid);
        ASSERT_EQ(connections.offsets.size(), grid.cells.size() + 1);
        ASSERT_EQ(connections.offsets.back(), static_cast<int>(connections.cells.size()));
        for (std::size_t n = 0; n < grid.cells.size(); n++)
        {
            const int cell = static_cast<int>(n);
            EXPECT_TRUE(std::is_sorted(connections.cells.begin() + connections.offsets[n],
                                       connections.cells.begin() + connections.offsets[n + 1]));
            for (int k = connections.offsets[n]; k < connections.offsets[n + 1]; k++)
            {
                const int m = connections.cells[k];
                const int back = find(connections, m, cell);
                ASSERT_GE(back, 0) << "cell " << m << " to cell " << n;
                EXPECT_NEAR(connections.widths[back], connections.widths[k], TOLERANCE);
                EXPECT_NEAR(std::fmod(connections.angles[k] + 180.0, 360.0),
                            connections.angles[back], 1e-7);
                EXPECT_GE(connections.angles[k], 0.0);
                EXPECT_LT(connections.angles[k], 360.0);
                EXPECT_GT(connections.distances[k], 0.0);
                const Point & c = grid.centers[n];
                const Point & d = grid.centers[m];
                const double gap = std::hypot(d.x - c.x, d.y - c.y);
                if (voronoi)
                {
                    // the face bisects the line between the centers, along
                    // the normal
                    EXPECT_NEAR(connections.distances[k], 0.5 * gap, TOLERANCE);
                    const double angle = connections.angles[k] * std::numbers::pi / 180.0;
                    EXPECT_NEAR(std::cos(angle) * (d.x - c.x) + std::sin(angle) * (d.y - c.y), gap,
                                TOLERANCE);
                }
                else
                {
                    EXPECT_LE(connections.distances[k] + connections.distances[back],
                              gap + TOLERANCE);
                }
            }
        }
    }
}

TEST(Connections, InteriorCellAreasFollowFromThem)
{
    // a cell with no side on the grid's edge is the triangles between its
    // center and each face
    const Mesh mesh = valley_mesh();
    for (const bool voronoi : {true, false})
    {
        const Grid grid =
            voronoi ? voronoi_grid(mesh) : triangle_grid(mesh, TriangleCenter::CENTROID);
        const Connections connections = connect_cells(grid);
        int interior = 0;
        for (std::size_t n = 0; n < grid.cells.size(); n++)
        {
            const int count = connections.offsets[n + 1] - connections.offsets[n];
            if (count != static_cast<int>(grid.cells[n].size())) continue;
            interior++;
            double twice = 0.0;
            for (int k = connections.offsets[n]; k < connections.offsets[n + 1]; k++)
                twice += connections.widths[k] * connections.distances[k];
            EXPECT_NEAR(twice, twice_area(grid, n), TOLERANCE) << "cell " << n;
        }
        EXPECT_GT(interior, 100);
    }
}

TEST(Connections, FaceSplitInTwoIsOneConnection)
{
    Grid grid = split_face_grid();
    for (int turn = 0; turn < 2; turn++)
    {
        const Connections connections = connect_cells(grid);
        ASSERT_EQ(connections.offsets, (std::vector<int>{0, 1, 2}));
        EXPECT_EQ(connections.cells, (std::vector<int>{1, 0}));
        EXPECT_NEAR(connections.distances[0], 0.5, TOLERANCE);
        EXPECT_NEAR(connections.distances[1], 0.5, TOLERANCE);
        EXPECT_NEAR(connections.widths[0], 1.0, TOLERANCE);
        EXPECT_EQ(connections.angles[0], 0.0);
        EXPECT_EQ(connections.angles[1], 180.0);
        // the same counterclockwise
        for (auto & cell : grid.cells) std::reverse(cell.begin(), cell.end());
    }
}

TEST(Connections, CircumcenterOutsideItsTriangleIsAnError)
{
    // the upper triangle's angle at its apex is 120 degrees, and the two
    // opposite angles sum to under 180, so the mesh is Delaunay
    const Mesh obtuse = two_triangles(2.0 / std::sqrt(3.0));
    const std::string message = reason(triangle_grid(obtuse, TriangleCenter::CIRCUMCENTER));
    EXPECT_EQ(message.rfind("the center of cell 0 is not on its side of the face it shares with "
                            "cell 1, so the distance from the center to the face is -",
                            0),
              0u)
        << message;
    EXPECT_EQ(reason(triangle_grid(obtuse, TriangleCenter::CENTROID)), "");
    EXPECT_EQ(reason(voronoi_grid(obtuse)), "");
    // a right angle puts the circumcenter on the face
    const Mesh right = two_triangles(2.0);
    EXPECT_NE(reason(triangle_grid(right, TriangleCenter::CIRCUMCENTER)), "");
    EXPECT_EQ(reason(triangle_grid(two_triangles(3.0), TriangleCenter::CIRCUMCENTER)), "");
}

TEST(Connections, ErrorsGiveTheReason)
{
    Grid three = split_face_grid();
    three.vertices.push_back({1.5, -1.0});
    three.cells.push_back({4, 6, 7, 3});
    three.centers.push_back({1.2, 0.0});
    three.sources.push_back(-1);
    // cell 2 lies over the others, so the edge from vertex 3 to vertex 4 is
    // a side of three cells
    EXPECT_EQ(reason(three), "the edge from vertex 3 to vertex 4 is a side of more than two cells");

    // cell 1 covers the right half of cell 0
    Grid overlap = split_face_grid();
    overlap.vertices.push_back({0.5, 0.0});
    overlap.vertices.push_back({0.5, 1.0});
    overlap.cells[1] = {4, 7, 8, 2, 3};
    overlap.centers[1] = {0.75, 0.5};
    EXPECT_EQ(reason(overlap), "cells 0 and 1 overlap along the edge from vertex 2 to vertex 3");

    Grid twice = split_face_grid();
    twice.cells.pop_back();
    twice.centers.pop_back();
    twice.sources.pop_back();
    twice.cells[0] = {0, 1, 2, 1, 4};
    twice.centers[0] = {0.3, 0.6};
    EXPECT_EQ(reason(twice), "the edge from vertex 1 to vertex 2 is a side of cell 0 twice");

    Grid empty = split_face_grid();
    empty.centers.pop_back();
    EXPECT_EQ(reason(empty), "the grid has 2 cells but 1 centers");
}
