// Features read from ESRI shapefiles with shapelib (src/shapelib/).

#ifndef TRIANGLE_UTILS_SHAPEFILE_HPP
#define TRIANGLE_UTILS_SHAPEFILE_HPP

#include <string>
#include <vector>

#include "geometry.hpp"

namespace triangle_utils
{

// A value of each feature: from the field of the .dbf file named, or the
// number if no field is named.
struct FeatureValue
{
    std::string field;
    double number = 0.0;
};

// The values of each feature.
struct FeatureValues
{
    FeatureValue marker;
    FeatureValue spacing;
    FeatureValue attribute;
    FeatureValue area_limit;
};

// A Feature for each part of the shapes of a shapefile, named with or
// without .shp, in the order of its shapes: points, lines, and polygons,
// their multi-part and Z and M forms, and multipatch rings; a polygon's
// holes are hole polygons with its values. Null and empty shapes are
// skipped, and coordinates are taken as given, without the .prj file.
// Throws Error if a file cannot be read, a field is not found or not a
// number, a value is missing, a marker is not a whole number, or a
// multipatch has triangles.
std::vector<Feature> read_shapefile(const std::string & name, const FeatureValues & values);

} // namespace triangle_utils

#endif
