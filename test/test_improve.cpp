// Tests improving a mesh by moving vertices: the smallest angle does not
// decrease, fixed and boundary vertices do not move, the area is kept, and
// obtuse triangles are removed toward a maximum angle.

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include <string>

#include <gtest/gtest.h>

#include "error.hpp"
#include "geometry.hpp"
#include "improve.hpp"
#include "refine.hpp"

using namespace triangle_utils;

namespace
{

constexpr double TOLERANCE = 1e-9;

Geometry polygon_geometry()
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
    return geometry;
}

double smallest_angle(const Mesh & mesh)
{
    double smallest = 180.0;
    for (const auto & corners : mesh.triangles)
    {
        for (int corner = 0; corner < 3; corner++)
        {
            const Point & a = mesh.vertices[corners[corner]];
            const Point & b = mesh.vertices[corners[(corner + 1) % 3]];
            const Point & c = mesh.vertices[corners[(corner + 2) % 3]];
            const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - a.x, vy = c.y - a.y;
            smallest =
                std::min(smallest, std::atan2(std::abs(ux * vy - uy * vx), ux * vx + uy * vy) *
                                       180.0 / 3.14159265358979323846);
        }
    }
    return smallest;
}

void check_mesh(const Mesh & mesh, double area)
{
    check_sizes(mesh);
    check_triangles(mesh);
    check_neighbors(mesh);
    EXPECT_NEAR(total_area(mesh), area, TOLERANCE * area);
}

// The corners with an obtuse angle, as (vertex, triangle) pairs.
std::vector<std::pair<int, int>> obtuse_corners(const Mesh & mesh, double limit)
{
    std::vector<std::pair<int, int>> corners;
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        const auto & t = mesh.triangles[triangle];
        for (int corner = 0; corner < 3; corner++)
        {
            const Point & a = mesh.vertices[t[corner]];
            const Point & b = mesh.vertices[t[(corner + 1) % 3]];
            const Point & c = mesh.vertices[t[(corner + 2) % 3]];
            const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - a.x, vy = c.y - a.y;
            if (std::atan2(std::abs(ux * vy - uy * vx), ux * vx + uy * vy) * 180.0 /
                    3.14159265358979323846 >
                limit)
                corners.push_back({t[corner], static_cast<int>(triangle)});
        }
    }
    return corners;
}

// Whether each vertex is on the boundary, where an edge has one triangle.
std::vector<bool> on_boundary(const Mesh & mesh)
{
    std::vector<bool> boundary(mesh.vertices.size(), false);
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
        for (int corner = 0; corner < 3; corner++)
            if (mesh.neighbors[triangle][corner] == NO_NEIGHBOR)
            {
                boundary[mesh.triangles[triangle][(corner + 1) % 3]] = true;
                boundary[mesh.triangles[triangle][(corner + 2) % 3]] = true;
            }
    return boundary;
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

TEST(Improve, ObtuseTrianglesAwayFromTheBoundaryAreRemoved)
{
    MeshOptions mesh_options;
    mesh_options.minimum_angle = 30.0;
    mesh_options.maximum_area = 0.5;
    const Mesh mesh = generate_mesh(polygon_geometry(), mesh_options);
    RefineOptions refine_options;
    refine_options.minimum_angle = 30.0;
    refine_options.maximum_angle = 100.0;
    const Mesh refined = refine_mesh(mesh, refine_options);
    const std::vector<bool> boundary = on_boundary(refined);
    ASSERT_GT(obtuse_corners(refined, 90.0).size(), 0u);

    for (const Smoothing smoothing : {Smoothing::LAPLACIAN, Smoothing::CENTROIDAL})
    {
        ImproveOptions options;
        options.smoothing = smoothing;
        options.maximum_angle = 90.0;
        const Mesh improved = improve_mesh(refined, options);
        check_mesh(improved, 58.0);
        EXPECT_GE(smallest_angle(improved), smallest_angle(refined) - TOLERANCE);
        EXPECT_LT(count_angles_over(improved, 90.0), count_angles_over(refined, 90.0));
        // what remains is obtuse at a boundary point where two triangles meet,
        // whose angles sum to 180 degrees, which moving vertices cannot change
        for (const auto & [vertex, triangle] : obtuse_corners(improved, 90.0))
            EXPECT_TRUE(boundary[vertex]) << "obtuse at interior vertex " << vertex;
        // boundary and segment vertices do not move
        for (std::size_t vertex = 0; vertex < refined.vertices.size(); vertex++)
        {
            if (!boundary[vertex]) continue;
            EXPECT_EQ(improved.vertices[vertex].x, refined.vertices[vertex].x);
            EXPECT_EQ(improved.vertices[vertex].y, refined.vertices[vertex].y);
        }
        EXPECT_EQ(improved.segments, refined.segments);
    }
}

TEST(Improve, ALowerMinimumAngleRemovesMoreObtuseTriangles)
{
    // the user guide's valley, meshed at 30 degrees and refined: some obtuse
    // triangles can be removed only by taking an angle below 30 degrees,
    // which a lower minimum angle allows
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
    geometry.lines.push_back({{{1800.0, 7840.0},
                               {1900.0, 6500.0},
                               {2300.0, 5200.0},
                               {2100.0, 3800.0},
                               {2500.0, 2400.0},
                               {2600.0, 1000.0},
                               {2400.0, 0.0}},
                              50.0,
                              2});
    geometry.polygons.push_back(
        {{{700.0, 5200.0}, {1300.0, 4700.0}, {1500.0, 5400.0}, {1200.0, 6200.0}, {700.0, 6000.0}},
         60.0,
         3});
    geometry.regions.push_back({{500.0, 1000.0}, 1.0, 0.0});
    geometry.regions.push_back({{4000.0, 1000.0}, 1.0, 0.0});
    geometry.regions.push_back({{1100.0, 5400.0}, 2.0, 3600.0});
    for (const Point & well : {Point{3300.0, 4400.0}, Point{1000.0, 2000.0}, Point{3400.0, 1500.0}})
        geometry.rings.push_back({well, 50.0, 16, 3.0, 300.0, 4});
    MeshOptions mesh_options;
    mesh_options.minimum_angle = 30.0;
    mesh_options.maximum_area = 40000.0;
    const Mesh mesh = generate_mesh(geometry, mesh_options);
    RefineOptions refine_options;
    refine_options.minimum_angle = 30.0;
    refine_options.maximum_angle = 100.0;
    const Mesh refined = refine_mesh(mesh, refine_options);

    ImproveOptions options;
    options.maximum_angle = 90.0;
    const Mesh kept = improve_mesh(refined, options);
    options.minimum_angle = 25.0;
    const Mesh traded = improve_mesh(refined, options);
    EXPECT_GE(smallest_angle(kept), smallest_angle(refined) - TOLERANCE);
    EXPECT_GE(smallest_angle(traded), 25.0 - TOLERANCE);
    EXPECT_LT(count_angles_over(traded, 90.0), count_angles_over(kept, 90.0));
    for (const int vertex : refined.fixed_vertices)
    {
        EXPECT_EQ(traded.vertices[vertex].x, refined.vertices[vertex].x);
        EXPECT_EQ(traded.vertices[vertex].y, refined.vertices[vertex].y);
    }
}

TEST(Improve, SmallestAngleIncreasesAndFixedVerticesStay)
{
    // a mesh without a minimum angle, with a well's ring and a point
    Geometry geometry;
    geometry.polygons.push_back({{{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}}, 1.0, 1});
    geometry.rings.push_back({{4.0, 6.0}, 0.5, 8, 2.0, 0.0, 3});
    geometry.points.push_back({7.0, 3.0});
    MeshOptions mesh_options;
    mesh_options.maximum_area = 0.5;
    const Mesh mesh = generate_mesh(geometry, mesh_options);
    ASSERT_EQ(mesh.fixed_vertices.size(), 2u);

    const Mesh improved = improve_mesh(mesh, {});
    check_mesh(improved, 100.0);
    EXPECT_GT(smallest_angle(improved), smallest_angle(mesh) + 0.5);
    for (const int vertex : mesh.fixed_vertices)
    {
        EXPECT_EQ(improved.vertices[vertex].x, mesh.vertices[vertex].x);
        EXPECT_EQ(improved.vertices[vertex].y, mesh.vertices[vertex].y);
    }
    EXPECT_EQ(improved.fixed_vertices, mesh.fixed_vertices);
    EXPECT_EQ(improved.triangle_attributes.size(), mesh.triangle_attributes.size());
    // the ring's triangles keep its attribute: the region is bounded by its
    // polygon, which is a segment
    double ring_area = 0.0;
    for (std::size_t triangle = 0; triangle < improved.triangles.size(); triangle++)
        if (improved.triangle_attributes[triangle] == 2.0)
            ring_area += signed_area(improved, triangle);
    double original_ring_area = 0.0;
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
        if (mesh.triangle_attributes[triangle] == 2.0)
            original_ring_area += signed_area(mesh, triangle);
    EXPECT_NEAR(ring_area, original_ring_area, TOLERANCE);
}

TEST(Improve, AreaLimitsHolesRegionsAndFixedVerticesAreKept)
{
    // a hole polygon with a point inside it, which the hole removes, and a
    // region with an area limit on each side of a line
    Geometry geometry;
    geometry.polygons.push_back({{{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}}, 1.0, 1});
    geometry.hole_polygons.push_back({{{6.0, 6.0}, {8.0, 6.0}, {8.0, 8.0}, {6.0, 8.0}}});
    geometry.points.push_back({7.0, 7.0});
    geometry.points.push_back({2.0, 8.0});
    geometry.lines.push_back({{{0.0, 3.0}, {10.0, 3.0}}, 0.5, 2});
    geometry.regions.push_back({{1.0, 1.0}, 1.0, 0.05});
    geometry.regions.push_back({{1.0, 9.0}, 2.0, 0.0});
    MeshOptions mesh_options;
    mesh_options.minimum_angle = 25.0;
    const Mesh mesh = generate_mesh(geometry, mesh_options);
    // the point in the hole is not a vertex of the mesh, so only the other
    // is fixed
    ASSERT_EQ(mesh.fixed_vertices.size(), 1u);
    EXPECT_EQ(mesh.vertices[mesh.fixed_vertices[0]].x, 2.0);
    Mesh limited = mesh;
    limited.triangle_area_limits.assign(mesh.triangles.size(), 0.0);
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
        if (mesh.triangle_attributes[triangle] == 1.0)
            limited.triangle_area_limits[triangle] = 0.05;
    RefineOptions refine_options;
    refine_options.minimum_angle = 25.0;
    refine_options.maximum_angle = 100.0;
    const Mesh refined = refine_mesh(limited, refine_options);
    Mesh carried = refined;
    carried.triangle_area_limits.assign(refined.triangles.size(), 0.0);
    for (std::size_t triangle = 0; triangle < refined.triangles.size(); triangle++)
        if (refined.triangle_attributes[triangle] == 1.0)
            carried.triangle_area_limits[triangle] = 0.05;

    for (const Smoothing smoothing : {Smoothing::LAPLACIAN, Smoothing::CENTROIDAL})
    {
        ImproveOptions options;
        options.smoothing = smoothing;
        options.maximum_angle = 90.0;
        const Mesh improved = improve_mesh(carried, options);
        check_mesh(improved, 96.0);
        double lower = 0.0;
        for (std::size_t triangle = 0; triangle < improved.triangles.size(); triangle++)
        {
            if (improved.triangle_attributes[triangle] != 1.0) continue;
            EXPECT_LE(signed_area(improved, triangle), 0.05 + TOLERANCE);
            lower += signed_area(improved, triangle);
        }
        // the region below the line keeps its area: 10 by 3
        EXPECT_NEAR(lower, 30.0, 1e-9);
        EXPECT_EQ(improved.vertices[refined.fixed_vertices[0]].x, 2.0);
        EXPECT_EQ(improved.vertices[refined.fixed_vertices[0]].y, 8.0);
    }
}

TEST(Improve, ErrorsGiveTheReason)
{
    const Mesh mesh = generate_mesh(polygon_geometry(), {});
    ImproveOptions options;
    options.maximum_angle = 45.0;
    EXPECT_EQ(error_message([&] { improve_mesh(mesh, options); }),
              "the maximum angle is 45, but it must be 0 or over 60 and under 180 degrees");
    options = {};
    options.minimum_angle = 70.0;
    EXPECT_EQ(error_message([&] { improve_mesh(mesh, options); }),
              "the minimum angle is 70, but it must be 0 to 60 degrees");
    options = {};
    options.iterations = -1;
    EXPECT_EQ(error_message([&] { improve_mesh(mesh, options); }),
              "the iterations are -1, but they must be at least 0");
    EXPECT_EQ(error_message([] { improve_mesh({}, {}); }), "the mesh has no triangles to improve");
    Mesh bad = mesh;
    bad.fixed_vertices = {static_cast<int>(mesh.vertices.size())};
    EXPECT_NE(error_message([&] { improve_mesh(bad, {}); }).find("fixed vertex"),
              std::string::npos);
}
