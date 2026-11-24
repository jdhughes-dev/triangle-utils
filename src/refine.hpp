// Refining a mesh: smaller triangles by area, and better shaped ones by
// minimum and maximum angle.

#ifndef TRIANGLE_UTILS_REFINE_HPP
#define TRIANGLE_UTILS_REFINE_HPP

#include <cstddef>

#include "mesh.hpp"

namespace triangle_utils
{

// Options for refining.
struct RefineOptions
{
    // the largest triangle area, or no limit if 0; the mesh's area limits
    // apply as well
    double maximum_area = 0.0;
    // the smallest angle in degrees, at most 34, or no limit if 0
    double minimum_angle = 0.0;
    // the largest angle in degrees, over 60 and under 180, or no limit if 0;
    // Triangle can reach about 95 to 100 degrees, not 90
    double maximum_angle = 0.0;
    // the most times a triangle is flagged for splitting for the maximum
    // angle; after them, refining for it stops, since it may never finish
    int maximum_angle_splits = 100000;
    // whether every triangle is Delaunay (Triangle's D switch)
    bool conforming_delaunay = false;
};

// Refine a mesh with Triangle, keeping its boundary and segments. Throws
// Error if the options are not valid, the triangles overlap, a segment is not
// an edge, or Triangle cannot refine the mesh.
Mesh refine_mesh(const Mesh & mesh, const RefineOptions & options);

// The number of triangles with an angle over the limit in degrees.
std::size_t count_angles_over(const Mesh & mesh, double limit);

} // namespace triangle_utils

#endif
