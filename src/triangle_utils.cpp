// The C API: each function catches the core's exceptions and returns a status.

#include "triangle_utils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "connections.hpp"
#include "error.hpp"
#include "features.hpp"
#include "geometry.hpp"
#include "grid.hpp"
#include "improve.hpp"
#include "layers.hpp"
#include "mesh.hpp"
#include "refine.hpp"

struct TriangleUtilsMesh
{
    triangle_utils::Mesh mesh;
};

struct TriangleUtilsGrid
{
    triangle_utils::Grid grid;
};

struct TriangleUtilsCellLengths
{
    std::vector<triangle_utils::CellLength> cell_lengths;
};

struct TriangleUtilsConnections
{
    triangle_utils::Connections connections;
};

struct TriangleUtilsLayers
{
    triangle_utils::Layers layers;
    triangle_utils::Disu disu;
};

struct TriangleUtilsGeometry
{
    triangle_utils::Geometry geometry;
};

namespace
{

// the longest reason kept, with room for the null
constexpr std::size_t LAST_ERROR_SIZE = 1024;
// a fixed array, so that keeping a reason cannot fail
char last_error[LAST_ERROR_SIZE] = "";

// An argument that is not valid, such as a null pointer.
class ArgumentError : public std::runtime_error
{
public:
    explicit ArgumentError(const std::string & message) : std::runtime_error(message) {}
};

void set_last_error(const char * reason)
{
    std::strncpy(last_error, reason, LAST_ERROR_SIZE - 1);
    last_error[LAST_ERROR_SIZE - 1] = '\0';
}

// Run an action and return its status, keeping the reason for an error.
template <typename Action> int call(Action && action) noexcept
{
    try
    {
        action();
        last_error[0] = '\0';
        return TRIANGLE_UTILS_OK;
    }
    catch (const ArgumentError & error)
    {
        set_last_error(error.what());
        return TRIANGLE_UTILS_INVALID_ARGUMENT;
    }
    catch (const triangle_utils::Error & error)
    {
        set_last_error(error.what());
        return TRIANGLE_UTILS_ERROR;
    }
    catch (const std::bad_alloc &)
    {
        set_last_error("there is not enough memory");
        return TRIANGLE_UTILS_OUT_OF_MEMORY;
    }
    catch (const std::exception & error)
    {
        set_last_error(error.what());
        return TRIANGLE_UTILS_INTERNAL_ERROR;
    }
    catch (...)
    {
        set_last_error("an unknown error occurred in triangle-utils");
        return TRIANGLE_UTILS_INTERNAL_ERROR;
    }
}

void check_pointer(const void * pointer, const char * name)
{
    if (pointer == nullptr)
    {
        throw ArgumentError(triangle_utils::message("the ", name, " pointer is null"));
    }
}

void check_count(int count, const char * name)
{
    if (count < 0)
    {
        throw ArgumentError(
            triangle_utils::message("the ", name, " count is negative (", count, ")"));
    }
}

} // namespace

// C linkage comes from the declarations in triangle_utils.h

const char * triangle_utils_last_error(void)
{
    return last_error;
}

const char * triangle_utils_version(void)
{
    return TRIANGLE_UTILS_VERSION;
}

void triangle_utils_version_numbers(int * major, int * minor, int * patch)
{
    if (major != nullptr) *major = TRIANGLE_UTILS_VERSION_MAJOR;
    if (minor != nullptr) *minor = TRIANGLE_UTILS_VERSION_MINOR;
    if (patch != nullptr) *patch = TRIANGLE_UTILS_VERSION_PATCH;
}

const char * triangle_utils_triangle_version(void)
{
    return triangle_version();
}

const char * triangle_utils_triangle_release(void)
{
    return triangle_release_version();
}

const char * triangle_utils_license(void)
{
    return "triangle-utils\n"
           "\n"
           "The code of triangle-utils is dedicated to the public domain under\n"
           "Creative Commons CC0 1.0 Universal:\n"
           "https://creativecommons.org/publicdomain/zero/1.0/\n"
           "It contains Triangle, which is under its own terms\n"
           "(triangle_utils_triangle_copyright()).\n";
}

const char * triangle_utils_triangle_copyright(void)
{
    return triangle_copyright();
}

int triangle_utils_mesh_create(int vertex_count, const double * vertex_xy, int triangle_count,
                               const int * triangle_corners, TriangleUtilsMesh ** mesh)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            *mesh = nullptr;
            check_count(vertex_count, "vertex");
            check_count(triangle_count, "triangle");
            if (vertex_count > 0) check_pointer(vertex_xy, "vertex coordinate");
            if (triangle_count > 0) check_pointer(triangle_corners, "triangle corner");

            auto created = std::make_unique<TriangleUtilsMesh>();
            auto & vertices = created->mesh.vertices;
            vertices.resize(static_cast<std::size_t>(vertex_count));
            for (std::size_t vertex = 0; vertex < vertices.size(); vertex++)
            {
                vertices[vertex] = {vertex_xy[2 * vertex], vertex_xy[2 * vertex + 1]};
                if (!std::isfinite(vertices[vertex].x) || !std::isfinite(vertices[vertex].y))
                {
                    throw triangle_utils::Error(triangle_utils::message(
                        "vertex ", vertex, " has a coordinate that is not finite"));
                }
            }
            auto & triangles = created->mesh.triangles;
            triangles.resize(static_cast<std::size_t>(triangle_count));
            for (std::size_t triangle = 0; triangle < triangles.size(); triangle++)
            {
                for (std::size_t corner = 0; corner < 3; corner++)
                {
                    triangles[triangle][corner] = triangle_corners[3 * triangle + corner];
                }
            }
            triangle_utils::check_sizes(created->mesh);
            triangle_utils::check_triangles(created->mesh);
            *mesh = created.release();
        });
}

int triangle_utils_mesh_set_neighbors(TriangleUtilsMesh * mesh, int triangle_count,
                                      const int * neighbors)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_count(triangle_count, "triangle");
            if (static_cast<std::size_t>(triangle_count) != mesh->mesh.triangles.size())
            {
                throw ArgumentError(triangle_utils::message(
                    "the triangle count is ", triangle_count, ", but the mesh has ",
                    mesh->mesh.triangles.size(), " triangles"));
            }
            if (triangle_count > 0) check_pointer(neighbors, "neighbor");
            triangle_utils::Mesh checked = mesh->mesh;
            checked.neighbors.resize(static_cast<std::size_t>(triangle_count));
            for (std::size_t triangle = 0; triangle < checked.neighbors.size(); triangle++)
            {
                for (std::size_t corner = 0; corner < 3; corner++)
                {
                    checked.neighbors[triangle][corner] = neighbors[3 * triangle + corner];
                }
            }
            triangle_utils::check_neighbors(checked);
            mesh->mesh.neighbors = std::move(checked.neighbors);
        });
}

void triangle_utils_mesh_free(TriangleUtilsMesh * mesh)
{
    delete mesh;
}

int triangle_utils_mesh_counts(const TriangleUtilsMesh * mesh, int * vertex_count,
                               int * triangle_count)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_pointer(vertex_count, "vertex count");
            check_pointer(triangle_count, "triangle count");
            // check_sizes limits the counts to what an int holds
            *vertex_count = static_cast<int>(mesh->mesh.vertices.size());
            *triangle_count = static_cast<int>(mesh->mesh.triangles.size());
        });
}

namespace
{

// Throw ArgumentError if a count the caller gives is not the mesh's.
void check_matches(int given, std::size_t actual, const char * name, const char * owner = "mesh")
{
    check_count(given, name);
    if (static_cast<std::size_t>(given) != actual)
    {
        throw ArgumentError(triangle_utils::message("the ", name, " count is ", given, ", but the ",
                                                    owner, " has ", actual));
    }
}

// Copy point_count points from x and y in turn.
std::vector<triangle_utils::Point> copy_points(int point_count, const double * point_xy)
{
    check_count(point_count, "point");
    if (point_count > 0) check_pointer(point_xy, "point coordinate");
    std::vector<triangle_utils::Point> points(static_cast<std::size_t>(point_count));
    for (std::size_t point = 0; point < points.size(); point++)
    {
        points[point] = {point_xy[2 * point], point_xy[2 * point + 1]};
    }
    return points;
}

} // namespace

int triangle_utils_mesh_vertices(const TriangleUtilsMesh * mesh, int vertex_count,
                                 double * vertex_xy)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_matches(vertex_count, mesh->mesh.vertices.size(), "vertex");
            if (vertex_count > 0) check_pointer(vertex_xy, "vertex coordinate");
            for (std::size_t vertex = 0; vertex < mesh->mesh.vertices.size(); vertex++)
            {
                vertex_xy[2 * vertex] = mesh->mesh.vertices[vertex].x;
                vertex_xy[2 * vertex + 1] = mesh->mesh.vertices[vertex].y;
            }
        });
}

int triangle_utils_mesh_triangles(const TriangleUtilsMesh * mesh, int triangle_count,
                                  int * triangle_corners)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_matches(triangle_count, mesh->mesh.triangles.size(), "triangle");
            if (triangle_count > 0) check_pointer(triangle_corners, "triangle corner");
            for (std::size_t triangle = 0; triangle < mesh->mesh.triangles.size(); triangle++)
            {
                for (std::size_t corner = 0; corner < 3; corner++)
                {
                    triangle_corners[3 * triangle + corner] =
                        mesh->mesh.triangles[triangle][corner];
                }
            }
        });
}

int triangle_utils_mesh_triangle_attribute_count(const TriangleUtilsMesh * mesh,
                                                 int * attribute_count)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_pointer(attribute_count, "attribute count");
            *attribute_count = mesh->mesh.triangle_attribute_count;
        });
}

int triangle_utils_mesh_triangle_attributes(const TriangleUtilsMesh * mesh, int value_count,
                                            double * attributes)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_matches(value_count, mesh->mesh.triangle_attributes.size(), "attribute value");
            if (value_count > 0) check_pointer(attributes, "attribute");
            std::copy(mesh->mesh.triangle_attributes.begin(), mesh->mesh.triangle_attributes.end(),
                      attributes);
        });
}

int triangle_utils_mesh_neighbors(const TriangleUtilsMesh * mesh, int triangle_count,
                                  int * neighbors)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_matches(triangle_count, mesh->mesh.triangles.size(), "triangle");
            if (mesh->mesh.neighbors.size() != mesh->mesh.triangles.size())
                throw triangle_utils::Error("the mesh has no neighbors");
            if (triangle_count > 0) check_pointer(neighbors, "neighbor");
            for (std::size_t triangle = 0; triangle < mesh->mesh.neighbors.size(); triangle++)
            {
                for (std::size_t corner = 0; corner < 3; corner++)
                {
                    neighbors[3 * triangle + corner] = mesh->mesh.neighbors[triangle][corner];
                }
            }
        });
}

int triangle_utils_mesh_segment_count(const TriangleUtilsMesh * mesh, int * segment_count)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_pointer(segment_count, "segment count");
            *segment_count = static_cast<int>(mesh->mesh.segments.size());
        });
}

int triangle_utils_mesh_segments(const TriangleUtilsMesh * mesh, int segment_count, int * ends,
                                 int * markers)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_matches(segment_count, mesh->mesh.segments.size(), "segment");
            if (segment_count > 0) check_pointer(ends, "segment end");
            const auto & segments = mesh->mesh.segments;
            const auto & segment_markers = mesh->mesh.segment_markers;
            for (std::size_t segment = 0; segment < segments.size(); segment++)
            {
                ends[2 * segment] = segments[segment][0];
                ends[2 * segment + 1] = segments[segment][1];
                if (markers != nullptr)
                    markers[segment] = segment_markers.empty() ? 0 : segment_markers[segment];
            }
        });
}

int triangle_utils_geometry_create(TriangleUtilsGeometry ** geometry)
{
    return call(
        [&]
        {
            check_pointer(geometry, "geometry");
            *geometry = nullptr;
            *geometry = new TriangleUtilsGeometry();
        });
}

void triangle_utils_geometry_free(TriangleUtilsGeometry * geometry)
{
    delete geometry;
}

namespace
{

// Add a chain to one of the geometry's lists.
int add_chain(TriangleUtilsGeometry * geometry,
              std::vector<triangle_utils::Chain> triangle_utils::Geometry::* list, int point_count,
              const double * point_xy, double spacing, int marker)
{
    return call(
        [&]
        {
            check_pointer(geometry, "geometry");
            (geometry->geometry.*list)
                .push_back({copy_points(point_count, point_xy), spacing, marker});
        });
}

} // namespace

int triangle_utils_geometry_add_polygon(TriangleUtilsGeometry * geometry, int point_count,
                                        const double * point_xy, double spacing, int marker)
{
    return add_chain(geometry, &triangle_utils::Geometry::polygons, point_count, point_xy, spacing,
                     marker);
}

int triangle_utils_geometry_add_hole_polygon(TriangleUtilsGeometry * geometry, int point_count,
                                             const double * point_xy, double spacing, int marker)
{
    return add_chain(geometry, &triangle_utils::Geometry::hole_polygons, point_count, point_xy,
                     spacing, marker);
}

int triangle_utils_geometry_add_line(TriangleUtilsGeometry * geometry, int point_count,
                                     const double * point_xy, double spacing, int marker)
{
    return add_chain(geometry, &triangle_utils::Geometry::lines, point_count, point_xy, spacing,
                     marker);
}

int triangle_utils_geometry_add_hole(TriangleUtilsGeometry * geometry, double x, double y)
{
    return call(
        [&]
        {
            check_pointer(geometry, "geometry");
            geometry->geometry.holes.push_back({x, y});
        });
}

int triangle_utils_geometry_add_region(TriangleUtilsGeometry * geometry, double x, double y,
                                       double attribute, double area_limit)
{
    return call(
        [&]
        {
            check_pointer(geometry, "geometry");
            geometry->geometry.regions.push_back({{x, y}, attribute, area_limit});
        });
}

int triangle_utils_geometry_add_points(TriangleUtilsGeometry * geometry, int point_count,
                                       const double * point_xy)
{
    return call(
        [&]
        {
            check_pointer(geometry, "geometry");
            const auto points = copy_points(point_count, point_xy);
            geometry->geometry.points.insert(geometry->geometry.points.end(), points.begin(),
                                             points.end());
        });
}

int triangle_utils_geometry_add_ring(TriangleUtilsGeometry * geometry, double x, double y,
                                     double radius, int point_count, double attribute,
                                     double area_limit, int marker)
{
    return call(
        [&]
        {
            check_pointer(geometry, "geometry");
            geometry->geometry.rings.push_back(
                {{x, y}, radius, point_count, attribute, area_limit, marker});
        });
}

int triangle_utils_mesh_generate(const TriangleUtilsGeometry * geometry, double maximum_area,
                                 double minimum_angle, double merge_distance,
                                 int conforming_delaunay, TriangleUtilsMesh ** mesh)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            *mesh = nullptr;
            check_pointer(geometry, "geometry");
            triangle_utils::MeshOptions options;
            options.maximum_area = maximum_area;
            options.minimum_angle = minimum_angle;
            options.merge_distance = merge_distance;
            options.conforming_delaunay = conforming_delaunay != 0;
            auto generated = std::make_unique<TriangleUtilsMesh>();
            generated->mesh = triangle_utils::generate_mesh(geometry->geometry, options);
            *mesh = generated.release();
        });
}

int triangle_utils_mesh_set_segments(TriangleUtilsMesh * mesh, int segment_count, const int * ends,
                                     const int * markers)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_count(segment_count, "segment");
            if (segment_count > 0) check_pointer(ends, "segment end");
            triangle_utils::Mesh checked = mesh->mesh;
            checked.segments.resize(static_cast<std::size_t>(segment_count));
            checked.segment_markers.clear();
            for (std::size_t segment = 0; segment < checked.segments.size(); segment++)
            {
                for (std::size_t end = 0; end < 2; end++)
                {
                    const int vertex = ends[2 * segment + end];
                    if (vertex < 0 || static_cast<std::size_t>(vertex) >= checked.vertices.size())
                        throw triangle_utils::Error(triangle_utils::message(
                            "segment ", segment, " refers to vertex ", vertex,
                            ", but the mesh has ", checked.vertices.size(), " vertices"));
                    checked.segments[segment][end] = vertex;
                }
                if (markers != nullptr) checked.segment_markers.push_back(markers[segment]);
            }
            triangle_utils::check_sizes(checked);
            mesh->mesh = std::move(checked);
        });
}

int triangle_utils_mesh_set_triangle_attributes(TriangleUtilsMesh * mesh, int triangle_count,
                                                int attribute_count, const double * attributes)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_matches(triangle_count, mesh->mesh.triangles.size(), "triangle");
            check_count(attribute_count, "attribute");
            const std::size_t value_count = static_cast<std::size_t>(triangle_count) *
                                            static_cast<std::size_t>(attribute_count);
            if (value_count > 0) check_pointer(attributes, "attribute");
            triangle_utils::Mesh checked = mesh->mesh;
            checked.triangle_attribute_count = attribute_count;
            checked.triangle_attributes.assign(attributes, attributes + value_count);
            for (const double value : checked.triangle_attributes)
                if (!std::isfinite(value))
                    throw triangle_utils::Error("a triangle attribute is not finite");
            triangle_utils::check_sizes(checked);
            mesh->mesh = std::move(checked);
        });
}

int triangle_utils_mesh_set_area_limits(TriangleUtilsMesh * mesh, int triangle_count,
                                        const double * area_limits)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_matches(triangle_count, mesh->mesh.triangles.size(), "triangle");
            if (triangle_count > 0) check_pointer(area_limits, "area limit");
            std::vector<double> limits(area_limits, area_limits + triangle_count);
            for (const double limit : limits)
                if (!std::isfinite(limit))
                    throw triangle_utils::Error("an area limit is not finite");
            mesh->mesh.triangle_area_limits = std::move(limits);
        });
}

int triangle_utils_mesh_refine(const TriangleUtilsMesh * mesh, double maximum_area,
                               double minimum_angle, double maximum_angle, int maximum_angle_splits,
                               int conforming_delaunay, TriangleUtilsMesh ** refined)
{
    return call(
        [&]
        {
            check_pointer(refined, "refined mesh");
            *refined = nullptr;
            check_pointer(mesh, "mesh");
            triangle_utils::RefineOptions options;
            options.maximum_area = maximum_area;
            options.minimum_angle = minimum_angle;
            options.maximum_angle = maximum_angle;
            options.maximum_angle_splits = maximum_angle_splits;
            options.conforming_delaunay = conforming_delaunay != 0;
            auto result = std::make_unique<TriangleUtilsMesh>();
            result->mesh = triangle_utils::refine_mesh(mesh->mesh, options);
            *refined = result.release();
        });
}

int triangle_utils_mesh_count_angles_over(const TriangleUtilsMesh * mesh, double limit, int * count)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_pointer(count, "count");
            // check_sizes limits the triangle count to what an int holds
            *count = static_cast<int>(triangle_utils::count_angles_over(mesh->mesh, limit));
        });
}

int triangle_utils_mesh_set_fixed_vertices(TriangleUtilsMesh * mesh, int fixed_count,
                                           const int * fixed_vertices)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_count(fixed_count, "fixed vertex");
            if (fixed_count > 0) check_pointer(fixed_vertices, "fixed vertex");
            triangle_utils::Mesh checked = mesh->mesh;
            checked.fixed_vertices.assign(fixed_vertices, fixed_vertices + fixed_count);
            triangle_utils::check_sizes(checked);
            mesh->mesh = std::move(checked);
        });
}

int triangle_utils_mesh_fixed_vertex_count(const TriangleUtilsMesh * mesh, int * fixed_count)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_pointer(fixed_count, "fixed vertex count");
            *fixed_count = static_cast<int>(mesh->mesh.fixed_vertices.size());
        });
}

int triangle_utils_mesh_fixed_vertices(const TriangleUtilsMesh * mesh, int fixed_count,
                                       int * fixed_vertices)
{
    return call(
        [&]
        {
            check_pointer(mesh, "mesh");
            check_matches(fixed_count, mesh->mesh.fixed_vertices.size(), "fixed vertex");
            if (fixed_count > 0) check_pointer(fixed_vertices, "fixed vertex");
            std::copy(mesh->mesh.fixed_vertices.begin(), mesh->mesh.fixed_vertices.end(),
                      fixed_vertices);
        });
}

int triangle_utils_mesh_improve(const TriangleUtilsMesh * mesh, int smoothing, double maximum_angle,
                                double minimum_angle, int iterations, double tolerance,
                                TriangleUtilsMesh ** improved)
{
    return call(
        [&]
        {
            check_pointer(improved, "improved mesh");
            *improved = nullptr;
            check_pointer(mesh, "mesh");
            triangle_utils::ImproveOptions options;
            if (smoothing == TRIANGLE_UTILS_LAPLACIAN)
                options.smoothing = triangle_utils::Smoothing::LAPLACIAN;
            else if (smoothing == TRIANGLE_UTILS_CENTROIDAL)
                options.smoothing = triangle_utils::Smoothing::CENTROIDAL;
            else
                throw ArgumentError(triangle_utils::message(
                    "the smoothing is ", smoothing, ", but it must be ", TRIANGLE_UTILS_LAPLACIAN,
                    " (Laplacian) or ", TRIANGLE_UTILS_CENTROIDAL, " (centroidal)"));
            options.maximum_angle = maximum_angle;
            options.minimum_angle = minimum_angle;
            options.iterations = iterations;
            options.tolerance = tolerance;
            auto result = std::make_unique<TriangleUtilsMesh>();
            result->mesh = triangle_utils::improve_mesh(mesh->mesh, options);
            *improved = result.release();
        });
}

int triangle_utils_mesh_voronoi(const TriangleUtilsMesh * mesh, TriangleUtilsGrid ** grid)
{
    return call(
        [&]
        {
            check_pointer(grid, "grid");
            *grid = nullptr;
            check_pointer(mesh, "mesh");
            auto result = std::make_unique<TriangleUtilsGrid>();
            result->grid = triangle_utils::voronoi_grid(mesh->mesh);
            *grid = result.release();
        });
}

int triangle_utils_mesh_triangle_grid(const TriangleUtilsMesh * mesh, int center,
                                      TriangleUtilsGrid ** grid)
{
    return call(
        [&]
        {
            check_pointer(grid, "grid");
            *grid = nullptr;
            check_pointer(mesh, "mesh");
            triangle_utils::TriangleCenter at;
            if (center == TRIANGLE_UTILS_CENTROID)
                at = triangle_utils::TriangleCenter::CENTROID;
            else if (center == TRIANGLE_UTILS_CIRCUMCENTER)
                at = triangle_utils::TriangleCenter::CIRCUMCENTER;
            else
                throw ArgumentError(triangle_utils::message(
                    "the center is ", center, ", but it must be ", TRIANGLE_UTILS_CENTROID,
                    " (centroid) or ", TRIANGLE_UTILS_CIRCUMCENTER, " (circumcenter)"));
            auto result = std::make_unique<TriangleUtilsGrid>();
            result->grid = triangle_utils::triangle_grid(mesh->mesh, at);
            *grid = result.release();
        });
}

void triangle_utils_grid_free(TriangleUtilsGrid * grid)
{
    delete grid;
}

namespace
{

std::size_t corner_total(const triangle_utils::Grid & grid)
{
    std::size_t total = 0;
    for (const auto & cell : grid.cells) total += cell.size();
    return total;
}

} // namespace

int triangle_utils_grid_counts(const TriangleUtilsGrid * grid, int * vertex_count, int * cell_count,
                               int * corner_count)
{
    return call(
        [&]
        {
            check_pointer(grid, "grid");
            check_pointer(vertex_count, "vertex count");
            check_pointer(cell_count, "cell count");
            check_pointer(corner_count, "corner count");
            // a grid's counts are bounded by its mesh's, which fit an int
            *vertex_count = static_cast<int>(grid->grid.vertices.size());
            *cell_count = static_cast<int>(grid->grid.cells.size());
            *corner_count = static_cast<int>(corner_total(grid->grid));
        });
}

int triangle_utils_grid_vertices(const TriangleUtilsGrid * grid, int vertex_count,
                                 double * vertex_xy)
{
    return call(
        [&]
        {
            check_pointer(grid, "grid");
            check_matches(vertex_count, grid->grid.vertices.size(), "vertex", "grid");
            if (vertex_count > 0) check_pointer(vertex_xy, "vertex coordinate");
            for (std::size_t vertex = 0; vertex < grid->grid.vertices.size(); vertex++)
            {
                vertex_xy[2 * vertex] = grid->grid.vertices[vertex].x;
                vertex_xy[2 * vertex + 1] = grid->grid.vertices[vertex].y;
            }
        });
}

int triangle_utils_grid_cells(const TriangleUtilsGrid * grid, int cell_count, int * corner_offsets,
                              int corner_count, int * corners)
{
    return call(
        [&]
        {
            check_pointer(grid, "grid");
            check_matches(cell_count, grid->grid.cells.size(), "cell", "grid");
            check_matches(corner_count, corner_total(grid->grid), "corner", "grid");
            check_pointer(corner_offsets, "corner offset");
            if (corner_count > 0) check_pointer(corners, "corner");
            int offset = 0;
            for (std::size_t cell = 0; cell < grid->grid.cells.size(); cell++)
            {
                corner_offsets[cell] = offset;
                for (const int corner : grid->grid.cells[cell]) corners[offset++] = corner;
            }
            corner_offsets[cell_count] = offset;
        });
}

int triangle_utils_grid_centers(const TriangleUtilsGrid * grid, int cell_count, double * center_xy)
{
    return call(
        [&]
        {
            check_pointer(grid, "grid");
            check_matches(cell_count, grid->grid.cells.size(), "cell", "grid");
            if (cell_count > 0) check_pointer(center_xy, "center coordinate");
            for (std::size_t cell = 0; cell < grid->grid.centers.size(); cell++)
            {
                center_xy[2 * cell] = grid->grid.centers[cell].x;
                center_xy[2 * cell + 1] = grid->grid.centers[cell].y;
            }
        });
}

int triangle_utils_grid_sources(const TriangleUtilsGrid * grid, int cell_count, int * sources)
{
    return call(
        [&]
        {
            check_pointer(grid, "grid");
            check_matches(cell_count, grid->grid.cells.size(), "cell", "grid");
            if (cell_count > 0) check_pointer(sources, "source");
            std::copy(grid->grid.sources.begin(), grid->grid.sources.end(), sources);
        });
}

int triangle_utils_grid_attribute_count(const TriangleUtilsGrid * grid, int * attribute_count)
{
    return call(
        [&]
        {
            check_pointer(grid, "grid");
            check_pointer(attribute_count, "attribute count");
            *attribute_count = grid->grid.attribute_count;
        });
}

int triangle_utils_grid_attributes(const TriangleUtilsGrid * grid, int value_count,
                                   double * attributes)
{
    return call(
        [&]
        {
            check_pointer(grid, "grid");
            check_matches(value_count, grid->grid.attributes.size(), "attribute value", "grid");
            if (value_count > 0) check_pointer(attributes, "attribute");
            std::copy(grid->grid.attributes.begin(), grid->grid.attributes.end(), attributes);
        });
}

int triangle_utils_grid_create(int vertex_count, const double * vertex_xy, int cell_count,
                               const int * corner_offsets, int corner_count, const int * corners,
                               const double * center_xy, TriangleUtilsGrid ** grid)
{
    return call(
        [&]
        {
            check_pointer(grid, "grid");
            *grid = nullptr;
            const auto vertices = copy_points(vertex_count, vertex_xy);
            check_count(cell_count, "cell");
            check_count(corner_count, "corner");
            check_pointer(corner_offsets, "corner offset");
            if (corner_count > 0) check_pointer(corners, "corner");
            if (corner_offsets[0] != 0 || corner_offsets[cell_count] != corner_count)
                throw ArgumentError(triangle_utils::message(
                    "the corner offsets run from ", corner_offsets[0], " to ",
                    corner_offsets[cell_count], ", but they must run from 0 to the corner count, ",
                    corner_count));
            auto created = std::make_unique<TriangleUtilsGrid>();
            created->grid.vertices = vertices;
            for (std::size_t cell = 0; cell < static_cast<std::size_t>(cell_count); cell++)
            {
                if (corner_offsets[cell + 1] < corner_offsets[cell])
                    throw ArgumentError(
                        triangle_utils::message("the corner offsets decrease at cell ", cell));
                created->grid.cells.emplace_back(corners + corner_offsets[cell],
                                                 corners + corner_offsets[cell + 1]);
            }
            created->grid.centers = copy_points(cell_count, center_xy);
            created->grid.sources.assign(static_cast<std::size_t>(cell_count), -1);
            triangle_utils::check_grid(created->grid);
            *grid = created.release();
        });
}

namespace
{

// Store cell lengths in a new handle.
TriangleUtilsCellLengths * store(std::vector<triangle_utils::CellLength> cell_lengths)
{
    auto stored = std::make_unique<TriangleUtilsCellLengths>();
    stored->cell_lengths = std::move(cell_lengths);
    return stored.release();
}

} // namespace

int triangle_utils_grid_line_cells(const TriangleUtilsGrid * grid, int point_count,
                                   const double * point_xy,
                                   TriangleUtilsCellLengths ** cell_lengths)
{
    return call(
        [&]
        {
            check_pointer(cell_lengths, "cell lengths");
            *cell_lengths = nullptr;
            check_pointer(grid, "grid");
            *cell_lengths = store(
                triangle_utils::cells_along_line(grid->grid, copy_points(point_count, point_xy)));
        });
}

int triangle_utils_grid_segment_cells(const TriangleUtilsGrid * grid,
                                      const TriangleUtilsMesh * mesh, int marker,
                                      TriangleUtilsCellLengths ** cell_lengths)
{
    return call(
        [&]
        {
            check_pointer(cell_lengths, "cell lengths");
            *cell_lengths = nullptr;
            check_pointer(grid, "grid");
            check_pointer(mesh, "mesh");
            *cell_lengths =
                store(triangle_utils::cells_on_segments(grid->grid, mesh->mesh, marker));
        });
}

void triangle_utils_cell_lengths_free(TriangleUtilsCellLengths * cell_lengths)
{
    delete cell_lengths;
}

int triangle_utils_cell_lengths_count(const TriangleUtilsCellLengths * cell_lengths, int * count)
{
    return call(
        [&]
        {
            check_pointer(cell_lengths, "cell lengths");
            check_pointer(count, "count");
            // bounded by the grid's cells and the line's points, which fit an int
            *count = static_cast<int>(cell_lengths->cell_lengths.size());
        });
}

int triangle_utils_cell_lengths(const TriangleUtilsCellLengths * cell_lengths, int count,
                                int * cells, double * starts, double * lengths)
{
    return call(
        [&]
        {
            check_pointer(cell_lengths, "cell lengths");
            check_matches(count, cell_lengths->cell_lengths.size(), "cell length", "result");
            if (count > 0)
            {
                check_pointer(cells, "cell");
                check_pointer(starts, "start");
                check_pointer(lengths, "length");
            }
            for (std::size_t i = 0; i < cell_lengths->cell_lengths.size(); i++)
            {
                cells[i] = cell_lengths->cell_lengths[i].cell;
                starts[i] = cell_lengths->cell_lengths[i].start;
                lengths[i] = cell_lengths->cell_lengths[i].length;
            }
        });
}

int triangle_utils_grid_point_cells(const TriangleUtilsGrid * grid, int point_count,
                                    const double * point_xy, int * cells)
{
    return call(
        [&]
        {
            check_pointer(grid, "grid");
            const auto points = copy_points(point_count, point_xy);
            if (point_count > 0) check_pointer(cells, "cell");
            const auto found = triangle_utils::cells_at_points(grid->grid, points);
            std::copy(found.begin(), found.end(), cells);
        });
}

int triangle_utils_grid_connections(const TriangleUtilsGrid * grid,
                                    TriangleUtilsConnections ** connections)
{
    return call(
        [&]
        {
            check_pointer(connections, "connections");
            *connections = nullptr;
            check_pointer(grid, "grid");
            auto stored = std::make_unique<TriangleUtilsConnections>();
            stored->connections = triangle_utils::connect_cells(grid->grid);
            *connections = stored.release();
        });
}

void triangle_utils_connections_free(TriangleUtilsConnections * connections)
{
    delete connections;
}

int triangle_utils_connections_count(const TriangleUtilsConnections * connections, int * cell_count,
                                     int * connection_count)
{
    return call(
        [&]
        {
            check_pointer(connections, "connections");
            check_pointer(cell_count, "cell count");
            check_pointer(connection_count, "connection count");
            // bounded by the grid's corners, which fit an int
            *cell_count = static_cast<int>(connections->connections.offsets.size() - 1);
            *connection_count = static_cast<int>(connections->connections.cells.size());
        });
}

int triangle_utils_connections(const TriangleUtilsConnections * connections, int cell_count,
                               int * offsets, int connection_count, int * cells, double * distances,
                               double * widths, double * angles)
{
    return call(
        [&]
        {
            check_pointer(connections, "connections");
            const auto & c = connections->connections;
            check_matches(cell_count, c.offsets.size() - 1, "cell", "result");
            check_matches(connection_count, c.cells.size(), "connection", "result");
            check_pointer(offsets, "offset");
            if (connection_count > 0)
            {
                check_pointer(cells, "cell");
                check_pointer(distances, "distance");
                check_pointer(widths, "width");
                check_pointer(angles, "angle");
            }
            std::copy(c.offsets.begin(), c.offsets.end(), offsets);
            std::copy(c.cells.begin(), c.cells.end(), cells);
            std::copy(c.distances.begin(), c.distances.end(), distances);
            std::copy(c.widths.begin(), c.widths.end(), widths);
            std::copy(c.angles.begin(), c.angles.end(), angles);
        });
}

int triangle_utils_grid_vertex_values(const TriangleUtilsGrid * grid,
                                      const TriangleUtilsMesh * mesh, int vertex_count,
                                      const double * vertex_values, int cell_count,
                                      double * cell_values)
{
    return call(
        [&]
        {
            check_pointer(grid, "grid");
            check_pointer(mesh, "mesh");
            check_matches(vertex_count, mesh->mesh.vertices.size(), "vertex");
            check_matches(cell_count, grid->grid.cells.size(), "cell", "grid");
            if (vertex_count > 0) check_pointer(vertex_values, "vertex value");
            if (cell_count > 0) check_pointer(cell_values, "cell value");
            const auto values = triangle_utils::values_at_centers(
                grid->grid, mesh->mesh,
                std::vector<double>(vertex_values, vertex_values + vertex_count));
            std::copy(values.begin(), values.end(), cell_values);
        });
}

int triangle_utils_grid_layers(const TriangleUtilsGrid * grid, int layer_count, int cell_count,
                               const double * top, const double * bottoms, const int * idomain,
                               double minimum_thickness, TriangleUtilsLayers ** layers)
{
    return call(
        [&]
        {
            check_pointer(layers, "layers");
            *layers = nullptr;
            check_pointer(grid, "grid");
            check_matches(cell_count, grid->grid.cells.size(), "cell", "grid");
            if (layer_count < 1)
                throw ArgumentError(
                    triangle_utils::message("the layer count is ", layer_count, ", not positive"));
            const std::size_t cells = static_cast<std::size_t>(cell_count);
            const std::size_t values = static_cast<std::size_t>(layer_count) * cells;
            if (cells > 0)
            {
                check_pointer(top, "top");
                check_pointer(bottoms, "bottom");
            }
            std::vector<int> domain;
            if (idomain != nullptr) domain.assign(idomain, idomain + values);
            auto stored = std::make_unique<TriangleUtilsLayers>();
            stored->layers = triangle_utils::layer_grid(
                grid->grid, layer_count, std::vector<double>(top, top + cells),
                std::vector<double>(bottoms, bottoms + values), domain, minimum_thickness);
            stored->disu = triangle_utils::layer_disu(
                grid->grid, triangle_utils::connect_cells(grid->grid), stored->layers);
            *layers = stored.release();
        });
}

void triangle_utils_layers_free(TriangleUtilsLayers * layers)
{
    delete layers;
}

int triangle_utils_layers_idomain(const TriangleUtilsLayers * layers, int value_count,
                                  int * idomain)
{
    return call(
        [&]
        {
            check_pointer(layers, "layers");
            const auto & values = layers->layers.idomain;
            check_matches(value_count, values.size(), "idomain value", "layers");
            if (value_count > 0) check_pointer(idomain, "idomain");
            std::copy(values.begin(), values.end(), idomain);
        });
}

int triangle_utils_layers_disu_count(const TriangleUtilsLayers * layers, int * node_count,
                                     int * connection_count)
{
    return call(
        [&]
        {
            check_pointer(layers, "layers");
            check_pointer(node_count, "node count");
            check_pointer(connection_count, "connection count");
            // layer_grid and layer_disu limit both to what an int holds
            *node_count = static_cast<int>(layers->disu.cells.size());
            *connection_count = static_cast<int>(layers->disu.nodes.size());
        });
}

int triangle_utils_layers_disu_nodes(const TriangleUtilsLayers * layers, int node_count,
                                     int * cells, double * top, double * bottom, double * area)
{
    return call(
        [&]
        {
            check_pointer(layers, "layers");
            const auto & d = layers->disu;
            check_matches(node_count, d.cells.size(), "node", "layers");
            if (node_count > 0)
            {
                check_pointer(cells, "cell");
                check_pointer(top, "top");
                check_pointer(bottom, "bottom");
                check_pointer(area, "area");
            }
            std::copy(d.cells.begin(), d.cells.end(), cells);
            std::copy(d.top.begin(), d.top.end(), top);
            std::copy(d.bottom.begin(), d.bottom.end(), bottom);
            std::copy(d.area.begin(), d.area.end(), area);
        });
}

int triangle_utils_layers_disu_connections(const TriangleUtilsLayers * layers, int node_count,
                                           int * offsets, int connection_count, int * nodes,
                                           int * directions, double * distances, double * widths,
                                           double * angles)
{
    return call(
        [&]
        {
            check_pointer(layers, "layers");
            const auto & d = layers->disu;
            check_matches(node_count, d.cells.size(), "node", "layers");
            check_matches(connection_count, d.nodes.size(), "connection", "layers");
            check_pointer(offsets, "offset");
            if (connection_count > 0)
            {
                check_pointer(nodes, "node");
                check_pointer(directions, "direction");
                check_pointer(distances, "distance");
                check_pointer(widths, "width");
                check_pointer(angles, "angle");
            }
            std::copy(d.offsets.begin(), d.offsets.end(), offsets);
            std::copy(d.nodes.begin(), d.nodes.end(), nodes);
            std::copy(d.directions.begin(), d.directions.end(), directions);
            std::copy(d.distances.begin(), d.distances.end(), distances);
            std::copy(d.widths.begin(), d.widths.end(), widths);
            std::copy(d.angles.begin(), d.angles.end(), angles);
        });
}
