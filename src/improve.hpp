// Improving a mesh by moving vertices and flipping edges.

#ifndef TRIANGLE_UTILS_IMPROVE_HPP
#define TRIANGLE_UTILS_IMPROVE_HPP

#include "mesh.hpp"

namespace triangle_utils
{

// Where a free vertex is moved toward.
enum class Smoothing
{
    // the average of its neighboring vertices
    LAPLACIAN,
    // the average of the centroids of its triangles, weighted by area
    CENTROIDAL,
};

// Options for improving.
struct ImproveOptions
{
    Smoothing smoothing = Smoothing::LAPLACIAN;
    // the largest angle to aim for in degrees, or 0 for none
    double maximum_angle = 0.0;
    // the smallest angle a move may leave in degrees, or 0 for the mesh's own
    double minimum_angle = 0.0;
    // the most sweeps over the vertices
    int iterations = 100;
    // the sweeps stop when no vertex moves farther than this fraction of
    // the mean edge length
    double tolerance = 1e-4;
};

// Improve a mesh by moving its vertices that are not fixed and flipping
// edges. Throws Error if the mesh or options are not valid.
Mesh improve_mesh(const Mesh & mesh, const ImproveOptions & options);

} // namespace triangle_utils

#endif
