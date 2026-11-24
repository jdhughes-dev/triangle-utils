// Tests refining a mesh: area limits are met, the area, boundary, segments,
// and attributes are kept, and the maximum angle is reached or reported.

#include <cmath>
#include <string>

#include <gtest/gtest.h>

#include "error.hpp"
#include "geometry.hpp"
#include "refine.hpp"

using namespace triangle_utils;

namespace
{

constexpr double TOLERANCE = 1e-9;

// The polygon of Triangle's library test, whose mesh has obtuse triangles;
// its area is 58.
Mesh polygon_mesh()
{
    Geometry geometry;
    geometry.polygons.push_back({{{0.0, 0.0},
                                  {10.0, 0.0},
                                  {10.0, 3.0},
                                  {7.0, 3.0},
                                  {6.0, 8.0},
                                  {2.0, 9.0},
                                  {1.0, 4.0},
                                  {0.0, 2.0}},
                                 0.0,
                                 4});
    MeshOptions options;
    options.minimum_angle = 30.0;
    options.maximum_area = 0.5;
    return generate_mesh(geometry, options);
}

// The length of the mesh's segments on the line from start to end.
double length_on(const Mesh & mesh, const Point & start, const Point & end)
{
    const double dx = end.x - start.x, dy = end.y - start.y;
    const double length = std::hypot(dx, dy);
    auto on = [&](const Point & point)
    {
        const double cross = (point.x - start.x) * dy - (point.y - start.y) * dx;
        const double along = ((point.x - start.x) * dx + (point.y - start.y) * dy) / length;
        return std::abs(cross) / length < TOLERANCE && along > -TOLERANCE &&
               along < length + TOLERANCE;
    };
    double total = 0.0;
    for (const auto & segment : mesh.segments)
    {
        const Point & first = mesh.vertices[segment[0]];
        const Point & second = mesh.vertices[segment[1]];
        if (on(first) && on(second)) total += std::hypot(second.x - first.x, second.y - first.y);
    }
    return total;
}

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

TEST(Refine, AreaLimitsKeepBoundarySegmentsAndAttributes)
{
    // a square with an inner polygon and a region attribute in each
    Geometry geometry;
    geometry.polygons.push_back({{{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}}, 0.0, 2});
    geometry.polygons.push_back({{{2.0, 2.0}, {5.0, 2.0}, {5.0, 5.0}, {2.0, 5.0}}, 0.0, 3});
    geometry.regions.push_back({{3.0, 3.0}, 7.0, 0.0});
    geometry.regions.push_back({{8.0, 8.0}, 1.0, 0.0});
    Mesh mesh = generate_mesh(geometry, {});

    // a limit for the triangles of the inner polygon only
    mesh.triangle_area_limits.assign(mesh.triangles.size(), 0.0);
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
        if (mesh.triangle_attributes[triangle] == 7.0) mesh.triangle_area_limits[triangle] = 0.02;
    const Mesh refined = refine_mesh(mesh, {});

    check_mesh(refined, 100.0);
    EXPECT_GT(refined.triangles.size(), mesh.triangles.size() + 300);
    double inner = 0.0;
    for (std::size_t triangle = 0; triangle < refined.triangles.size(); triangle++)
    {
        const double area = signed_area(refined, triangle);
        if (refined.triangle_attributes[triangle] == 7.0)
        {
            EXPECT_LE(area, 0.02 + TOLERANCE);
            inner += area;
        }
        else
        {
            EXPECT_EQ(refined.triangle_attributes[triangle], 1.0);
        }
    }
    EXPECT_NEAR(inner, 9.0, TOLERANCE);
    EXPECT_NEAR(length_on(refined, {0.0, 0.0}, {10.0, 0.0}), 10.0, TOLERANCE);
    EXPECT_NEAR(length_on(refined, {2.0, 2.0}, {5.0, 2.0}), 3.0, TOLERANCE);
    bool inner_marked = false;
    for (std::size_t segment = 0; segment < refined.segments.size(); segment++)
    {
        const Point & first = refined.vertices[refined.segments[segment][0]];
        const Point & second = refined.vertices[refined.segments[segment][1]];
        if (first.y == 2.0 && second.y == 2.0 && first.x >= 2.0 && second.x <= 5.0)
        {
            EXPECT_EQ(refined.segment_markers[segment], 3);
            inner_marked = true;
        }
    }
    EXPECT_TRUE(inner_marked);
    for (std::size_t segment = 0; segment < refined.segments.size(); segment++)
    {
        const Point & first = refined.vertices[refined.segments[segment][0]];
        if (first.y == 0.0 && refined.vertices[refined.segments[segment][1]].y == 0.0)
            EXPECT_EQ(refined.segment_markers[segment], 2);
    }

    // a maximum area for every triangle
    RefineOptions options;
    options.maximum_area = 0.1;
    const Mesh finer = refine_mesh(mesh, options);
    check_mesh(finer, 100.0);
    for (std::size_t triangle = 0; triangle < finer.triangles.size(); triangle++)
        EXPECT_LE(signed_area(finer, triangle), 0.1 + TOLERANCE);
}

TEST(Refine, WithoutSegmentsTheBoundaryIsKept)
{
    Mesh square;
    square.vertices = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
    square.triangles = {{0, 1, 2}, {0, 2, 3}};
    RefineOptions options;
    options.maximum_area = 0.01;
    options.conforming_delaunay = true;
    const Mesh refined = refine_mesh(square, options);
    check_mesh(refined, 1.0);
    // Triangle makes the boundary segments, with marker 1
    EXPECT_NEAR(length_on(refined, {0.0, 0.0}, {1.0, 0.0}), 1.0, TOLERANCE);
    for (const int marker : refined.segment_markers) EXPECT_EQ(marker, 1);
}

TEST(Refine, MaximumAngleIsReachedOrReported)
{
    const Mesh mesh = polygon_mesh();
    ASSERT_GT(count_angles_over(mesh, 100.0), 0u);

    RefineOptions options;
    options.minimum_angle = 30.0;
    options.maximum_angle = 100.0;
    const Mesh refined = refine_mesh(mesh, options);
    check_mesh(refined, 58.0);
    EXPECT_EQ(count_angles_over(refined, 100.0), 0u);
    EXPECT_NEAR(length_on(refined, {6.0, 8.0}, {2.0, 9.0}), std::hypot(4.0, 1.0), TOLERANCE);

    // 90 degrees does not finish, so the splits run out; the mesh is still
    // valid and the triangles over the limit are counted
    options.maximum_angle = 90.0;
    options.maximum_angle_splits = 50;
    const Mesh stopped = refine_mesh(mesh, options);
    check_mesh(stopped, 58.0);
    EXPECT_GT(count_angles_over(stopped, 90.0), 0u);

    // with no splits, the maximum angle changes nothing
    options.maximum_angle = 100.0;
    options.maximum_angle_splits = 0;
    RefineOptions plain;
    plain.minimum_angle = 30.0;
    EXPECT_EQ(refine_mesh(mesh, options).triangles.size(),
              refine_mesh(mesh, plain).triangles.size());
}

TEST(Refine, ErrorsGiveTheReason)
{
    const Mesh mesh = polygon_mesh();
    RefineOptions options;
    options.maximum_angle = 50.0;
    EXPECT_EQ(error_message([&] { refine_mesh(mesh, options); }),
              "the maximum angle is 50, but it must be 0 or over 60 and under 180 degrees");
    options = {};
    options.maximum_angle_splits = -1;
    EXPECT_EQ(error_message([&] { refine_mesh(mesh, options); }),
              "the maximum angle splits are -1, but they must be at least 0");

    Mesh limits = mesh;
    limits.triangle_area_limits.assign(3, 0.1);
    EXPECT_NE(error_message([&] { refine_mesh(limits, {}); }).find("area limits"),
              std::string::npos);
    EXPECT_EQ(error_message([] { refine_mesh({}, {}); }), "the mesh has no triangles to refine");

    // triangles must not overlap or repeat, which can make Triangle loop
    Mesh overlapping;
    overlapping.vertices = {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}};
    overlapping.triangles = {{0, 1, 2}, {0, 1, 3}};
    EXPECT_EQ(error_message([&] { refine_mesh(overlapping, {}); }),
              "triangle 1 runs along edge 0-1 the same way as another triangle, so they overlap");
    overlapping.triangles = {{0, 1, 2}, {0, 1, 2}};
    EXPECT_NE(error_message([&] { refine_mesh(overlapping, {}); }).find("overlap"),
              std::string::npos);

    // a segment must be an edge of a triangle, and only once
    Mesh square;
    square.vertices = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
    square.triangles = {{0, 1, 2}, {0, 2, 3}};
    square.segments = {{1, 3}};
    EXPECT_EQ(error_message([&] { refine_mesh(square, {}); }),
              "segment 0 (vertices 1-3, numbered from 0) is not an edge of a triangle");
    square.segments = {{0, 1}, {1, 0}};
    EXPECT_EQ(error_message([&] { refine_mesh(square, {}); }),
              "segment 1 (numbered from 0) repeats an earlier one");
}
