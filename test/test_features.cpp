// Tests mapping lines and marked segments to cells: the lengths sum to the
// line's or segments' length, and the cells are in order along the line.

#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "error.hpp"
#include "features.hpp"
#include "geometry.hpp"
#include "grid.hpp"

using namespace triangle_utils;

namespace
{

constexpr double TOLERANCE = 1e-9;
constexpr int BOUNDARY = 1;
constexpr int RIVER = 3;
const std::vector<Point> RIVER_LINE = {{2.0, 0.0}, {3.0, 5.0}, {2.5, 10.0}};

// A square crossed by a river, meshed with conforming Delaunay triangles.
Mesh square_mesh()
{
    Geometry geometry;
    geometry.polygons.push_back(
        {{{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}}, 0.0, BOUNDARY});
    geometry.lines.push_back({RIVER_LINE, 0.5, RIVER});
    MeshOptions options;
    options.maximum_area = 0.3;
    options.minimum_angle = 28.0;
    options.conforming_delaunay = true;
    return generate_mesh(geometry, options);
}

double line_length(const std::vector<Point> & line)
{
    double length = 0.0;
    for (std::size_t i = 0; i + 1 < line.size(); i++)
        length += std::hypot(line[i + 1].x - line[i].x, line[i + 1].y - line[i].y);
    return length;
}

// The point a distance along a line.
Point along(const std::vector<Point> & line, double distance)
{
    for (std::size_t i = 0; i + 1 < line.size(); i++)
    {
        const double length = std::hypot(line[i + 1].x - line[i].x, line[i + 1].y - line[i].y);
        if (distance <= length || i + 2 == line.size())
        {
            const double f = distance / length;
            return {line[i].x + f * (line[i + 1].x - line[i].x),
                    line[i].y + f * (line[i + 1].y - line[i].y)};
        }
        distance -= length;
    }
    return line.back();
}

// Whether a polygon contains a point, or has it on a face.
bool touches(const Grid & grid, int cell, const Point & point)
{
    const auto & corners = grid.cells[cell];
    bool inside = false;
    for (std::size_t i = 0, j = corners.size() - 1; i < corners.size(); j = i++)
    {
        const Point & a = grid.vertices[corners[i]];
        const Point & b = grid.vertices[corners[j]];
        const double cross = (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
        const double dot = (point.x - a.x) * (point.x - b.x) + (point.y - a.y) * (point.y - b.y);
        if (std::abs(cross) <= TOLERANCE * std::hypot(b.x - a.x, b.y - a.y) && dot <= 0.0)
            return true;
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y))
            inside = !inside;
    }
    return inside;
}

// Check that the parts follow the line in order, without gaps, from its
// start, sum to its length, and lie in their cells.
void check_along(const Grid & grid, const std::vector<Point> & line,
                 const std::vector<CellLength> & parts)
{
    ASSERT_FALSE(parts.empty());
    EXPECT_NEAR(parts.front().start, 0.0, TOLERANCE);
    double sum = 0.0;
    for (std::size_t i = 0; i < parts.size(); i++)
    {
        EXPECT_GT(parts[i].length, 0.0);
        sum += parts[i].length;
        if (i > 0)
        {
            EXPECT_NEAR(parts[i].start, parts[i - 1].start + parts[i - 1].length, TOLERANCE);
            EXPECT_NE(parts[i].cell, parts[i - 1].cell) << "part " << i;
        }
        const Point middle = along(line, parts[i].start + 0.5 * parts[i].length);
        EXPECT_TRUE(touches(grid, parts[i].cell, middle)) << "part " << i;
    }
    EXPECT_NEAR(sum, line_length(line), TOLERANCE * line_length(line));
}

// A grid of n by n unit squares, numbered by row from the bottom, with
// corners clockwise.
Grid squares(int n)
{
    Grid grid;
    for (int row = 0; row <= n; row++)
        for (int column = 0; column <= n; column++)
            grid.vertices.push_back({static_cast<double>(column), static_cast<double>(row)});
    for (int row = 0; row < n; row++)
        for (int column = 0; column < n; column++)
        {
            const int a = row * (n + 1) + column;
            grid.cells.push_back({a, a + n + 1, a + n + 2, a + 1});
            grid.centers.push_back({column + 0.5, row + 0.5});
        }
    return grid;
}

std::vector<int> cells_of(const std::vector<CellLength> & parts)
{
    std::vector<int> cells;
    for (const CellLength & part : parts) cells.push_back(part.cell);
    return cells;
}

} // namespace

TEST(Features, LineThroughCornersCrossesTheDiagonalCells)
{
    const auto parts = cells_along_line(squares(3), {{0.0, 0.0}, {3.0, 3.0}});
    EXPECT_EQ(cells_of(parts), (std::vector<int>{0, 4, 8}));
    for (const CellLength & part : parts) EXPECT_NEAR(part.length, std::sqrt(2.0), TOLERANCE);
}

TEST(Features, LineAlongFacesIsInTheCellsOnItsLeft)
{
    const Grid grid = squares(3);
    // up x = 1, the left is x < 1; down it, x > 1
    EXPECT_EQ(cells_of(cells_along_line(grid, {{1.0, 0.0}, {1.0, 3.0}})),
              (std::vector<int>{0, 3, 6}));
    EXPECT_EQ(cells_of(cells_along_line(grid, {{1.0, 3.0}, {1.0, 0.0}})),
              (std::vector<int>{7, 4, 1}));
    // along the boundary, in the only cells there, whichever the direction
    EXPECT_EQ(cells_of(cells_along_line(grid, {{0.0, 0.0}, {3.0, 0.0}})),
              (std::vector<int>{0, 1, 2}));
    EXPECT_EQ(cells_of(cells_along_line(grid, {{3.0, 0.0}, {0.0, 0.0}})),
              (std::vector<int>{2, 1, 0}));
}

TEST(Features, LineThatLeavesAndReturnsEntersTheCellTwice)
{
    const Grid grid = squares(1);
    const std::vector<Point> line = {{0.5, 0.2}, {2.0, 0.2}, {2.0, 0.8}, {0.5, 0.8}};
    const auto parts = cells_along_line(grid, line);
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(cells_of(parts), (std::vector<int>{0, 0}));
    EXPECT_NEAR(parts[0].length, 0.5, TOLERANCE);
    EXPECT_NEAR(parts[1].start, 1.5 + 0.6 + 1.0, TOLERANCE);
    EXPECT_NEAR(parts[1].length, 0.5, TOLERANCE);
}

TEST(Features, LineCrossesTheNotchOfACellThatIsNotConvex)
{
    // an L, clockwise, whose notch is the square from (1, 1) to (2, 2)
    Grid grid;
    grid.vertices = {{0.0, 0.0}, {0.0, 2.0}, {1.0, 2.0}, {1.0, 1.0}, {2.0, 1.0}, {2.0, 0.0}};
    grid.cells = {{0, 1, 2, 3, 4, 5}};
    grid.centers = {{0.5, 0.5}};
    double sum = 0.0;
    for (const CellLength & part : cells_along_line(grid, {{-1.0, 1.5}, {3.0, 1.5}}))
        sum += part.length;
    EXPECT_NEAR(sum, 1.0, TOLERANCE);
    const auto low = cells_along_line(grid, {{-1.0, 0.5}, {3.0, 0.5}});
    ASSERT_EQ(low.size(), 1u);
    EXPECT_NEAR(low[0].length, 2.0, TOLERANCE);
}

TEST(Features, DenselyDigitizedLineHasOnePartPerCell)
{
    // two cells 100 km wide, and a line of 200,001 points 0.7 m apart across
    // both, whose distances along it round by more than a part's tolerance
    Grid grid;
    grid.vertices = {{0.0, 0.0}, {0.0, 1e5}, {1e5, 1e5}, {1e5, 0.0}, {2e5, 1e5}, {2e5, 0.0}};
    grid.cells = {{0, 1, 2, 3}, {3, 2, 4, 5}};
    grid.centers = {{5e4, 5e4}, {1.5e5, 5e4}};
    std::vector<Point> line;
    for (int point = 0; point <= 200000; point++) line.push_back({0.7 + 0.7 * point, 5e4 + 0.1});
    const auto parts = cells_along_line(grid, line);
    EXPECT_EQ(cells_of(parts), (std::vector<int>{0, 1}));
    EXPECT_NEAR(parts[0].length + parts[1].length, 1.4e5, 1e-6);
}

TEST(Features, RiverRunsThroughTheVoronoiCellsOfItsVertices)
{
    const Mesh mesh = square_mesh();
    const Grid grid = voronoi_grid(mesh);
    const auto parts = cells_along_line(grid, RIVER_LINE);
    check_along(grid, RIVER_LINE, parts);
    // the river's vertices are the centers of the cells it crosses
    std::set<int> river_vertices;
    for (std::size_t segment = 0; segment < mesh.segments.size(); segment++)
        if (mesh.segment_markers[segment] == RIVER)
            river_vertices.insert(mesh.segments[segment].begin(), mesh.segments[segment].end());
    std::set<int> crossed;
    for (const CellLength & part : parts) crossed.insert(grid.sources[part.cell]);
    EXPECT_EQ(crossed, river_vertices);
    // in order: each part holds its cell's center, which is on the river
    for (const CellLength & part : parts)
    {
        const Point & center = grid.centers[part.cell];
        double nearest = HUGE_VAL, at = 0.0;
        for (double d = part.start; d <= part.start + part.length; d += part.length / 64.0)
        {
            const Point point = along(RIVER_LINE, d);
            const double gap = std::hypot(point.x - center.x, point.y - center.y);
            if (gap < nearest) nearest = gap, at = d;
        }
        EXPECT_LT(nearest, 0.02 * part.length) << "the center of part at " << at;
    }
}

TEST(Features, RiverAlongTriangleEdgesIsInTheCellsOnItsLeft)
{
    const Grid grid = triangle_grid(square_mesh(), TriangleCenter::CENTROID);
    const auto parts = cells_along_line(grid, RIVER_LINE);
    check_along(grid, RIVER_LINE, parts);
    for (const CellLength & part : parts)
    {
        // the cell's center is left of the line where the part is
        const Point a = along(RIVER_LINE, part.start);
        const Point b = along(RIVER_LINE, part.start + part.length);
        const Point & c = grid.centers[part.cell];
        EXPECT_GT((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x), 0.0);
    }
}

TEST(Features, LineAcrossTheGridEndsAtItsEdges)
{
    const Grid grid = voronoi_grid(square_mesh());
    // a line from outside to outside, which crosses 10 of the square
    const std::vector<Point> line = {{-2.0, 4.3}, {12.0, 4.3}};
    const auto parts = cells_along_line(grid, line);
    ASSERT_FALSE(parts.empty());
    EXPECT_NEAR(parts.front().start, 2.0, TOLERANCE);
    double sum = 0.0;
    for (const CellLength & part : parts) sum += part.length;
    EXPECT_NEAR(sum, 10.0, TOLERANCE);
}

TEST(Features, BoundaryCellsShareTheBoundaryLength)
{
    const Mesh mesh = square_mesh();
    for (const bool voronoi : {true, false})
    {
        const Grid grid =
            voronoi ? voronoi_grid(mesh) : triangle_grid(mesh, TriangleCenter::CENTROID);
        const auto cells = cells_on_segments(grid, mesh, BOUNDARY);
        double sum = 0.0;
        std::set<int> seen;
        for (const CellLength & cell : cells)
        {
            EXPECT_TRUE(seen.insert(cell.cell).second) << "cell " << cell.cell << " twice";
            EXPECT_GT(cell.length, 0.0);
            sum += cell.length;
        }
        EXPECT_NEAR(sum, 40.0, TOLERANCE);
        if (!voronoi) continue;
        // a boundary vertex's cell has half of each boundary segment at it
        for (const CellLength & cell : cells)
        {
            const int vertex = grid.sources[cell.cell];
            double half = 0.0;
            for (std::size_t segment = 0; segment < mesh.segments.size(); segment++)
            {
                const auto & ends = mesh.segments[segment];
                if (mesh.segment_markers[segment] != BOUNDARY ||
                    (ends[0] != vertex && ends[1] != vertex))
                    continue;
                const Point & p = mesh.vertices[ends[0]];
                const Point & q = mesh.vertices[ends[1]];
                half += 0.5 * std::hypot(q.x - p.x, q.y - p.y);
            }
            EXPECT_NEAR(cell.length, half, TOLERANCE);
        }
    }
}

TEST(Features, ErrorsGiveTheReason)
{
    const Mesh mesh = square_mesh();
    const Grid grid = voronoi_grid(mesh);
    auto reason = [](const auto & call)
    {
        try
        {
            call();
        }
        catch (const Error & error)
        {
            return std::string(error.what());
        }
        return std::string();
    };
    EXPECT_EQ(reason([&] { cells_along_line(grid, {{1.0, 1.0}}); }),
              "the line has 1 points, but it needs at least 2");
    EXPECT_EQ(reason([&] { cells_along_line(grid, {{1.0, 1.0}, {NAN, 2.0}}); }),
              "point 1 of the line is not finite");
    EXPECT_EQ(reason([&] { cells_on_segments(grid, mesh, 9); }),
              "no segment of the mesh has marker 9");
}
