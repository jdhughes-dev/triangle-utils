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

/* Geometry to mesh; create it with triangle_utils_geometry_create(), add to
   it, mesh it with triangle_utils_mesh_generate(), and free it with
   triangle_utils_geometry_free(). Points are x and y of each in turn. */
typedef struct TriangleUtilsGeometry TriangleUtilsGeometry;

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

/* Set the neighbors of a mesh's triangle_count triangles (the triangle
   opposite each corner in turn, or -1 for none); each must be the triangle
   across that edge. The array is copied, and the mesh is unchanged after a
   failure. */
int triangle_utils_mesh_set_neighbors(TriangleUtilsMesh * mesh, int triangle_count,
                                      const int * neighbors);

/* Set the segments of a mesh: segment_count pairs of vertices, and a marker
   for each if markers is not NULL. The arrays are copied, and the mesh is
   unchanged after a failure. */
int triangle_utils_mesh_set_segments(TriangleUtilsMesh * mesh, int segment_count, const int * ends,
                                     const int * markers);

/* Set the attributes of a mesh's triangle_count triangles, attribute_count of
   each in turn, such as a region's. The array is copied, and the mesh is
   unchanged after a failure. */
int triangle_utils_mesh_set_triangle_attributes(TriangleUtilsMesh * mesh, int triangle_count,
                                                int attribute_count, const double * attributes);

/* Set an area limit for each of a mesh's triangle_count triangles, for
   triangle_utils_mesh_refine(); a limit that is not positive is no limit.
   The array is copied, and the mesh is unchanged after a failure. */
int triangle_utils_mesh_set_area_limits(TriangleUtilsMesh * mesh, int triangle_count,
                                        const double * area_limits);

/* Free a mesh; NULL is ignored. */
void triangle_utils_mesh_free(TriangleUtilsMesh * mesh);

/* The number of vertices and triangles of a mesh; unchanged after a
   failure. */
int triangle_utils_mesh_counts(const TriangleUtilsMesh * mesh, int * vertex_count,
                               int * triangle_count);

/* The mesh's vertices, x and y of each in turn; vertex_count must be the
   mesh's. */
int triangle_utils_mesh_vertices(const TriangleUtilsMesh * mesh, int vertex_count,
                                 double * vertex_xy);

/* The mesh's triangles, three corners of each in turn, counterclockwise. */
int triangle_utils_mesh_triangles(const TriangleUtilsMesh * mesh, int triangle_count,
                                  int * triangle_corners);

/* The number of attributes of each triangle: 1 for a generated mesh with
   regions, the region's attribute, or 0. */
int triangle_utils_mesh_triangle_attribute_count(const TriangleUtilsMesh * mesh,
                                                 int * attribute_count);

/* The triangles' attributes, attribute_count of each in turn; value_count is
   the attribute count times the triangle count. */
int triangle_utils_mesh_triangle_attributes(const TriangleUtilsMesh * mesh, int value_count,
                                            double * attributes);

/* The mesh's neighbors (the triangle opposite each corner in turn, or -1);
   an error if the mesh has none. */
int triangle_utils_mesh_neighbors(const TriangleUtilsMesh * mesh, int triangle_count,
                                  int * neighbors);

/* The number of the mesh's segments. */
int triangle_utils_mesh_segment_count(const TriangleUtilsMesh * mesh, int * segment_count);

/* The mesh's segments, two ends of each in turn, and the marker of each, if
   markers is not NULL. */
int triangle_utils_mesh_segments(const TriangleUtilsMesh * mesh, int segment_count, int * ends,
                                 int * markers);

/* Create empty geometry; *geometry is NULL after a failure. */
int triangle_utils_geometry_create(TriangleUtilsGeometry ** geometry);

/* Free geometry; NULL is ignored. */
void triangle_utils_geometry_free(TriangleUtilsGeometry * geometry);

/* Add a polygon of point_count points, a boundary or an internal boundary
   the mesh follows. Points are added along it so none is farther than
   spacing from the next, unless spacing is not positive; its segments get
   the marker. */
int triangle_utils_geometry_add_polygon(TriangleUtilsGeometry * geometry, int point_count,
                                        const double * point_xy, double spacing, int marker);

/* Add a polygon whose inside is not meshed, as for
   triangle_utils_geometry_add_polygon(). */
int triangle_utils_geometry_add_hole_polygon(TriangleUtilsGeometry * geometry, int point_count,
                                             const double * point_xy, double spacing, int marker);

/* Add an open line the mesh follows, such as a river, as for
   triangle_utils_geometry_add_polygon(). */
int triangle_utils_geometry_add_line(TriangleUtilsGeometry * geometry, int point_count,
                                     const double * point_xy, double spacing, int marker);

/* Add a point inside a hole bounded by the polygons. */
int triangle_utils_geometry_add_hole(TriangleUtilsGeometry * geometry, double x, double y);

/* Add a region by a point inside it: its triangles get the attribute and
   are at most area_limit in area, or have no limit if it is not positive.
   A region spreads only up to segments, so a line across a polygon needs a
   region on each side; so does a ring's region. */
int triangle_utils_geometry_add_region(TriangleUtilsGeometry * geometry, double x, double y,
                                       double attribute, double area_limit);

/* Add point_count points the mesh includes as vertices. */
int triangle_utils_geometry_add_points(TriangleUtilsGeometry * geometry, int point_count,
                                       const double * point_xy);

/* Add a ring of finer triangles around a point, such as a well: a vertex at
   (x, y), a polygon of point_count points at the radius with the marker,
   and a region inside with the attribute and area limit. */
int triangle_utils_geometry_add_ring(TriangleUtilsGeometry * geometry, double x, double y,
                                     double radius, int point_count, double attribute,
                                     double area_limit, int marker);

/* Mesh the geometry. Triangles are at most maximum_area in area and have no
   angle under minimum_angle degrees (at most 34), each with no limit if 0;
   points closer than merge_distance are merged, or only identical points if
   it is 0; and every triangle is Delaunay if conforming_delaunay is not 0,
   as a Voronoi grid needs. The mesh covers the polygons (or what the
   segments enclose, without polygons), less everything inside a hole
   polygon, and has only the vertices its triangles use. Where segments
   repeat, the first one's marker is kept, and segments with marker 0 on
   the mesh's boundary get marker 1, as Triangle gives them. The mesh has
   neighbors and segments. *mesh is NULL after a failure. */
int triangle_utils_mesh_generate(const TriangleUtilsGeometry * geometry, double maximum_area,
                                 double minimum_angle, double merge_distance,
                                 int conforming_delaunay, TriangleUtilsMesh ** mesh);

/* Refine a mesh, keeping its boundary and segments, each of which must be
   an edge of a triangle, and its triangle attributes and area limits where
   segments bound them (Triangle averages them across an edge it flips that
   is not a segment, and drops a limit if either triangle has none). Triangles are split to meet their area limits and maximum_area,
   and to have no angle under minimum_angle degrees (at most 34) or over
   maximum_angle (over 60 and under 180; Triangle can reach about 95 to 100
   degrees, not 90), each with no limit if 0. Triangles are flagged for the
   maximum angle at most maximum_angle_splits times, since it may never
   finish; triangle_utils_mesh_count_angles_over() tells what is left. Every
   triangle is Delaunay if conforming_delaunay is not 0. The new mesh has
   neighbors; *refined is NULL after a failure. */
int triangle_utils_mesh_refine(const TriangleUtilsMesh * mesh, double maximum_area,
                               double minimum_angle, double maximum_angle, int maximum_angle_splits,
                               int conforming_delaunay, TriangleUtilsMesh ** refined);

/* The number of a mesh's triangles with an angle over limit degrees. */
int triangle_utils_mesh_count_angles_over(const TriangleUtilsMesh * mesh, double limit,
                                          int * count);

#ifdef __cplusplus
}
#endif

#endif
