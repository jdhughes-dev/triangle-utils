// Geometry to mesh: polygons, holes, regions, points, lines, and rings, and
// the triangular mesh Triangle builds from them.

#ifndef TRIANGLE_UTILS_GEOMETRY_HPP
#define TRIANGLE_UTILS_GEOMETRY_HPP

#include <string>
#include <vector>

#include "mesh.hpp"

namespace triangle_utils
{

// A region: a point inside it, its attribute, and its maximum triangle area,
// or no limit if the area is not positive.
struct Region
{
    Point point;
    double attribute = 0.0;
    double area_limit = 0.0;
};

// A chain of points: a closed polygon (the last point joins the first) or an
// open line.
struct Chain
{
    std::vector<Point> points;
    // the longest distance between points along it, added evenly between
    // the given points; no points are added if it is not positive
    double spacing = 0.0;
    // the boundary marker of its segments
    int marker = 0;
};

// A ring of finer triangles around a point, such as a well: a vertex at the
// center, a polygon of point_count points at the radius, and a region inside.
struct Ring
{
    Point center;
    double radius = 0.0;
    int point_count = 0;
    double attribute = 0.0;
    double area_limit = 0.0;
    int marker = 0;
};

// What to mesh.
struct Geometry
{
    // boundaries and internal boundaries the mesh follows
    std::vector<Chain> polygons;
    // polygons whose inside is not meshed
    std::vector<Chain> hole_polygons;
    // points inside holes bounded by the polygons
    std::vector<Point> holes;
    std::vector<Region> regions;
    // vertices the mesh includes
    std::vector<Point> points;
    // open lines the mesh follows, such as rivers
    std::vector<Chain> lines;
    std::vector<Ring> rings;
};

// The kind of a feature read from a GIS.
enum class FeatureKind
{
    POLYGON,
    HOLE_POLYGON,
    LINE,
    POINT,
};

// One part of a feature: its points, the row of the source it came from, and
// its values; a polygon's points are counterclockwise.
struct Feature
{
    FeatureKind kind = FeatureKind::POINT;
    std::vector<Point> points;
    int row = 0;
    int marker = 0;
    double spacing = 0.0;
    double attribute = 0.0;
    double area_limit = 0.0;
};

// What point features add to geometry.
enum class PointsAs
{
    VERTICES,
    REGIONS,
    HOLES,
};

// Options for meshing.
struct MeshOptions
{
    // the largest triangle area, or no limit if not positive
    double maximum_area = 0.0;
    // the smallest angle in degrees, at most 34, or no quality limit if 0
    double minimum_angle = 0.0;
    // points closer than this, or than 1e-12 of the largest coordinate if
    // that is larger, are merged, and a point that close to a segment
    // splits it
    double merge_distance = 0.0;
    // whether every triangle is Delaunay (Triangle's D switch), as a Voronoi
    // grid needs
    bool conforming_delaunay = false;
};

// Mesh the geometry. The mesh covers the polygons (or what the segments
// enclose, without polygons), less everything inside a hole polygon, and has
// only the vertices its triangles use. A region's attribute and limit, and a
// ring's, spread only up to segments. A repeated segment keeps the first
// one's marker, and segments and vertices with marker 0 on the mesh's
// boundary get marker 1, as Triangle gives them. The mesh has neighbors and
// segments, and one triangle attribute, its region's, when there are regions
// or rings. Throws Error if the geometry or options are not valid, Triangle
// cannot mesh them, or no triangle is left.
Mesh generate_mesh(const Geometry & geometry, const MeshOptions & options);

// Add features to geometry: polygons, hole polygons, and lines with their
// spacing and marker, and points as vertices, regions with their attribute
// and area limit, or holes; generate_mesh() checks them.
void add_features(Geometry & geometry, const std::vector<Feature> & features, PointsAs points);

// The points of a chain with points added so no step is longer than its
// spacing.
std::vector<Point> densify(const Chain & chain, bool closed);

// A point strictly inside a simple polygon, without regard to other geometry;
// throws Error if none is found.
Point point_inside(const std::vector<Point> & polygon);

} // namespace triangle_utils

#endif
