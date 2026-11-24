// Calls to Triangle that report its errors as exceptions.

#ifndef TRIANGLE_UTILS_RUN_TRIANGLE_HPP
#define TRIANGLE_UTILS_RUN_TRIANGLE_HPP

#include <string>

#include "mesh.hpp"

namespace triangle_utils
{

// Run Triangle quietly (Q is added to the switches). Throws Error with
// Triangle's reason if it fails, or std::bad_alloc if it runs out of memory;
// output and voronoi are then not valid.
void run_triangle(const std::string & switches, triangulateio & input, TriangleOutput & output,
                  TriangleOutput * voronoi = nullptr);

// A positive number for Triangle's switches, in fixed notation with 17
// significant digits, since Triangle reads only digits and a point there.
std::string switch_number(double value);

} // namespace triangle_utils

#endif
