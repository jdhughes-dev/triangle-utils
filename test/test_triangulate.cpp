// Tests that Triangle meshes a unit square and that its errors are thrown with its reason.

#include <span>
#include <string>

#include <gtest/gtest.h>

#include "error.hpp"
#include "mesh.hpp"
#include "run_triangle.hpp"

using triangle_utils::run_triangle;
using triangle_utils::TriangleOutput;

namespace
{

// Signed area of a triangle; positive when its corners are counterclockwise.
double signed_area(const double * first, const double * second, const double * third)
{
    return 0.5 * ((second[0] - first[0]) * (third[1] - first[1]) -
                  (third[0] - first[0]) * (second[1] - first[1]));
}

} // namespace

TEST(Triangulate, UnitSquareWithAreaLimit)
{
    double vertices[] = {0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0};
    int segments[] = {0, 1, 1, 2, 2, 3, 3, 0};
    // the a switch below gives the same limit
    constexpr double AREA_LIMIT = 0.01;
    const std::string switches = "pzqa0.01";

    triangulateio input{};
    input.pointlist = vertices;
    input.numberofpoints = 4;
    input.segmentlist = segments;
    input.numberofsegments = 4;
    TriangleOutput output;

    run_triangle(switches, input, output);

    // a unit square needs at least 1 / AREA_LIMIT triangles
    const int triangle_count = output.get().numberoftriangles;
    ASSERT_GE(triangle_count, 100);
    std::span<const double> points(output.get().pointlist, 2 * output.get().numberofpoints);
    std::span<const int> corners(output.get().trianglelist, 3 * triangle_count);
    double total_area = 0.0;
    for (int triangle = 0; triangle < triangle_count; triangle++)
    {
        double area =
            signed_area(&points[2 * corners[3 * triangle]], &points[2 * corners[3 * triangle + 1]],
                        &points[2 * corners[3 * triangle + 2]]);
        EXPECT_GT(area, 0.0) << "triangle " << triangle;
        EXPECT_LE(area, AREA_LIMIT * (1.0 + 1e-12)) << "triangle " << triangle;
        total_area += area;
    }
    EXPECT_NEAR(total_area, 1.0, 1e-12);
}

TEST(Triangulate, ErrorGivesTriangleReason)
{
    double vertices[] = {0.0, 0.0, 1.0, 0.0};
    triangulateio input{};
    input.pointlist = vertices;
    input.numberofpoints = 2;
    TriangleOutput output;

    testing::internal::CaptureStdout();
    try
    {
        run_triangle("z", input, output);
        ADD_FAILURE() << "two vertices did not throw";
    }
    catch (const triangle_utils::Error & error)
    {
        EXPECT_STREQ(error.what(), "Triangle could not mesh the input: Input must have at "
                                   "least three input vertices.");
    }
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "") << "Triangle printed its message";
    EXPECT_EQ(output.get().pointlist, nullptr);
}
