// Tests reading and writing Triangle's files: round trips, Triangle's rules
// for numbering and missing values, and errors that name the file and line.

#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "error.hpp"
#include "triangle_files.hpp"

using namespace triangle_utils;

namespace
{

// The message of the Error an action throws, or an empty string.
template <typename Action> std::string error_message(Action && action)
{
    try
    {
        action();
    }
    catch (const Error & error)
    {
        return error.what();
    }
    return "";
}

VertexFile read_node_text(const std::string & text)
{
    std::istringstream input(text);
    return read_node(input, "test.node");
}

// Triangle's box.poly example: a square with a square hole, numbered from 1.
const std::string BOX_POLY =
    R"(# A box with eight vertices in 2D, no attributes, one boundary marker.
8 2 0 1
 # Outer box has these vertices:
 1   0 0   0
 2   0 3   0
 3   3 0   0
 4   3 3   33     # A special marker for this vertex.
 # Inner square has these vertices:
 5   1 1   0
 6   1 2   0
 7   2 1   0
 8   2 2   0
# Five segments with boundary markers.
5 1
 1   1 2   5      # Left side of outer box.
 # Square hole has these segments:
 2   5 7   0
 3   7 8   0
 4   8 6   10
 5   6 5   0
# One hole in the middle of the inner square.
1
 1   1.5 1.5
)";

} // namespace

TEST(TriangleFiles, NodeRoundTrip)
{
    VertexFile vertices;
    vertices.vertices = {{0.0, 0.0}, {1.0, 0.1}, {0.3, 1.0 / 3.0}};
    vertices.attribute_count = 2;
    vertices.attributes = {1.0, -2.5, 3.0, 4.0, 1e-300, 6.25};
    vertices.markers = {1, 0, 7};
    for (int first_number : {0, 1})
    {
        vertices.first_number = first_number;
        std::stringstream file;
        write_node(file, vertices);
        const VertexFile read = read_node(file, "test.node");
        EXPECT_EQ(read.first_number, first_number);
        ASSERT_EQ(read.vertices.size(), 3u);
        for (std::size_t vertex = 0; vertex < 3; vertex++)
        {
            EXPECT_EQ(read.vertices[vertex].x, vertices.vertices[vertex].x);
            EXPECT_EQ(read.vertices[vertex].y, vertices.vertices[vertex].y);
        }
        EXPECT_EQ(read.attribute_count, 2);
        EXPECT_EQ(read.attributes, vertices.attributes);
        EXPECT_EQ(read.markers, vertices.markers);
    }
}

TEST(TriangleFiles, EleAndNeighRoundTrip)
{
    VertexFile vertices;
    vertices.vertices = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
    TriangleFile triangles;
    triangles.triangles = {{0, 1, 2}, {0, 2, 3}};
    triangles.attribute_count = 1;
    triangles.attributes = {7.0, 8.5};
    const std::vector<std::array<int, 3>> neighbors = {{NO_NEIGHBOR, 1, NO_NEIGHBOR},
                                                       {NO_NEIGHBOR, NO_NEIGHBOR, 0}};
    for (int first_number : {0, 1})
    {
        vertices.first_number = first_number;
        std::stringstream ele;
        write_ele(ele, triangles, first_number);
        const TriangleFile read = read_ele(ele, "test.ele", vertices);
        EXPECT_EQ(read.triangles, triangles.triangles);
        EXPECT_EQ(read.attribute_count, 1);
        EXPECT_EQ(read.attributes, triangles.attributes);

        std::stringstream neigh;
        write_neigh(neigh, neighbors, first_number);
        EXPECT_EQ(read_neigh(neigh, "test.neigh", first_number, 2), neighbors);
    }
}

TEST(TriangleFiles, PolyRoundTrip)
{
    std::istringstream input(BOX_POLY);
    const PolyFile box = read_poly(input, "box.poly");
    EXPECT_EQ(box.vertices.first_number, 1);
    ASSERT_EQ(box.vertices.vertices.size(), 8u);
    EXPECT_EQ(box.vertices.vertices[3].x, 3.0);
    EXPECT_EQ(box.vertices.markers[3], 33);
    ASSERT_EQ(box.segments.size(), 5u);
    EXPECT_EQ(box.segments[1], (std::array<int, 2>{4, 6}));
    EXPECT_EQ(box.segment_markers[3], 10);
    ASSERT_EQ(box.holes.size(), 1u);
    EXPECT_EQ(box.holes[0].x, 1.5);
    EXPECT_TRUE(box.regions.empty());

    PolyFile with_regions = box;
    with_regions.regions = {{{0.5, 0.5}, 3.0, 0.1}};
    std::stringstream file;
    write_poly(file, with_regions);
    const PolyFile read = read_poly(file, "test.poly");
    EXPECT_EQ(read.vertices.markers, box.vertices.markers);
    EXPECT_EQ(read.segments, box.segments);
    EXPECT_EQ(read.segment_markers, box.segment_markers);
    ASSERT_EQ(read.holes.size(), 1u);
    EXPECT_EQ(read.holes[0].y, 1.5);
    ASSERT_EQ(read.regions.size(), 1u);
    EXPECT_EQ(read.regions[0].attribute, 3.0);
    EXPECT_EQ(read.regions[0].area_limit, 0.1);
}

TEST(TriangleFiles, PolyWithSeparateVertices)
{
    VertexFile vertices;
    vertices.vertices = {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}};
    vertices.first_number = 0;
    PolyFile poly;
    poly.segments = {{0, 1}, {1, 2}, {2, 0}};
    std::stringstream file;
    write_poly(file, poly, 0);
    std::stringstream copy(file.str());
    const PolyFile read = read_poly(file, "test.poly", &vertices);
    EXPECT_TRUE(read.vertices.vertices.empty());
    EXPECT_EQ(read.segments, poly.segments);
    EXPECT_EQ(error_message([&] { read_poly(copy, "test.poly"); }),
              "test.poly has no vertices, and no .node file was given");
}

TEST(TriangleFiles, TriangleNumberingAndMissingValues)
{
    // a first vertex other than 0 or 1 leaves the numbering at 1; missing
    // attributes and markers are 0
    const VertexFile vertices = read_node_text("3 2 1 1\n5 0 0\r\n6 1 0 2.5\n\n7 0 1 1 9\n");
    EXPECT_EQ(vertices.first_number, 1);
    EXPECT_EQ(vertices.attributes, (std::vector<double>{0.0, 2.5, 1.0}));
    EXPECT_EQ(vertices.markers, (std::vector<int>{0, 0, 9}));

    // a missing maximum area takes the attribute's value
    std::istringstream poly("3 2 0 0\n0 0 0\n1 1 0\n2 0 1\n0 0\n0\n1\n0 0.2 0.2 4\n");
    const PolyFile read = read_poly(poly, "test.poly");
    EXPECT_EQ(read.vertices.first_number, 0);
    ASSERT_EQ(read.regions.size(), 1u);
    EXPECT_EQ(read.regions[0].area_limit, 4.0);
}

TEST(TriangleFiles, ShortHeadersAndTriangleValues)
{
    // as Triangle reads them: a header with only a count has dimension 2 and
    // no attributes or markers, any marker count but 0 means markers, and an
    // integer may have a sign
    const VertexFile vertices = read_node_text("3\n1 0 0\n2 1 0\n3 0 1\n");
    EXPECT_EQ(vertices.vertices.size(), 3u);
    EXPECT_EQ(vertices.attribute_count, 0);
    EXPECT_TRUE(vertices.markers.empty());
    EXPECT_EQ(read_node_text("1 2 0 2\n1 0 0 4\n").markers, (std::vector<int>{4}));

    // a byte order mark is skipped, and a triangle with 6 corners (o2) keeps
    // its first 3
    EXPECT_EQ(read_node_text("\xEF\xBB\xBF"
                             "1\n1 0 0\n")
                  .vertices.size(),
              1u);
    std::istringstream six_corners("1 6 1\n1 1 2 3 1 2 3 7.5\n");
    const TriangleFile quadratic = read_ele(six_corners, "test.ele", vertices);
    EXPECT_EQ(quadratic.triangles, (std::vector<std::array<int, 3>>{{0, 1, 2}}));
    EXPECT_EQ(quadratic.attributes, (std::vector<double>{7.5}));

    std::istringstream ele("1\n+1 1 2 3\n");
    EXPECT_EQ(read_ele(ele, "test.ele", vertices).triangles,
              (std::vector<std::array<int, 3>>{{0, 1, 2}}));
    std::istringstream poly("3\n1 0 0\n2 1 0\n3 0 1\n1\n1 1 2\n0\n");
    const PolyFile read = read_poly(poly, "test.poly");
    EXPECT_EQ(read.segments, (std::vector<std::array<int, 2>>{{0, 1}}));
    EXPECT_TRUE(read.segment_markers.empty());
}

TEST(TriangleFiles, WritersCheckSizes)
{
    VertexFile vertices;
    vertices.vertices = {{0.0, 0.0}, {1.0, 0.0}};
    vertices.attribute_count = 1;
    vertices.attributes = {1.0};
    std::ostringstream output;
    EXPECT_EQ(error_message([&] { write_node(output, vertices); }),
              "there are 1 vertex attributes, but 2 are needed");
    vertices.attributes = {1.0, 2.0};
    vertices.markers = {1};
    EXPECT_EQ(error_message([&] { write_node(output, vertices); }),
              "there are 1 vertex markers, but 2 are needed");
}

TEST(TriangleFiles, ErrorsNameTheFileAndLine)
{
    EXPECT_EQ(error_message([] { read_node_text("1 3 0 0\n1 0 0\n"); }),
              "test.node line 1: the dimension must be 2");
    EXPECT_EQ(error_message([] { read_node_text("2 2 0 0\n1 0 0\n"); }),
              "test.node ends before a vertex");
    EXPECT_EQ(error_message([] { read_node_text("1 2 0 0\n# comment\n1 0 x\n"); }),
              "test.node line 3: the y coordinate \"x\" is not a number");
    EXPECT_EQ(error_message([] { read_node_text("1 2 0 0\n1 0 nan\n"); }),
              "test.node line 2: the y coordinate \"nan\" is not finite");
    EXPECT_EQ(error_message([] { read_node_text("1 2 0 0\n1.0 0 0\n"); }),
              "test.node line 2: the vertex number \"1.0\" is not an integer");
    // the largest count is read, and one more is an error
    EXPECT_EQ(error_message([] { read_node_text("715827882 2 0 0\n"); }),
              "test.node ends before a vertex");
    EXPECT_EQ(error_message([] { read_node_text("715827883 2 0 0\n"); }),
              "test.node line 1: the vertex count is 715827883, but it must be 0 to 715827882");
    EXPECT_EQ(error_message([] { read_node_text("-1 2 0 0\n"); }),
              "test.node line 1: the vertex count is -1, but it must be 0 to 715827882");

    const VertexFile vertices = read_node_text("3 2 0 0\n1 0 0\n2 1 0\n3 0 1\n");
    std::istringstream ele("1 3 0\n1 1 2 4\n");
    EXPECT_EQ(error_message([&] { read_ele(ele, "test.ele", vertices); }),
              "test.ele line 2: a triangle corner refers to vertex 4, but the vertices are 1 to 3");
    std::istringstream two_corners("1 2 0\n");
    EXPECT_EQ(error_message([&] { read_ele(two_corners, "test.ele", vertices); }),
              "test.ele line 1: the corner count must be 3 to 6");
    std::istringstream overflow("2147483648 3 0\n");
    EXPECT_EQ(error_message([&] { read_ele(overflow, "test.ele", vertices); }),
              "test.ele line 1: the triangle count \"2147483648\" is too large");
    std::istringstream neigh("1 3\n1 -1 2 -1\n");
    EXPECT_EQ(error_message([&] { read_neigh(neigh, "test.neigh", 1, 1); }),
              "test.neigh line 2: neighbor 2 is not a triangle of the file");
    std::istringstream wrong_count("2 3\n");
    EXPECT_EQ(error_message([&] { read_neigh(wrong_count, "test.neigh", 1, 1); }),
              "test.neigh line 1: the file has 2 triangles, but the mesh has 1");
}
