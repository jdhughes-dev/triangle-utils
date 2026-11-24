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
    // the largest angle in degrees to aim for, over 60 and under 180, or 0
    // to improve the smallest angle only; 90 puts every circumcenter inside
    // its triangle
    double maximum_angle = 0.0;
    // the smallest angle in degrees a move may leave, up to 60, or 0 for the
    // mesh's own smallest angle, so it does not decrease; a lower value
    // trades smaller angles for fewer over the maximum angle
    double minimum_angle = 0.0;
    // the most sweeps over the vertices
    int iterations = 100;
    // the sweeps stop when no vertex moves farther than this fraction of
    // the mean edge length
    double tolerance = 1e-4;
};

// Improve a mesh: move each vertex that is not fixed (on a segment or the
// boundary, where triangles of different attributes or area limits meet, or
// in the mesh's fixed vertices) toward where the smoothing puts it, only
// where no triangle inverts, no angle falls below the minimum angle (the
// mesh's smallest by default) or the vertex's own smallest angle if that is
// lower, and the worst angle around the vertex improves; then flip edges
// that are not segments so the triangles are Delaunay. No move or flip
// takes a triangle over its area limit. The triangles, their attributes and
// area limits, and the segments are kept; the result has neighbors. Throws
// Error if the mesh or options are not valid.
Mesh improve_mesh(const Mesh & mesh, const ImproveOptions & options);

} // namespace triangle_utils

#endif
