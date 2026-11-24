/* The triangle-utils C API.

   Each function that can fail returns a status code; after a status other
   than TRIANGLE_UTILS_OK, triangle_utils_last_error() gives the reason.
   The version is the API version: before 1.0.0 a minor version may change
   the API incompatibly, and from 1.0.0 on only a major version does.
   Calls must not run concurrently, since Triangle keeps global state, and
   the library turns off Triangle's printing for the whole process.
   Indices are zero based. */

#ifndef TRIANGLE_UTILS_H
#define TRIANGLE_UTILS_H

#include "triangle_utils_version.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* status codes */
#define TRIANGLE_UTILS_OK 0
/* the input is not valid, or Triangle could not mesh it */
#define TRIANGLE_UTILS_ERROR 1
/* a null pointer, a negative count, or another argument that is not valid */
#define TRIANGLE_UTILS_INVALID_ARGUMENT 2
#define TRIANGLE_UTILS_OUT_OF_MEMORY 3
/* an error in triangle-utils itself */
#define TRIANGLE_UTILS_INTERNAL_ERROR 4

/* A mesh; create it with triangle_utils_mesh_create() and free it with
   triangle_utils_mesh_free(). */
typedef struct TriangleUtilsMesh TriangleUtilsMesh;

/* The reason for the last status other than TRIANGLE_UTILS_OK, or an empty
   string after a function that returns a status succeeded; valid until the
   next such call. */
const char * triangle_utils_last_error(void);

/* The library's version, such as "0.1.0". */
const char * triangle_utils_version(void);

/* The library's version as numbers, to compare with the
   TRIANGLE_UTILS_VERSION macros of the header a caller was built with. */
void triangle_utils_version_numbers(int * major, int * minor, int * patch);

/* Triangle's version, "1.6". */
const char * triangle_utils_triangle_version(void);

/* The version of MODFLOW-ORG/triangle the library was built with; a build
   from a commit after a release reports that release. */
const char * triangle_utils_triangle_release(void);

/* The license of triangle-utils (CC0 1.0). */
const char * triangle_utils_license(void);

/* Triangle's copyright notice and terms of use, which a program that
   contains the library must make available to its users. */
const char * triangle_utils_triangle_copyright(void);

/* Create a mesh from vertex_count vertices (x and y of each in turn) and
   triangle_count triangles (three corners of each in turn,
   counterclockwise); each coordinate must be finite and each triangle must
   have a positive area. The arrays are copied. *mesh is NULL after a
   failure. */
int triangle_utils_mesh_create(int vertex_count, const double * vertex_xy, int triangle_count,
                               const int * triangle_corners, TriangleUtilsMesh ** mesh);

/* Free a mesh; NULL is ignored. */
void triangle_utils_mesh_free(TriangleUtilsMesh * mesh);

/* The number of vertices and triangles of a mesh; unchanged after a
   failure. */
int triangle_utils_mesh_counts(const TriangleUtilsMesh * mesh, int * vertex_count,
                               int * triangle_count);

#ifdef __cplusplus
}
#endif

#endif
