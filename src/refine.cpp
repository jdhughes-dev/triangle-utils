// Refining a mesh: smaller triangles by area, and better shaped ones by
// minimum and maximum angle.

#include "refine.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include "error.hpp"
#include "run_triangle.hpp"

namespace triangle_utils
{

namespace
{

// the largest minimum angle; Triangle may not finish above it
constexpr double MAX_MINIMUM_ANGLE = 34.0;

// The largest angle in degrees of the triangle with these corners.
double largest_angle(const double * first, const double * second, const double * third)
{
    const double * corners[] = {first, second, third};
    double largest = 0.0;
    for (int corner = 0; corner < 3; corner++)
    {
        const double * at = corners[corner];
        const double * next = corners[(corner + 1) % 3];
        const double * previous = corners[(corner + 2) % 3];
        const double ux = next[0] - at[0], uy = next[1] - at[1];
        const double vx = previous[0] - at[0], vy = previous[1] - at[1];
        // atan2 stays defined for slivers, unlike acos
        largest = std::max(largest, std::atan2(std::abs(ux * vy - uy * vx), ux * vx + uy * vy));
    }
    return largest * 180.0 / std::numbers::pi;
}

// The state of the u switch's test for the maximum angle.
struct AngleTest
{
    double limit = 0.0;
    int splits_left = 0;
};

// Triangle's u switch test: split a triangle over the angle limit until the
// splits are used, then accept every triangle so Triangle finishes.
int over_angle(const double * org, const double * dest, const double * apex, double, void * data)
{
    auto * test = static_cast<AngleTest *>(data);
    if (test->splits_left <= 0 || largest_angle(org, dest, apex) <= test->limit) return 0;
    test->splits_left--;
    return 1;
}

// Clears the u switch's test when it goes out of scope.
struct TestGuard
{
    ~TestGuard() { triangle_set_unsuitable(nullptr, nullptr); }
};

} // namespace

Mesh refine_mesh(const Mesh & mesh, const RefineOptions & options)
{
    if (!(options.maximum_area >= 0.0 && std::isfinite(options.maximum_area)))
        throw Error(message("the maximum area is ", options.maximum_area,
                            ", but it must be finite and at least 0"));
    if (!(options.minimum_angle >= 0.0 && options.minimum_angle <= MAX_MINIMUM_ANGLE))
        throw Error(message("the minimum angle is ", options.minimum_angle,
                            ", but it must be 0 to ", MAX_MINIMUM_ANGLE, " degrees"));
    if (!(options.maximum_angle == 0.0 ||
          (options.maximum_angle > 60.0 && options.maximum_angle < 180.0)))
        throw Error(message("the maximum angle is ", options.maximum_angle,
                            ", but it must be 0 or over 60 and under 180 degrees"));
    if (options.maximum_angle_splits < 0)
        throw Error(message("the maximum angle splits are ", options.maximum_angle_splits,
                            ", but they must be at least 0"));
    if (mesh.triangles.empty()) throw Error("the mesh has no triangles to refine");
    check_triangles(mesh);
    for (const double limit : mesh.triangle_area_limits)
        if (!std::isfinite(limit)) throw Error("an area limit is not finite");

    // Triangle reads the input without changing it; the copy gives it the
    // non-const arrays it takes, without neighbors, which it does not read
    Mesh input = mesh;
    input.neighbors.clear();
    triangulateio triangle_input = to_triangulateio(input);

    // r: refine, p: keep the segments, z: numbered from 0, n: neighbors,
    // a: the mesh's area limits, a<area>, q<angle>, u: the angle test
    std::string switches = "rpzn";
    if (!input.triangle_area_limits.empty()) switches += "a";
    if (options.maximum_area > 0.0) switches += "a" + switch_number(options.maximum_area);
    if (options.minimum_angle > 0.0) switches += "q" + switch_number(options.minimum_angle);
    if (options.conforming_delaunay) switches += "D";

    AngleTest test{options.maximum_angle, options.maximum_angle_splits};
    TestGuard guard;
    if (options.maximum_angle > 0.0)
    {
        triangle_set_unsuitable(over_angle, &test);
        switches += "u";
    }
    TriangleOutput output;
    run_triangle(switches, triangle_input, output);
    return from_triangulateio(output.get());
}

std::size_t count_angles_over(const Mesh & mesh, double limit)
{
    std::size_t count = 0;
    for (const auto & corners : mesh.triangles)
    {
        if (largest_angle(&mesh.vertices[corners[0]].x, &mesh.vertices[corners[1]].x,
                          &mesh.vertices[corners[2]].x) > limit)
            count++;
    }
    return count;
}

} // namespace triangle_utils
