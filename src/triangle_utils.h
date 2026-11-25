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

/* A grid of polygonal cells for MODFLOW 6 DISV, built from a mesh with
   triangle_utils_mesh_voronoi() or triangle_utils_mesh_triangle_grid(); free
   it with triangle_utils_grid_free(). */
typedef struct TriangleUtilsGrid TriangleUtilsGrid;

/* The cells a line or marked segments pass through, with the length in
   each; free it with triangle_utils_cell_lengths_free(). */
typedef struct TriangleUtilsCellLengths TriangleUtilsCellLengths;

/* The faces the cells of a grid share, for MODFLOW DISU; free it with
   triangle_utils_connections_free(). */
typedef struct TriangleUtilsConnections TriangleUtilsConnections;

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

/* Set the vertices of a mesh that improving must not move, besides those on
   segments and the boundary, such as wells; a generated mesh has its point
   features and ring centers. The array is copied, and the mesh is unchanged
   after a failure. */
int triangle_utils_mesh_set_fixed_vertices(TriangleUtilsMesh * mesh, int fixed_count,
                                           const int * fixed_vertices);

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
   points closer than merge_distance, or than 1e-12 of the largest
   coordinate if that is larger, are merged, and a point that close to a
   segment splits it; and every triangle is Delaunay if conforming_delaunay
   is not 0, as a Voronoi grid needs. The mesh covers the polygons (or what
   the segments enclose, without polygons), less everything inside a hole
   polygon, and has only the vertices its triangles use. Where segments
   repeat, including the pieces of split segments, the first one's marker is
   kept, and segments with marker 0 on the mesh's boundary get marker 1, as
   Triangle gives them. The mesh has neighbors and segments. *mesh is NULL
   after a failure. */
int triangle_utils_mesh_generate(const TriangleUtilsGeometry * geometry, double maximum_area,
                                 double minimum_angle, double merge_distance,
                                 int conforming_delaunay, TriangleUtilsMesh ** mesh);

/* Refine a mesh by its area limits, maximum_area, minimum_angle (at most
   34), and maximum_angle (over 60 and under 180), each none if 0, keeping
   its boundary and segments; see README.md for what Triangle can reach and
   how it treats attributes and limits. At most maximum_angle_splits
   triangles are flagged for the maximum angle. *refined is NULL after a
   failure. */
int triangle_utils_mesh_refine(const TriangleUtilsMesh * mesh, double maximum_area,
                               double minimum_angle, double maximum_angle, int maximum_angle_splits,
                               int conforming_delaunay, TriangleUtilsMesh ** refined);

/* The number of a mesh's triangles with an angle over limit degrees. */
int triangle_utils_mesh_count_angles_over(const TriangleUtilsMesh * mesh, double limit,
                                          int * count);

/* The number of a mesh's fixed vertices. */
int triangle_utils_mesh_fixed_vertex_count(const TriangleUtilsMesh * mesh, int * fixed_count);

/* The mesh's fixed vertices; fixed_count must be the mesh's. */
int triangle_utils_mesh_fixed_vertices(const TriangleUtilsMesh * mesh, int fixed_count,
                                       int * fixed_vertices);

/* Smoothing for triangle_utils_mesh_improve(): toward the average of a
   vertex's neighbors, or of its triangles' centroids weighted by area. */
#define TRIANGLE_UTILS_LAPLACIAN 0
#define TRIANGLE_UTILS_CENTROIDAL 1

/* Improve a mesh by moving the vertices that are not fixed and flipping edges
   that are not segments: a move is kept only where no triangle inverts, no
   angle falls below minimum_angle degrees (up to 60, or the mesh's own
   smallest angle if 0, so it does not decrease), no triangle exceeds its
   area limit, and the worst angle around the vertex improves, toward
   maximum_angle degrees (over 60 and under 180; 90 puts every circumcenter
   inside its triangle), or the smallest angle only if it is 0. A
   minimum_angle below the mesh's trades smaller angles for fewer over
   maximum_angle. Sweeps stop after iterations, or when no vertex moves
   farther than tolerance times the mean edge length. A boundary point where
   two triangles meet keeps an obtuse angle if it has one, since its two
   angles sum to 180 degrees; triangle_utils_mesh_count_angles_over() tells
   what is left. *improved is NULL after a failure. */
int triangle_utils_mesh_improve(const TriangleUtilsMesh * mesh, int smoothing, double maximum_angle,
                                double minimum_angle, int iterations, double tolerance,
                                TriangleUtilsMesh ** improved);

/* The Voronoi grid of a mesh: a cell around each vertex, centered on it, with
   corners at the circumcenters of the vertex's triangles, closed on the
   boundary by the midpoints of its edges, and by its vertices where it
   turns, so a boundary cell's center is on its edge and the cell is not
   convex where the boundary turns inward, as at a hole's corner. A cell on
   a segment inside the mesh, such as a lake's shore, spans both sides and
   takes the attributes of the side with most of its area. The mesh must be
   conforming Delaunay (Triangle's D switch), so each circumcenter is in the
   mesh. *grid is NULL after a failure. */
int triangle_utils_mesh_voronoi(const TriangleUtilsMesh * mesh, TriangleUtilsGrid ** grid);

/* Centers for triangle_utils_mesh_triangle_grid(): each triangle's centroid,
   or its circumcenter, which joins neighbors perpendicular to their shared
   edge but lies outside an obtuse triangle. */
#define TRIANGLE_UTILS_CENTROID 0
#define TRIANGLE_UTILS_CIRCUMCENTER 1

/* A grid whose cells are the mesh's triangles, with their attributes.
   *grid is NULL after a failure. */
int triangle_utils_mesh_triangle_grid(const TriangleUtilsMesh * mesh, int center,
                                      TriangleUtilsGrid ** grid);

/* Free a grid; NULL is ignored. */
void triangle_utils_grid_free(TriangleUtilsGrid * grid);

/* The number of vertices and cells of a grid, and the number of corners of
   all its cells; unchanged after a failure. */
int triangle_utils_grid_counts(const TriangleUtilsGrid * grid, int * vertex_count, int * cell_count,
                               int * corner_count);

/* The grid's vertices, x and y of each in turn; each is listed once. */
int triangle_utils_grid_vertices(const TriangleUtilsGrid * grid, int vertex_count,
                                 double * vertex_xy);

/* The corners of each cell, clockwise as DISV lists them: those of cell i
   are corners[corner_offsets[i]] up to corners[corner_offsets[i + 1]], so
   corner_offsets has cell_count + 1 values. */
int triangle_utils_grid_cells(const TriangleUtilsGrid * grid, int cell_count, int * corner_offsets,
                              int corner_count, int * corners);

/* The center of each cell, x and y in turn. */
int triangle_utils_grid_centers(const TriangleUtilsGrid * grid, int cell_count, double * center_xy);

/* The mesh vertex (Voronoi grid) or triangle (triangle grid) of each cell. */
int triangle_utils_grid_sources(const TriangleUtilsGrid * grid, int cell_count, int * sources);

/* The number of attributes of each cell, as of the mesh's triangles. */
int triangle_utils_grid_attribute_count(const TriangleUtilsGrid * grid, int * attribute_count);

/* The cells' attributes, attribute_count of each in turn; value_count is the
   attribute count times the cell count. */
int triangle_utils_grid_attributes(const TriangleUtilsGrid * grid, int value_count,
                                   double * attributes);

/* A grid from its vertices, x and y of each in turn, its cells' corners
   (those of cell i are corners[corner_offsets[i]] up to
   corners[corner_offsets[i + 1]]), and its centers, such as a grid read back
   from a caller's arrays; it has no sources or attributes. *grid is NULL
   after a failure. */
int triangle_utils_grid_create(int vertex_count, const double * vertex_xy, int cell_count,
                               const int * corner_offsets, int corner_count, const int * corners,
                               const double * center_xy, TriangleUtilsGrid ** grid);

/* The cells along a line of point_count points, in order, with the length of
   line in each; a cell the line enters twice appears twice, a part along a
   face is in the cell on the line's left, or on its right where none is on
   the left, and parts outside the grid are left out. *cell_lengths is NULL
   after a failure. */
int triangle_utils_grid_line_cells(const TriangleUtilsGrid * grid, int point_count,
                                   const double * point_xy,
                                   TriangleUtilsCellLengths ** cell_lengths);

/* The cells on the mesh's segments with a marker, such as a boundary, each
   once, in order of where they first meet the segments, with the length of
   segment in each; a segment along a face two cells share is in only one of
   them; an error if no segment has the marker. *cell_lengths is NULL after
   a failure. */
int triangle_utils_grid_segment_cells(const TriangleUtilsGrid * grid,
                                      const TriangleUtilsMesh * mesh, int marker,
                                      TriangleUtilsCellLengths ** cell_lengths);

/* The cell containing each of point_count points, such as wells, or -1
   outside the grid; a point on a face or corner is in the lowest-numbered
   cell there. cells has point_count values. */
int triangle_utils_grid_point_cells(const TriangleUtilsGrid * grid, int point_count,
                                    const double * point_xy, int * cells);

/* Free cell lengths; NULL is ignored. */
void triangle_utils_cell_lengths_free(TriangleUtilsCellLengths * cell_lengths);

/* The number of cell lengths. */
int triangle_utils_cell_lengths_count(const TriangleUtilsCellLengths * cell_lengths, int * count);

/* Each cell, where its part starts along the line or segments, and its
   length; count must be the number of cell lengths. */
int triangle_utils_cell_lengths(const TriangleUtilsCellLengths * cell_lengths, int count,
                                int * cells, double * starts, double * lengths);

/* The connections of a grid's cells to the cells they share a face with;
   cells that share a face share its two corners. An error if an edge is a
   side of more than two cells, two cells overlap, or a center is on a face or
   beyond it, as an obtuse triangle's circumcenter is.
   *connections is NULL after a failure. */
int triangle_utils_grid_connections(const TriangleUtilsGrid * grid,
                                    TriangleUtilsConnections ** connections);

/* Free connections; NULL is ignored. */
void triangle_utils_connections_free(TriangleUtilsConnections * connections);

/* The number of cells and of connections, each listed from both of its
   cells. */
int triangle_utils_connections_count(const TriangleUtilsConnections * connections, int * cell_count,
                                     int * connection_count);

/* The connections of cell i are offsets[i] up to offsets[i + 1], so offsets
   has cell_count + 1 values; for each, in increasing order of the other
   cell: the other cell (DISU JA, without the cell itself), the distance from
   the cell's center to the face, normal to it (CL12), the face's width
   (HWVA), and the direction of its normal out of the cell, in degrees
   counterclockwise from the x axis, 0 up to 360 (ANGLDEGX). */
int triangle_utils_connections(const TriangleUtilsConnections * connections, int cell_count,
                               int * offsets, int connection_count, int * cells, double * distances,
                               double * widths, double * angles);

#ifdef __cplusplus
}
#endif

#endif
