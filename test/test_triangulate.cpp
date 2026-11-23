// Tests that Triangle builds as a subproject and meshes a unit square.

#include <cmath>
#include <cstdio>
#include <span>
#include <string>

#include <gtest/gtest.h>

#include "triangle_status.h"
#include "triangle_utils_version.h"

namespace {

// Free the arrays Triangle allocated in an output structure.
void free_output(triangulateio &output)
{
  for (void *array :
       {static_cast<void *>(output.pointlist),
        static_cast<void *>(output.pointattributelist),
        static_cast<void *>(output.pointmarkerlist),
        static_cast<void *>(output.trianglelist),
        static_cast<void *>(output.triangleattributelist),
        static_cast<void *>(output.trianglearealist),
        static_cast<void *>(output.neighborlist),
        static_cast<void *>(output.segmentlist),
        static_cast<void *>(output.segmentmarkerlist),
        static_cast<void *>(output.edgelist),
        static_cast<void *>(output.edgemarkerlist),
        static_cast<void *>(output.normlist)}) {
    triangle_free(array);
  }
}

// Signed area of a triangle; positive when its corners are counterclockwise.
double signed_area(const double *first, const double *second,
                   const double *third)
{
  return 0.5 * ((second[0] - first[0]) * (third[1] - first[1]) -
                (third[0] - first[0]) * (second[1] - first[1]));
}

} // namespace

TEST(Triangulate, UnitSquareWithAreaLimit)
{
  double vertices[] = {0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0};
  int segments[] = {0, 1, 1, 2, 2, 3, 3, 0};
  constexpr double AREA_LIMIT = 0.01;

  triangulateio input{};
  input.pointlist = vertices;
  input.numberofpoints = 4;
  input.segmentlist = segments;
  input.numberofsegments = 4;
  triangulateio output{};

  char switches[32];
  std::snprintf(switches, sizeof(switches), "pzqa%gQ", AREA_LIMIT);
  ASSERT_EQ(triangulate_status(switches, &input, &output, nullptr), 0);

  std::span<const double> points(output.pointlist,
                                 2 * output.numberofpoints);
  std::span<const int> corners(output.trianglelist,
                               3 * output.numberoftriangles);
  ASSERT_GT(output.numberoftriangles, 0);
  double total_area = 0.0;
  for (int triangle = 0; triangle < output.numberoftriangles; triangle++) {
    double area = signed_area(&points[2 * corners[3 * triangle]],
                              &points[2 * corners[3 * triangle + 1]],
                              &points[2 * corners[3 * triangle + 2]]);
    EXPECT_GT(area, 0.0) << "triangle " << triangle;
    EXPECT_LE(area, AREA_LIMIT * (1.0 + 1e-12)) << "triangle " << triangle;
    total_area += area;
  }
  EXPECT_NEAR(total_area, 1.0, 1e-12);

  free_output(output);
}

TEST(Version, MacrosMatchVersionString)
{
  std::string parts = std::to_string(TRIANGLE_UTILS_VERSION_MAJOR) + "." +
                      std::to_string(TRIANGLE_UTILS_VERSION_MINOR) + "." +
                      std::to_string(TRIANGLE_UTILS_VERSION_PATCH);
  EXPECT_EQ(parts, TRIANGLE_UTILS_VERSION);
  EXPECT_STREQ(triangle_version(), "1.6");
}
