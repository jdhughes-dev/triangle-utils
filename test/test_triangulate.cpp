// Tests that Triangle builds as a subproject and meshes a unit square.

#include <span>

#include <gtest/gtest.h>

#include "triangle_status.h"

namespace
{

// A triangulateio output whose arrays are freed when it goes out of scope.
struct TriangleOutput
{
    triangulateio mesh{};

    TriangleOutput() = default;
    TriangleOutput(const TriangleOutput &) = delete;
    TriangleOutput & operator=(const TriangleOutput &) = delete;

    // Arrays Triangle did not allocate are null; the hole and region lists
    // point to the input's arrays and are not freed.
    ~TriangleOutput()
    {
        for (void * array :
             {static_cast<void *>(mesh.pointlist), static_cast<void *>(mesh.pointattributelist),
              static_cast<void *>(mesh.pointmarkerlist), static_cast<void *>(mesh.trianglelist),
              static_cast<void *>(mesh.triangleattributelist),
              static_cast<void *>(mesh.trianglearealist), static_cast<void *>(mesh.neighborlist),
              static_cast<void *>(mesh.segmentlist), static_cast<void *>(mesh.segmentmarkerlist),
              static_cast<void *>(mesh.edgelist), static_cast<void *>(mesh.edgemarkerlist),
              static_cast<void *>(mesh.normlist)})
        {
            triangle_free(array);
        }
    }
};

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
    char switches[] = "pzqa0.01Q";

    triangulateio input{};
    input.pointlist = vertices;
    input.numberofpoints = 4;
    input.segmentlist = segments;
    input.numberofsegments = 4;
    TriangleOutput output;

    ASSERT_EQ(triangulate_status(switches, &input, &output.mesh, nullptr), 0);

    // a unit square needs at least 1 / AREA_LIMIT triangles
    const int triangle_count = output.mesh.numberoftriangles;
    ASSERT_GE(triangle_count, 100);
    std::span<const double> points(output.mesh.pointlist, 2 * output.mesh.numberofpoints);
    std::span<const int> corners(output.mesh.trianglelist, 3 * triangle_count);
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
