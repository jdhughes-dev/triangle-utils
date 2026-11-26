// Tests the layers of a grid: values at cell centers follow a linear field,
// layers check their elevations and make thin cells pass-through, and their
// DISU data number the active cells layer by layer with symmetric
// connections that pass through pass-through cells and stop at inactive ones.

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "connections.hpp"
#include "error.hpp"
#include "geometry.hpp"
#include "grid.hpp"
#include "layers.hpp"

using namespace triangle_utils;

namespace
{

constexpr double TOLERANCE = 1e-9;

// A rectangle meshed with conforming Delaunay triangles.
Mesh rectangle_mesh()
{
    Geometry geometry;
    geometry.polygons.push_back({{{0.0, 0.0}, {10.0, 0.0}, {10.0, 4.0}, {0.0, 4.0}}, 0.0, 1});
    MeshOptions options;
    options.maximum_area = 1.0;
    options.minimum_angle = 28.0;
    options.conforming_delaunay = true;
    return generate_mesh(geometry, options);
}

double plane(const Point & p)
{
    return 10.0 + 0.3 * p.x - 0.2 * p.y;
}

// The message of the Error an action throws, or "" if it throws none.
template <typename Action> std::string error_of(Action && action)
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

double cell_area(const Grid & grid, int cell)
{
    double sum = 0.0;
    const auto & corners = grid.cells[cell];
    for (std::size_t i = 0; i < corners.size(); i++)
    {
        const Point & a = grid.vertices[corners[i]];
        const Point & b = grid.vertices[corners[(i + 1) % corners.size()]];
        sum += a.x * b.y - b.x * a.y;
    }
    return 0.5 * std::abs(sum);
}

} // namespace

TEST(Layers, ValuesAtCentersFollowALinearField)
{
    const Mesh mesh = rectangle_mesh();
    std::vector<double> values;
    for (const Point & vertex : mesh.vertices) values.push_back(plane(vertex));
    for (const Grid & grid : {voronoi_grid(mesh), triangle_grid(mesh, TriangleCenter::CENTROID),
                              triangle_grid(mesh, TriangleCenter::CIRCUMCENTER)})
    {
        const auto at_centers = values_at_centers(grid, mesh, values);
        ASSERT_EQ(at_centers.size(), grid.cells.size());
        for (std::size_t cell = 0; cell < grid.cells.size(); cell++)
            EXPECT_NEAR(at_centers[cell], plane(grid.centers[cell]), TOLERANCE);
    }
    // a Voronoi cell gets its own vertex's value exactly
    const Grid grid = voronoi_grid(mesh);
    const auto at_centers = values_at_centers(grid, mesh, values);
    for (std::size_t cell = 0; cell < grid.cells.size(); cell++)
        EXPECT_EQ(at_centers[cell], values[grid.sources[cell]]);

    EXPECT_NE(error_of([&] { values_at_centers(grid, mesh, {1.0}); }).find("values"),
              std::string::npos);
    values[0] = NAN;
    EXPECT_NE(error_of([&] { values_at_centers(grid, mesh, values); }).find("not finite"),
              std::string::npos);
    Grid outside = grid;
    outside.centers[3] = {20.0, 2.0};
    values[0] = 0.0;
    EXPECT_NE(error_of([&] { values_at_centers(outside, mesh, values); })
                  .find("the center of cell 3 is outside the mesh"),
              std::string::npos);
}

TEST(Layers, ElevationsAreChecked)
{
    const Grid grid = voronoi_grid(rectangle_mesh());
    const std::size_t n = grid.cells.size();
    const std::vector<double> top(n, 10.0);
    std::vector<double> bottoms(2 * n, 5.0);
    for (std::size_t cell = 0; cell < n; cell++) bottoms[n + cell] = 0.0;

    const Layers layers = layer_grid(grid, 2, top, bottoms, {}, 0.0);
    EXPECT_EQ(layers.layer_count, 2);
    EXPECT_EQ(layers.idomain, std::vector<int>(2 * n, 1));

    EXPECT_NE(error_of([&] { layer_grid(grid, 0, top, bottoms, {}, 0.0); }).find("layer count"),
              std::string::npos);
    EXPECT_NE(error_of([&] { layer_grid(grid, 3, top, bottoms, {}, 0.0); }).find("bottom values"),
              std::string::npos);
    EXPECT_NE(error_of([&] { layer_grid(grid, 2, {1.0}, bottoms, {}, 0.0); }).find("top values"),
              std::string::npos);
    EXPECT_NE(error_of([&] { layer_grid(grid, 2, top, bottoms, {1}, 0.0); }).find("idomain"),
              std::string::npos);
    EXPECT_NE(error_of([&] { layer_grid(grid, 2, top, bottoms, {}, -1.0); })
                  .find("minimum thickness"),
              std::string::npos);
    auto infinite = bottoms;
    infinite[n + 2] = INFINITY;
    EXPECT_NE(error_of([&] { layer_grid(grid, 2, top, infinite, {}, 0.0); }).find("not finite"),
              std::string::npos);

    // the second layer's bottom crosses the first's in cell 2, and meets it
    // in cell 4
    auto crossing = bottoms;
    crossing[n + 2] = 6.0;
    crossing[n + 4] = 5.0;
    EXPECT_NE(error_of([&] { layer_grid(grid, 2, top, crossing, {}, 0.0); })
                  .find("cell 2 in layer 1 has a top of 5 and a bottom of 6"),
              std::string::npos);
    // not if the cells are inactive
    std::vector<int> idomain(2 * n, 1);
    idomain[n + 2] = 0;
    idomain[n + 4] = 0;
    EXPECT_EQ(layer_grid(grid, 2, top, crossing, idomain, 0.0).idomain, idomain);
    // or with a minimum thickness, which makes them and a thin cell
    // pass-through
    crossing[n + 5] = 4.5;
    const Layers thin = layer_grid(grid, 2, top, crossing, {}, 1.0);
    for (std::size_t i = 0; i < 2 * n; i++)
    {
        const bool made = i == n + 2 || i == n + 4 || i == n + 5;
        EXPECT_EQ(thin.idomain[i], made ? -1 : 1) << i;
    }
}

TEST(Layers, DisuConnectsActiveCells)
{
    const Grid grid = voronoi_grid(rectangle_mesh());
    const Connections connections = connect_cells(grid);
    const int n = static_cast<int>(grid.cells.size());
    const int layer_count = 4;
    std::vector<double> top(n, 10.0);
    std::vector<double> bottoms;
    for (int layer = 0; layer < layer_count; layer++)
        for (int cell = 0; cell < n; cell++) bottoms.push_back(8.0 - 2.0 * layer - 0.01 * cell);
    std::vector<int> idomain(layer_count * n, 1);
    // cell 0 passes through layers 1 and 2; cell 1 is inactive in layer 1 and
    // passes through layer 2; cell 2 passes through the top layer
    idomain[n] = idomain[2 * n] = -1;
    idomain[n + 1] = 0;
    idomain[2 * n + 1] = -1;
    idomain[2] = -1;
    const Layers layers = layer_grid(grid, layer_count, top, bottoms, idomain, 0.0);
    const Disu disu = layer_disu(grid, connections, layers);

    // a node for each active cell, layer by layer
    std::vector<int> active;
    for (int i = 0; i < layer_count * n; i++)
        if (idomain[i] > 0) active.push_back(i);
    ASSERT_EQ(disu.cells, active);
    const std::size_t node_count = active.size();
    ASSERT_EQ(disu.offsets.size(), node_count + 1);
    std::map<int, int> node_of;
    for (std::size_t node = 0; node < node_count; node++) node_of[active[node]] = node;

    std::map<std::pair<int, int>, std::size_t> position;
    for (std::size_t node = 0; node < node_count; node++)
    {
        const int i = disu.cells[node];
        const int cell = i % n;
        const int layer = i / n;
        const double cell_top = layer == 0 ? top[cell] : bottoms[i - n];
        EXPECT_EQ(disu.top[node], cell_top);
        EXPECT_EQ(disu.bottom[node], bottoms[i]);
        EXPECT_NEAR(disu.area[node], cell_area(grid, cell), TOLERANCE);
        const int start = disu.offsets[node];
        const int end = disu.offsets[node + 1];
        ASSERT_EQ(disu.nodes[start], static_cast<int>(node));
        EXPECT_EQ(disu.distances[start], 0.0);
        EXPECT_TRUE(std::is_sorted(disu.nodes.begin() + start + 1, disu.nodes.begin() + end));
        std::size_t horizontal = 0;
        for (int c = start + 1; c < end; c++)
        {
            position[{static_cast<int>(node), disu.nodes[c]}] = c;
            const int other = disu.cells[disu.nodes[c]];
            if (disu.directions[c] == 0)
            {
                EXPECT_EQ(other % n, cell);
                EXPECT_NEAR(disu.distances[c], 0.5 * (cell_top - bottoms[i]), TOLERANCE);
                EXPECT_NEAR(disu.widths[c], disu.area[node], TOLERANCE);
            }
            else
            {
                EXPECT_EQ(other / n, layer);
                horizontal++;
            }
        }
        // the cell's active neighbors in its layer
        std::size_t expected = 0;
        for (int c = connections.offsets[cell]; c < connections.offsets[cell + 1]; c++)
            if (idomain[layer * n + connections.cells[c]] > 0) expected++;
        EXPECT_EQ(horizontal, expected);
    }
    // symmetric, each width the same from both nodes
    for (const auto & [pair, c] : position)
    {
        const auto back = position.find({pair.second, pair.first});
        ASSERT_NE(back, position.end());
        EXPECT_EQ(disu.directions[c], disu.directions[back->second]);
        EXPECT_NEAR(disu.widths[c], disu.widths[back->second], TOLERANCE);
    }
    // through the pass-through cells, but not past an inactive one
    EXPECT_TRUE(position.count({node_of[0], node_of[3 * n]}));
    EXPECT_FALSE(position.count({node_of[1], node_of[3 * n + 1]}));
    // a cell below a pass-through cell in the top layer connects only down,
    // and its top is the pass-through cell's bottom
    const int below = node_of[n + 2];
    int vertical = 0;
    for (int c = disu.offsets[below] + 1; c < disu.offsets[below + 1]; c++)
        if (disu.directions[c] == 0)
        {
            EXPECT_EQ(disu.nodes[c], node_of[2 * n + 2]);
            vertical++;
        }
    EXPECT_EQ(vertical, 1);
    EXPECT_EQ(disu.top[below], bottoms[2]);
}
