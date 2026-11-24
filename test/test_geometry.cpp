// Tests meshing geometry: the mesh covers the domain, follows every boundary
// and line, carries region attributes and limits, and merges repeated points.

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "error.hpp"
#include "geometry.hpp"

using namespace triangle_utils;

namespace
{

constexpr double TOLERANCE = 1e-9;

Chain square(double low, double high, double spacing = 0.0, int marker = 0)
{
    return {{{low, low}, {high, low}, {high, high}, {low, high}}, spacing, marker};
}

Point centroid(const Mesh & mesh, std::size_t triangle)
{
    const auto & corners = mesh.triangles[triangle];
    Point result;
    for (const int corner : corners)
    {
        result.x += mesh.vertices[corner].x / 3.0;
        result.y += mesh.vertices[corner].y / 3.0;
    }
    return result;
}

double smallest_angle(const Mesh & mesh, std::size_t triangle)
{
    double smallest = 180.0;
    for (int corner = 0; corner < 3; corner++)
    {
        const Point & a = mesh.vertices[mesh.triangles[triangle][corner]];
        const Point & b = mesh.vertices[mesh.triangles[triangle][(corner + 1) % 3]];
        const Point & c = mesh.vertices[mesh.triangles[triangle][(corner + 2) % 3]];
        const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - a.x, vy = c.y - a.y;
        smallest = std::min(smallest, std::atan2(std::abs(ux * vy - uy * vx), ux * vx + uy * vy) *
                                          180.0 / 3.14159265358979323846);
    }
    return smallest;
}

// Whether a point lies on the segment from start to end.
bool on_segment(const Point & point, const Point & start, const Point & end)
{
    const double dx = end.x - start.x, dy = end.y - start.y;
    const double length = std::hypot(dx, dy);
    const double cross = (point.x - start.x) * dy - (point.y - start.y) * dx;
    const double along = ((point.x - start.x) * dx + (point.y - start.y) * dy) / length;
    return std::abs(cross) / length < TOLERANCE && along > -TOLERANCE && along < length + TOLERANCE;
}

// The length of the mesh's segments that lie on the line from start to end.
double length_on(const Mesh & mesh, const Point & start, const Point & end)
{
    double length = 0.0;
    for (const auto & segment : mesh.segments)
    {
        const Point & first = mesh.vertices[segment[0]];
        const Point & second = mesh.vertices[segment[1]];
        if (on_segment(first, start, end) && on_segment(second, start, end))
            length += std::hypot(second.x - first.x, second.y - first.y);
    }
    return length;
}

// Check the properties every generated mesh has, including that each segment
// is an edge of a triangle.
void check_mesh(const Mesh & mesh, double area)
{
    check_sizes(mesh);
    check_triangles(mesh);
    check_neighbors(mesh);
    std::set<std::pair<int, int>> edges;
    for (const auto & corners : mesh.triangles)
    {
        for (int corner = 0; corner < 3; corner++)
            edges.insert(std::minmax(corners[corner], corners[(corner + 1) % 3]));
    }
    for (const auto & segment : mesh.segments)
        EXPECT_TRUE(edges.contains(std::minmax(segment[0], segment[1])));
    // every vertex is a corner of a triangle
    std::set<int> used;
    for (const auto & corners : mesh.triangles) used.insert(corners.begin(), corners.end());
    EXPECT_EQ(used.size(), mesh.vertices.size());
    EXPECT_NEAR(total_area(mesh), area, TOLERANCE * area);
}

template <typename Action> std::string error_message(Action && action)
{
    try
    {
        action();
    }
    catch (const Error & error)
    {
        return error.what();
    }
    return "";
}

} // namespace

TEST(Geometry, SquareWithHoleMeetsAreaAndAngle)
{
    Geometry geometry;
    geometry.polygons.push_back(square(0.0, 10.0));
    geometry.hole_polygons.push_back(square(4.0, 6.0));
    MeshOptions options;
    options.maximum_area = 1.0;
    options.minimum_angle = 25.0;
    const Mesh mesh = generate_mesh(geometry, options);

    check_mesh(mesh, 96.0);
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        EXPECT_LE(signed_area(mesh, triangle), 1.0 + TOLERANCE);
        EXPECT_GE(smallest_angle(mesh, triangle), 25.0 - TOLERANCE);
        const Point center = centroid(mesh, triangle);
        EXPECT_FALSE(center.x > 4.0 && center.x < 6.0 && center.y > 4.0 && center.y < 6.0);
    }
    // the outer boundary and the hole's are followed
    EXPECT_NEAR(length_on(mesh, {0.0, 0.0}, {10.0, 0.0}), 10.0, TOLERANCE);
    EXPECT_NEAR(length_on(mesh, {4.0, 6.0}, {4.0, 4.0}), 2.0, TOLERANCE);
}

TEST(Geometry, HolePointsAreNotTiedToPolygons)
{
    // the hole's point is given before the polygon that bounds it
    Geometry geometry;
    geometry.holes.push_back({5.0, 5.0});
    geometry.polygons.push_back(square(0.0, 10.0));
    geometry.polygons.push_back(square(4.0, 6.0));
    check_mesh(generate_mesh(geometry, {}), 96.0);
}

TEST(Geometry, HolePolygonsRemoveEverythingInside)
{
    MeshOptions options;
    options.maximum_area = 0.5;
    // a line, an island polygon, and a ring inside hole polygons, and two
    // hole polygons that overlap: the union of the holes is removed
    Geometry geometry;
    geometry.polygons.push_back(square(0.0, 10.0));
    geometry.hole_polygons.push_back(square(1.0, 4.0));
    geometry.hole_polygons.push_back(square(3.0, 5.0));
    geometry.lines.push_back({{{1.5, 2.0}, {3.5, 2.0}}});
    geometry.polygons.push_back(square(2.0, 3.0));
    geometry.hole_polygons.push_back(square(6.0, 9.0));
    geometry.rings.push_back({{7.5, 7.5}, 0.5, 8, 1.0, 0.01, 0});
    const Mesh mesh = generate_mesh(geometry, options);
    // 100, less the union of the first two holes (9 + 4 - 1) and the third (9)
    check_mesh(mesh, 100.0 - 12.0 - 9.0);
}

TEST(Geometry, SmallAreaLimitsAreMet)
{
    // a limit Triangle would read wrongly in scientific notation
    Geometry geometry;
    geometry.polygons.push_back(square(0.0, 0.01));
    MeshOptions options;
    options.maximum_area = 1e-7;
    const Mesh mesh = generate_mesh(geometry, options);
    check_mesh(mesh, 1e-4);
    EXPECT_GE(mesh.triangles.size(), 1000u);
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
        EXPECT_LE(signed_area(mesh, triangle), 1e-7 * (1.0 + TOLERANCE));
}

TEST(Geometry, RingsAcrossTheBoundaryAddNoArea)
{
    Geometry geometry;
    geometry.polygons.push_back(square(0.0, 10.0));
    geometry.rings.push_back({{10.0, 5.0}, 2.0, 16, 1.0, 0.05, 0});
    check_mesh(generate_mesh(geometry, {}), 100.0);
}

TEST(Geometry, LinesOutsideThePolygonsAddNoArea)
{
    // a river that leaves the square and returns, enclosing area outside it
    Geometry geometry;
    geometry.polygons.push_back(square(0.0, 10.0));
    geometry.lines.push_back({{{5.0, 5.0}, {5.0, 15.0}, {8.0, 15.0}, {8.0, 8.0}}});
    const Mesh mesh = generate_mesh(geometry, {});
    check_mesh(mesh, 100.0);
    EXPECT_NEAR(length_on(mesh, {5.0, 5.0}, {5.0, 15.0}), 5.0, TOLERANCE);
}

TEST(Geometry, OnlyUsedVerticesAndBoundaryMarkers)
{
    // a point outside the polygon, and a hole polygon at a corner
    Geometry geometry;
    geometry.polygons.push_back(square(0.0, 10.0));
    geometry.points.push_back({20.0, 20.0});
    geometry.hole_polygons.push_back(square(0.0, 4.0));
    check_mesh(generate_mesh(geometry, {}), 84.0);

    // after triangles outside are removed, the new boundary segments and
    // vertices get marker 1, as Triangle gives the rest
    geometry = {};
    geometry.polygons.push_back(square(0.0, 10.0));
    geometry.lines.push_back({{{5.0, 5.0}, {5.0, 15.0}, {8.0, 15.0}, {8.0, 8.0}}, 0.0, 3});
    const Mesh mesh = generate_mesh(geometry, {});
    check_mesh(mesh, 100.0);
    for (std::size_t segment = 0; segment < mesh.segments.size(); segment++)
    {
        const Point & first = mesh.vertices[mesh.segments[segment][0]];
        const Point & second = mesh.vertices[mesh.segments[segment][1]];
        if (first.y == 10.0 && second.y == 10.0) EXPECT_EQ(mesh.segment_markers[segment], 1);
    }
    for (std::size_t vertex = 0; vertex < mesh.vertices.size(); vertex++)
    {
        // where the river crosses, a vertex has the river's marker
        if (mesh.vertices[vertex].y == 10.0) EXPECT_NE(mesh.vertex_markers[vertex], 0);
    }
}

TEST(Geometry, ALineEndingOnAnEdgeSplitsIt)
{
    // a river that starts on a sloped boundary edge between two of its
    // points, which made Triangle fail or never finish (#42)
    Geometry geometry;
    geometry.polygons.push_back({{{0.0, 0.0},
                                  {4000.0, 0.0},
                                  {4600.0, 2200.0},
                                  {4200.0, 6000.0},
                                  {2600.0, 8000.0},
                                  {600.0, 7600.0},
                                  {0.0, 4000.0}},
                                 200.0,
                                 1});
    geometry.lines.push_back({{{1800.0, 7840.0}, {2300.0, 5200.0}, {2400.0, 0.0}}, 50.0, 2});
    geometry.regions.push_back({{500.0, 1000.0}, 1.0, 0.0});
    geometry.regions.push_back({{4000.0, 1000.0}, 1.0, 0.0});
    MeshOptions options;
    options.minimum_angle = 30.0;
    options.maximum_area = 40000.0;
    const Mesh mesh = generate_mesh(geometry, options);

    // the polygon's area, by the shoelace formula
    const auto & points = geometry.polygons[0].points;
    double area = 0.0;
    for (std::size_t point = 0; point < points.size(); point++)
    {
        const Point & next = points[(point + 1) % points.size()];
        area += 0.5 * (points[point].x * next.y - next.x * points[point].y);
    }
    check_mesh(mesh, area);
    // the river starts at a vertex of the mesh, and the boundary is followed
    // on both sides of it
    const auto start = std::find_if(mesh.vertices.begin(), mesh.vertices.end(),
                                    [](const Point & p) { return p.x == 1800.0 && p.y == 7840.0; });
    EXPECT_NE(start, mesh.vertices.end());
    EXPECT_NEAR(length_on(mesh, {600.0, 7600.0}, {2600.0, 8000.0}), std::hypot(2000.0, 400.0),
                1e-6);
    EXPECT_NEAR(length_on(mesh, {1800.0, 7840.0}, {2300.0, 5200.0}), std::hypot(500.0, 2640.0),
                1e-6);
}

TEST(Geometry, PointsARoundingErrorOffAreMergedOrSplit)
{
    // the end of a river a rounding error from a densified point of the
    // boundary, and a point a rounding error off a sloped edge; each made
    // Triangle crash or never finish
    const Chain triangle_polygon{{{0.0, 0.0}, {10.0, 0.0}, {10.0, 7.0}}, 4.2, 1};
    const Point densified = densify(triangle_polygon, true)[5];
    MeshOptions options;
    options.minimum_angle = 30.0;
    options.maximum_area = 0.5;
    for (const int steps : {-3, -1, 1, 2})
    {
        Point end = densified;
        for (int step = 0; step < std::abs(steps); step++)
            end.x = std::nextafter(end.x, steps > 0 ? 20.0 : -20.0);
        Geometry geometry;
        geometry.polygons.push_back(triangle_polygon);
        geometry.lines.push_back({{end, {8.0, 3.0}}, 0.0, 2});
        const Mesh mesh = generate_mesh(geometry, options);
        check_mesh(mesh, 35.0);
        // the boundary keeps its marker where it was split
        for (std::size_t segment = 0; segment < mesh.segments.size(); segment++)
        {
            const Point & first = mesh.vertices[mesh.segments[segment][0]];
            const Point & second = mesh.vertices[mesh.segments[segment][1]];
            if (first.y == 0.0 && second.y == 0.0) EXPECT_EQ(mesh.segment_markers[segment], 1);
        }
    }

    Geometry geometry;
    geometry.polygons.push_back({{{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}}});
    geometry.points.push_back({std::nextafter(10.0 / 3.0, 0.0), 10.0 / 3.0});
    const Mesh mesh = generate_mesh(geometry, options);
    check_mesh(mesh, 50.0);
    EXPECT_NEAR(length_on(mesh, {0.0, 0.0}, {10.0, 10.0}), std::hypot(10.0, 10.0), 1e-9);
}

TEST(Geometry, PointsOnEdgesInLargeCoordinates)
{
    // the river of the #42 valley, in coordinates like UTM northings
    Geometry geometry;
    const double x0 = 500000.0, y0 = 4500000.0;
    geometry.polygons.push_back({{{x0, y0},
                                  {x0 + 4000.0, y0},
                                  {x0 + 4600.0, y0 + 2200.0},
                                  {x0 + 2600.0, y0 + 8000.0},
                                  {x0 + 600.0, y0 + 7600.0}},
                                 200.0,
                                 1});
    geometry.lines.push_back({{{x0 + 1800.0, y0 + 7840.0}, {x0 + 2400.0, y0}}, 50.0, 2});
    geometry.regions.push_back({{x0 + 500.0, y0 + 1000.0}, 1.0, 0.0});
    geometry.regions.push_back({{x0 + 3500.0, y0 + 1000.0}, 1.0, 0.0});
    MeshOptions options;
    options.minimum_angle = 30.0;
    options.maximum_area = 40000.0;
    const Mesh mesh = generate_mesh(geometry, options);
    check_sizes(mesh);
    check_triangles(mesh);
    check_neighbors(mesh);
    EXPECT_NEAR(length_on(mesh, {x0 + 1800.0, y0 + 7840.0}, {x0 + 2400.0, y0}),
                std::hypot(600.0, 7840.0), 1e-3);
}

TEST(Geometry, RegionsCarryAttributesAndLimits)
{
    Geometry geometry;
    geometry.polygons.push_back(square(0.0, 10.0));
    geometry.polygons.push_back(square(2.0, 5.0));
    geometry.regions.push_back({{3.0, 3.0}, 7.0, 0.05});
    geometry.regions.push_back({{8.0, 8.0}, 3.0, 0.0});
    const Mesh mesh = generate_mesh(geometry, {});

    check_mesh(mesh, 100.0);
    ASSERT_EQ(mesh.triangle_attribute_count, 1);
    double inner_area = 0.0;
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        const Point center = centroid(mesh, triangle);
        const bool inner = center.x > 2.0 && center.x < 5.0 && center.y > 2.0 && center.y < 5.0;
        EXPECT_EQ(mesh.triangle_attributes[triangle], inner ? 7.0 : 3.0);
        if (inner)
        {
            EXPECT_LE(signed_area(mesh, triangle), 0.05 + TOLERANCE);
            inner_area += signed_area(mesh, triangle);
        }
    }
    EXPECT_NEAR(inner_area, 9.0, TOLERANCE);
}

TEST(Geometry, LinesAndPointsAreFollowed)
{
    Geometry geometry;
    geometry.polygons.push_back(square(0.0, 10.0, 0.0, 1));
    // a river that bends, densified to a spacing, and a point
    geometry.lines.push_back({{{1.0, 1.0}, {5.0, 3.0}, {9.0, 2.0}}, 0.5, 5});
    geometry.points.push_back({7.0, 7.0});
    const Mesh mesh = generate_mesh(geometry, {});

    check_mesh(mesh, 100.0);
    EXPECT_NEAR(length_on(mesh, {1.0, 1.0}, {5.0, 3.0}), std::hypot(4.0, 2.0), TOLERANCE);
    EXPECT_NEAR(length_on(mesh, {5.0, 3.0}, {9.0, 2.0}), std::hypot(4.0, 1.0), TOLERANCE);
    for (std::size_t segment = 0; segment < mesh.segments.size(); segment++)
    {
        const Point & first = mesh.vertices[mesh.segments[segment][0]];
        const Point & second = mesh.vertices[mesh.segments[segment][1]];
        const double length = std::hypot(second.x - first.x, second.y - first.y);
        if (on_segment(first, {1.0, 1.0}, {5.0, 3.0}) && on_segment(second, {1.0, 1.0}, {5.0, 3.0}))
        {
            EXPECT_LE(length, 0.5 + TOLERANCE);
            EXPECT_EQ(mesh.segment_markers[segment], 5);
        }
    }
    const auto point = std::find_if(mesh.vertices.begin(), mesh.vertices.end(),
                                    [](const Point & p) { return p.x == 7.0 && p.y == 7.0; });
    EXPECT_NE(point, mesh.vertices.end());
}

TEST(Geometry, RingsRefineAroundAPoint)
{
    Geometry geometry;
    geometry.polygons.push_back(square(0.0, 10.0));
    geometry.rings.push_back({{5.0, 5.0}, 1.0, 12, 2.0, 0.01, 0});
    const Mesh mesh = generate_mesh(geometry, {});

    check_mesh(mesh, 100.0);
    const auto center = std::find_if(mesh.vertices.begin(), mesh.vertices.end(),
                                     [](const Point & p) { return p.x == 5.0 && p.y == 5.0; });
    EXPECT_NE(center, mesh.vertices.end());
    int ring_triangles = 0;
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        const Point c = centroid(mesh, triangle);
        if (std::hypot(c.x - 5.0, c.y - 5.0) < 0.9)
        {
            EXPECT_EQ(mesh.triangle_attributes[triangle], 2.0);
            EXPECT_LE(signed_area(mesh, triangle), 0.01 + TOLERANCE);
            ring_triangles++;
        }
    }
    EXPECT_GT(ring_triangles, 100);
    // outside the ring, triangles keep no attribute and no limit
    double largest_outside = 0.0;
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        const Point c = centroid(mesh, triangle);
        if (std::hypot(c.x - 5.0, c.y - 5.0) > 1.0)
        {
            EXPECT_EQ(mesh.triangle_attributes[triangle], 0.0);
            largest_outside = std::max(largest_outside, signed_area(mesh, triangle));
        }
    }
    EXPECT_GT(largest_outside, 1.0);

    // a ring whose points would merge is an error, not a region over all
    MeshOptions options;
    options.merge_distance = 0.01;
    geometry.rings[0].radius = 0.001;
    EXPECT_EQ(error_message([&] { generate_mesh(geometry, options); }),
              "ring 0's points are closer than the merge distance 0.01");
}

TEST(Geometry, RepeatedPointsAreMerged)
{
    // two squares sharing an edge, given with the shared points twice
    Geometry geometry;
    geometry.polygons.push_back({{{0.0, 0.0}, {5.0, 0.0}, {5.0, 5.0}, {0.0, 5.0}}});
    geometry.polygons.push_back({{{5.0, 0.0}, {10.0, 0.0}, {10.0, 5.0}, {5.0, 5.0}}});
    Mesh mesh = generate_mesh(geometry, {});
    check_mesh(mesh, 50.0);
    EXPECT_EQ(mesh.vertices.size(), 6u);

    // points a rounding error apart are merged; points farther apart only
    // within the merge distance
    geometry.polygons[1].points[0].x = std::nextafter(5.0, 6.0);
    EXPECT_EQ(generate_mesh(geometry, {}).vertices.size(), 6u);
    geometry.polygons[1].points[0].x = 5.0 + 1e-6;
    geometry.polygons[1].points[3].x = 5.0 - 1e-6;
    EXPECT_GT(generate_mesh(geometry, {}).vertices.size(), 6u);
    MeshOptions options;
    options.merge_distance = 1e-5;
    mesh = generate_mesh(geometry, options);
    check_mesh(mesh, 50.0);
    EXPECT_EQ(mesh.vertices.size(), 6u);

    // an edge and a line along it in the other direction, both densified,
    // share their points and segments
    Geometry shared;
    shared.polygons.push_back(square(0.0, 10.0, 0.7));
    shared.lines.push_back({{{10.0, 0.0}, {0.0, 0.0}}, 0.7});
    const Mesh with_line = generate_mesh(shared, {});
    shared.lines.clear();
    EXPECT_EQ(with_line.vertices.size(), generate_mesh(shared, {}).vertices.size());
}

TEST(Geometry, DensifyAndPointInside)
{
    const std::vector<Point> points = densify({{{0.0, 0.0}, {1.0, 0.0}}, 0.3}, false);
    ASSERT_EQ(points.size(), 5u);
    EXPECT_NEAR(points[1].x, 0.25, TOLERANCE);
    EXPECT_EQ(points.back().x, 1.0);
    EXPECT_EQ(densify(square(0.0, 1.0, 0.5), true).size(), 8u);
    // a spacing longer than the edge adds nothing, and either direction gives
    // the same points
    EXPECT_EQ(densify({{{0.0, 0.0}, {1.0, 0.0}}, 5.0}, false).size(), 2u);
    std::vector<Point> backward = densify({{{1.0, 0.3}, {0.0, 0.0}}, 0.3}, false);
    std::reverse(backward.begin(), backward.end());
    const std::vector<Point> forward = densify({{{0.0, 0.0}, {1.0, 0.3}}, 0.3}, false);
    ASSERT_EQ(backward.size(), forward.size());
    for (std::size_t point = 0; point < forward.size(); point++)
    {
        EXPECT_EQ(backward[point].x, forward[point].x);
        EXPECT_EQ(backward[point].y, forward[point].y);
    }

    // a U shape, whose centroid is outside it
    const std::vector<Point> u_shape = {{0.0, 0.0}, {3.0, 0.0}, {3.0, 3.0}, {2.0, 3.0},
                                        {2.0, 1.0}, {1.0, 1.0}, {1.0, 3.0}, {0.0, 3.0}};
    const Point inside = point_inside(u_shape);
    const bool in_base = inside.y > 0.0 && inside.y < 1.0 && inside.x > 0.0 && inside.x < 3.0;
    const bool in_arm = inside.y > 1.0 && inside.y < 3.0 &&
                        ((inside.x > 0.0 && inside.x < 1.0) || (inside.x > 2.0 && inside.x < 3.0));
    EXPECT_TRUE(in_base || in_arm);
}

TEST(Geometry, ErrorsGiveTheReason)
{
    Geometry geometry;
    geometry.polygons.push_back({{{0.0, 0.0}, {1.0, 0.0}}});
    EXPECT_EQ(error_message([&] { generate_mesh(geometry, {}); }),
              "polygon 0 has 2 points, but at least 3 are needed");

    MeshOptions options;
    options.maximum_area = -1.0;
    EXPECT_EQ(error_message([&] { generate_mesh({}, options); }),
              "the maximum area is -1, but it must be finite and at least 0");
    options.maximum_area = 0.0;
    options.minimum_angle = 40.0;
    EXPECT_EQ(error_message([&] { generate_mesh({}, options); }),
              "the minimum angle is 40, but it must be 0 to 34 degrees");

    // Triangle removes every triangle outside the segments, so a boundary that
    // is not closed leaves none
    geometry.polygons.clear();
    geometry.lines.push_back({{{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}}});
    EXPECT_EQ(error_message([&] { generate_mesh(geometry, {}); }),
              "the mesh has no triangles; close the domain's boundary with polygons or segments");
    geometry.lines.clear();

    // Triangle's own reason
    geometry.polygons.clear();
    geometry.points = {{0.0, 0.0}, {1.0, 0.0}};
    EXPECT_EQ(error_message([&] { generate_mesh(geometry, {}); }),
              "Triangle could not mesh the input: Input must have at least three input "
              "vertices.");
}
