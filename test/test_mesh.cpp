// Tests the mesh type, its conversion to and from Triangle, and its checks.

#include <cstddef>
#include <utility>

#include <gtest/gtest.h>

#include "error.hpp"
#include "mesh.hpp"
#include "triangle_status.h"

using triangle_utils::check_neighbors;
using triangle_utils::check_triangles;
using triangle_utils::Error;
using triangle_utils::from_triangulateio;
using triangle_utils::Mesh;
using triangle_utils::NO_NEIGHBOR;
using triangle_utils::to_triangulateio;
using triangle_utils::total_area;
using triangle_utils::TriangleOutput;

namespace
{

// A unit square meshed with Triangle, with neighbors and a region attribute.
Mesh unit_square_mesh()
{
    double vertices[] = {0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0};
    int segments[] = {0, 1, 1, 2, 2, 3, 3, 0};
    // x, y, attribute, and area limit of the one region
    double regions[] = {0.5, 0.5, 7.0, 0.0};
    char switches[] = "pzqa0.01AnQ";

    triangulateio input{};
    input.pointlist = vertices;
    input.numberofpoints = 4;
    input.segmentlist = segments;
    input.numberofsegments = 4;
    input.regionlist = regions;
    input.numberofregions = 1;
    TriangleOutput output;
    if (triangulate_status(switches, &input, &output.get(), nullptr) != 0)
        throw Error("Triangle could not mesh the unit square");
    return from_triangulateio(output.get());
}

// An interior edge: a triangle and the corner opposite an edge it shares.
std::pair<std::size_t, int> interior_edge(const Mesh & mesh)
{
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); triangle++)
    {
        for (int corner = 0; corner < 3; corner++)
        {
            if (mesh.neighbors[triangle][corner] != NO_NEIGHBOR) return {triangle, corner};
        }
    }
    throw Error("the mesh has no interior edge");
}

} // namespace

TEST(Mesh, FromTriangleOutput)
{
    Mesh mesh = unit_square_mesh();

    EXPECT_GE(mesh.triangles.size(), 100u);
    EXPECT_EQ(mesh.vertex_markers.size(), mesh.vertices.size());
    EXPECT_EQ(mesh.neighbors.size(), mesh.triangles.size());
    EXPECT_GE(mesh.segments.size(), 4u);
    EXPECT_EQ(mesh.segment_markers.size(), mesh.segments.size());
    ASSERT_EQ(mesh.triangle_attribute_count, 1);
    ASSERT_EQ(mesh.triangle_attributes.size(), mesh.triangles.size());
    for (double attribute : mesh.triangle_attributes)
    {
        EXPECT_EQ(attribute, 7.0);
    }
    EXPECT_NO_THROW(check_triangles(mesh));
    EXPECT_NO_THROW(check_neighbors(mesh));
    EXPECT_NEAR(total_area(mesh), 1.0, 1e-12);
}

TEST(Mesh, RoundTripThroughTriangulateio)
{
    Mesh mesh = unit_square_mesh();
    Mesh copy = from_triangulateio(to_triangulateio(mesh));

    ASSERT_EQ(copy.vertices.size(), mesh.vertices.size());
    for (std::size_t vertex = 0; vertex < mesh.vertices.size(); vertex++)
    {
        EXPECT_EQ(copy.vertices[vertex].x, mesh.vertices[vertex].x);
        EXPECT_EQ(copy.vertices[vertex].y, mesh.vertices[vertex].y);
    }
    EXPECT_EQ(copy.vertex_markers, mesh.vertex_markers);
    EXPECT_EQ(copy.triangles, mesh.triangles);
    EXPECT_EQ(copy.triangle_attribute_count, mesh.triangle_attribute_count);
    EXPECT_EQ(copy.triangle_attributes, mesh.triangle_attributes);
    EXPECT_EQ(copy.neighbors, mesh.neighbors);
    EXPECT_EQ(copy.segments, mesh.segments);
    EXPECT_EQ(copy.segment_markers, mesh.segment_markers);
}

TEST(Mesh, RefinedByTriangle)
{
    Mesh mesh = unit_square_mesh();
    triangulateio input = to_triangulateio(mesh);
    TriangleOutput output;
    char switches[] = "rpzqa0.002nQ";
    ASSERT_EQ(triangulate_status(switches, &input, &output.get(), nullptr), 0);

    Mesh refined = from_triangulateio(output.get());
    EXPECT_GT(refined.triangles.size(), mesh.triangles.size());
    EXPECT_NO_THROW(check_triangles(refined));
    EXPECT_NO_THROW(check_neighbors(refined));
    EXPECT_NEAR(total_area(refined), 1.0, 1e-12);
    for (double attribute : refined.triangle_attributes)
    {
        EXPECT_EQ(attribute, 7.0);
    }
    // every segment stays on the square's boundary
    ASSERT_EQ(refined.segment_markers.size(), refined.segments.size());
    for (const auto & segment : refined.segments)
    {
        const auto & start = refined.vertices[segment[0]];
        const auto & end = refined.vertices[segment[1]];
        bool on_side = (start.x == end.x && (start.x == 0.0 || start.x == 1.0)) ||
                       (start.y == end.y && (start.y == 0.0 || start.y == 1.0));
        EXPECT_TRUE(on_side) << "segment from (" << start.x << ", " << start.y << ")";
    }
}

TEST(Mesh, ToTriangulateioPointsIntoTheMesh)
{
    Mesh mesh = unit_square_mesh();
    triangulateio view = to_triangulateio(mesh);
    EXPECT_EQ(view.pointlist, &mesh.vertices[0].x);
    EXPECT_EQ(view.trianglelist, mesh.triangles[0].data());
    EXPECT_EQ(view.neighborlist, mesh.neighbors[0].data());

    Mesh empty;
    triangulateio empty_view = to_triangulateio(empty);
    EXPECT_EQ(empty_view.pointlist, nullptr);
    EXPECT_EQ(empty_view.trianglelist, nullptr);
    EXPECT_EQ(empty_view.numberofpoints, 0);
}

TEST(Mesh, MismatchedSizesAreAnError)
{
    Mesh markers = unit_square_mesh();
    markers.vertex_markers.pop_back();
    EXPECT_THROW(to_triangulateio(markers), Error);

    Mesh attributes = unit_square_mesh();
    attributes.triangle_attributes.pop_back();
    EXPECT_THROW(to_triangulateio(attributes), Error);

    Mesh limits = unit_square_mesh();
    limits.triangle_area_limits.assign(limits.triangles.size() + 1, 0.01);
    EXPECT_THROW(to_triangulateio(limits), Error);
}

TEST(Mesh, ClockwiseTriangleIsAnError)
{
    Mesh mesh = unit_square_mesh();
    std::swap(mesh.triangles[0][1], mesh.triangles[0][2]);
    EXPECT_THROW(check_triangles(mesh), Error);
}

TEST(Mesh, MissingNeighborIsAnError)
{
    Mesh mesh = unit_square_mesh();
    auto [triangle, corner] = interior_edge(mesh);
    mesh.neighbors[triangle][corner] = NO_NEIGHBOR;
    EXPECT_THROW(check_neighbors(mesh), Error);
}

TEST(Mesh, MissingNeighborRowsAreAnError)
{
    Mesh mesh = unit_square_mesh();
    mesh.neighbors.clear();
    EXPECT_THROW(check_neighbors(mesh), Error);
}

TEST(Mesh, WrongNeighborIsAnError)
{
    Mesh mesh = unit_square_mesh();
    auto [triangle, corner] = interior_edge(mesh);
    int right = mesh.neighbors[triangle][corner];
    mesh.neighbors[triangle][corner] = right == 0 ? 1 : 0;
    EXPECT_THROW(check_neighbors(mesh), Error);
}

TEST(Mesh, EdgeSharedByThreeTrianglesIsAnError)
{
    Mesh mesh;
    mesh.vertices = {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {0.5, -1.0}, {0.5, 2.0}};
    mesh.triangles = {{0, 1, 2}, {1, 0, 3}, {0, 1, 4}};
    mesh.neighbors.assign(3, {NO_NEIGHBOR, NO_NEIGHBOR, NO_NEIGHBOR});
    EXPECT_THROW(check_neighbors(mesh), Error);
}

TEST(Mesh, InvalidTriangleOutputIsAnError)
{
    double vertices[] = {0.0, 0.0, 1.0, 0.0, 0.0, 1.0};
    int corners[] = {0, 1, 2};
    int neighbors[] = {-1, -1, 1};
    int segments[] = {0, 3};
    auto valid = [&]()
    {
        triangulateio input{};
        input.pointlist = vertices;
        input.numberofpoints = 3;
        input.trianglelist = corners;
        input.numberoftriangles = 1;
        input.numberofcorners = 3;
        return input;
    };
    EXPECT_NO_THROW(from_triangulateio(valid()));

    triangulateio six_corners = valid();
    six_corners.numberofcorners = 6;
    EXPECT_THROW(from_triangulateio(six_corners), Error);

    triangulateio no_array = valid();
    no_array.trianglelist = nullptr;
    EXPECT_THROW(from_triangulateio(no_array), Error);

    triangulateio negative_count = valid();
    negative_count.numberofpoints = -1;
    EXPECT_THROW(from_triangulateio(negative_count), Error);

    triangulateio neighbor_out_of_range = valid();
    neighbor_out_of_range.neighborlist = neighbors;
    EXPECT_THROW(from_triangulateio(neighbor_out_of_range), Error);

    triangulateio segment_out_of_range = valid();
    segment_out_of_range.segmentlist = segments;
    segment_out_of_range.numberofsegments = 1;
    EXPECT_THROW(from_triangulateio(segment_out_of_range), Error);
}

TEST(Mesh, VertexOutOfRangeIsAnError)
{
    double vertices[] = {0.0, 0.0, 1.0, 0.0, 0.0, 1.0};
    int corners[] = {0, 1, 3};
    triangulateio input{};
    input.pointlist = vertices;
    input.numberofpoints = 3;
    input.trianglelist = corners;
    input.numberoftriangles = 1;
    input.numberofcorners = 3;
    EXPECT_THROW(from_triangulateio(input), Error);
}
