// Tests meshing geometry: the mesh covers the domain, follows every boundary
// and line, carries region attributes and limits, and merges repeated points.

#include <algorithm>
#include <cmath>
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

// Check the properties every generated mesh has.
void check_mesh(const Mesh & mesh, double area)
{
    check_sizes(mesh);
    check_triangles(mesh);
    check_neighbors(mesh);
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

    // nearly the same points are merged within a distance, and only then
    geometry.polygons[1].points[0].x = 5.0 + 1e-9;
    geometry.polygons[1].points[3].x = 5.0 - 1e-9;
    MeshOptions options;
    options.merge_distance = 1e-6;
    mesh = generate_mesh(geometry, options);
    check_mesh(mesh, 50.0);
    EXPECT_EQ(mesh.vertices.size(), 6u);
}

TEST(Geometry, DensifyAndPointInside)
{
    const std::vector<Point> points = densify({{{0.0, 0.0}, {1.0, 0.0}}, 0.3}, false);
    ASSERT_EQ(points.size(), 5u);
    EXPECT_NEAR(points[1].x, 0.25, TOLERANCE);
    EXPECT_EQ(points.back().x, 1.0);
    EXPECT_EQ(densify(square(0.0, 1.0, 0.5), true).size(), 8u);

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
