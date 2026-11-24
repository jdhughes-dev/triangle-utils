// The C API: each function catches the core's exceptions and returns a status.

#include "triangle_utils.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

#include "error.hpp"
#include "mesh.hpp"

struct TriangleUtilsMesh
{
    triangle_utils::Mesh mesh;
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
