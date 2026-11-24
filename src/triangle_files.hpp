// Reading and writing Triangle's files (.node, .ele, .poly, .neigh), for the
// command line; indices in memory are zero based.

#ifndef TRIANGLE_UTILS_TRIANGLE_FILES_HPP
#define TRIANGLE_UTILS_TRIANGLE_FILES_HPP

#include <array>
#include <cstddef>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "mesh.hpp"

namespace triangle_utils
{

// The vertices of a .node file, or of the vertex section of a .poly file.
struct VertexFile
{
    std::vector<Point> vertices;
    // number of attributes of each vertex
    int attribute_count = 0;
    // attribute_count values for each vertex in turn
    std::vector<double> attributes;
    // a boundary marker for each vertex, or empty
    std::vector<int> markers;
    // the number of the first vertex, 0 or 1, as Triangle reads it: 1 unless
    // the first vertex is numbered 0; the other files number vertices and
    // triangles from it too
    int first_number = 1;
};

// The triangles of an .ele file.
struct TriangleFile
{
    // corners of each triangle
    std::vector<std::array<int, 3>> triangles;
    int attribute_count = 0;
    // attribute_count values for each triangle in turn
    std::vector<double> attributes;
};

// A region of a .poly file: a point inside it, its attribute, and its
// maximum triangle area.
struct Region
{
    Point point;
    double attribute = 0.0;
    double area_limit = 0.0;
};

// The contents of a .poly file.
struct PolyFile
{
    // empty when the vertices are in a separate .node file
    VertexFile vertices;
    std::vector<std::array<int, 2>> segments;
    // a boundary marker for each segment, or empty
    std::vector<int> segment_markers;
    // a point inside each hole
    std::vector<Point> holes;
    std::vector<Region> regions;
};

// Read a .node file; name is used in error messages. Throws Error if the file
// is not valid.
VertexFile read_node(std::istream & input, const std::string & name);

// Read an .ele file whose corners are vertices of a .node file.
TriangleFile read_ele(std::istream & input, const std::string & name, const VertexFile & vertices);

// Read a .poly file; separate_vertices are the vertices of the .node file,
// required when the .poly file has none.
PolyFile read_poly(std::istream & input, const std::string & name,
                   const VertexFile * separate_vertices = nullptr);

// Read a .neigh file for triangle_count triangles; a missing neighbor is
// NO_NEIGHBOR.
std::vector<std::array<int, 3>> read_neigh(std::istream & input, const std::string & name,
                                           int first_number, std::size_t triangle_count);

// Write a .node file.
void write_node(std::ostream & output, const VertexFile & vertices);

// Write an .ele file, numbering from first_number.
void write_ele(std::ostream & output, const TriangleFile & triangles, int first_number);

// Write a .poly file; without vertices it uses those of a .node file numbered
// from first_number.
void write_poly(std::ostream & output, const PolyFile & poly, int first_number = 1);

// Write a .neigh file, numbering from first_number.
void write_neigh(std::ostream & output, const std::vector<std::array<int, 3>> & neighbors,
                 int first_number);

} // namespace triangle_utils

#endif
