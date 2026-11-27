// Tests reading features from shapefiles, which the tests write with
// shapelib: polygons with several outer rings and holes, made
// counterclockwise; multi-part lines and points; null shapes skipped; values
// from integer, double, and text fields or numbers; multipatch rings, and
// multipatch triangles an error; the errors of fields and files; and adding
// the features to geometry, which meshes as the same coordinates do.

#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "error.hpp"
#include "geometry.hpp"
#include "shapefile.hpp"
#include "shapelib/shapefil.h"

using namespace triangle_utils;

namespace
{

// A folder for a test's shapefiles, removed after it.
class Folder
{
public:
    explicit Folder(const std::string & name)
        : m_path(std::filesystem::temp_directory_path() / ("triangle_utils_" + name))
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }
    ~Folder() { std::filesystem::remove_all(m_path); }
    std::string operator/(const std::string & name) const { return (m_path / name).string(); }

private:
    std::filesystem::path m_path;
};

// A shape's parts, each a list of points.
using Parts = std::vector<std::vector<Point>>;

// Write a shape of parts; no parts writes a null shape.
void write_shape(SHPHandle file, int type, const Parts & parts,
                 const std::vector<int> & part_types = {})
{
    std::vector<int> starts;
    std::vector<double> x, y;
    for (const auto & part : parts)
    {
        starts.push_back(static_cast<int>(x.size()));
        for (const Point & point : part)
        {
            x.push_back(point.x);
            y.push_back(point.y);
        }
    }
    SHPObject * shape =
        parts.empty()
            ? SHPCreateSimpleObject(SHPT_NULL, 0, nullptr, nullptr, nullptr)
            : SHPCreateObject(type, -1, static_cast<int>(parts.size()), starts.data(),
                              part_types.empty() ? nullptr : part_types.data(),
                              static_cast<int>(x.size()), x.data(), y.data(), nullptr, nullptr);
    ASSERT_GE(SHPWriteObject(file, -1, shape), 0);
    SHPDestroyObject(shape);
}

// The fields of the polygons' .dbf file: an integer, a double, text, and a
// logical field.
constexpr int MARKER = 0, SPACING = 1, ATTRIBUTE = 2, FLAG = 3;

// Write polygons.shp: shape 0 has two outer rings, the first with a hole,
// shape 1 is null, and shape 2 is a square written counterclockwise.
std::string write_polygons(const Folder & folder)
{
    const std::string name = folder / "polygons";
    SHPHandle file = SHPCreate(name.c_str(), SHPT_POLYGON);
    // outer rings clockwise and holes counterclockwise, each closed
    write_shape(file, SHPT_POLYGON,
                {{{0, 0}, {0, 4}, {4, 4}, {4, 0}, {0, 0}},
                 {{1, 1}, {2, 1}, {2, 2}, {1, 2}, {1, 1}},
                 {{10, 0}, {10, 2}, {12, 2}, {12, 0}, {10, 0}}});
    write_shape(file, SHPT_POLYGON, {});
    write_shape(file, SHPT_POLYGON, {{{20, 0}, {22, 0}, {22, 2}, {20, 2}, {20, 0}}});
    SHPClose(file);
    DBFHandle dbf = DBFCreate(name.c_str());
    DBFAddField(dbf, "marker", FTInteger, 10, 0);
    DBFAddField(dbf, "spacing", FTDouble, 12, 4);
    DBFAddField(dbf, "zone", FTString, 10, 0);
    DBFAddField(dbf, "flag", FTLogical, 1, 0);
    // in order, since rows are added one after another; the null shape's
    // values are missing, which is not an error
    for (int row = 0; row < 3; row++)
    {
        if (row == 1)
        {
            for (int field = 0; field < 4; field++) DBFWriteNULLAttribute(dbf, row, field);
            continue;
        }
        DBFWriteIntegerAttribute(dbf, row, MARKER, row + 1);
        DBFWriteDoubleAttribute(dbf, row, SPACING, 0.5 * (row + 1));
        DBFWriteStringAttribute(dbf, row, ATTRIBUTE, row == 0 ? "2.5" : "7");
        DBFWriteLogicalAttribute(dbf, row, FLAG, 'T');
    }
    DBFClose(dbf);
    return name;
}

FeatureValues fields(const std::string & marker, const std::string & spacing,
                     const std::string & attribute, double area_limit)
{
    return {{marker, 0.0}, {spacing, 0.0}, {attribute, 0.0}, {"", area_limit}};
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

} // namespace

TEST(Shapefile, PolygonsHolesAndValues)
{
    const Folder folder("polygons");
    const std::string name = write_polygons(folder);
    const auto features = read_shapefile(name + ".shp", fields("marker", "spacing", "zone", 7.0));
    ASSERT_EQ(features.size(), 4u);
    const std::vector<FeatureKind> kinds = {FeatureKind::POLYGON, FeatureKind::HOLE_POLYGON,
                                            FeatureKind::POLYGON, FeatureKind::POLYGON};
    const std::vector<int> rows = {0, 0, 0, 2};
    for (std::size_t i = 0; i < features.size(); i++)
    {
        EXPECT_EQ(features[i].kind, kinds[i]) << i;
        EXPECT_EQ(features[i].row, rows[i]) << i;
        EXPECT_EQ(features[i].marker, rows[i] + 1) << i;
        EXPECT_EQ(features[i].spacing, 0.5 * (rows[i] + 1)) << i;
        EXPECT_EQ(features[i].attribute, rows[i] == 0 ? 2.5 : 7.0) << i;
        EXPECT_EQ(features[i].area_limit, 7.0) << i;
        // counterclockwise from the first point, without the closing point
        ASSERT_EQ(features[i].points.size(), 4u) << i;
        double twice_area = 0.0;
        const auto & p = features[i].points;
        for (std::size_t j = 0; j < p.size(); j++)
            twice_area += p[j].x * p[(j + 1) % p.size()].y - p[(j + 1) % p.size()].x * p[j].y;
        EXPECT_GT(twice_area, 0.0) << i;
    }
    EXPECT_EQ(features[0].points[0].x, 0.0);
    EXPECT_EQ(features[0].points[1].x, 4.0);
    EXPECT_EQ(features[1].points[0].x, 1.0);
    // the counterclockwise square keeps its order
    EXPECT_EQ(features[3].points[1].x, 22.0);
    EXPECT_EQ(features[3].points[1].y, 0.0);

    // numbers rather than fields, without the .dbf file
    const auto numbers = read_shapefile(name, {{"", 3.0}, {"", 0.0}, {"", 0.0}, {"", 0.0}});
    ASSERT_EQ(numbers.size(), 4u);
    EXPECT_EQ(numbers[3].marker, 3);
}

TEST(Shapefile, FieldErrors)
{
    const Folder folder("fields");
    const std::string name = write_polygons(folder);
    EXPECT_EQ(error_of([&] { read_shapefile(name, fields("missing", "", "", 0.0)); }),
              "the marker field missing is not in the .dbf file");
    EXPECT_EQ(error_of([&] { read_shapefile(name, fields("", "flag", "", 0.0)); }),
              "the spacing field flag is not a number or text field");
    // a marker from the double field, 0.5, is not a whole number
    EXPECT_EQ(error_of([&] { read_shapefile(name, fields("spacing", "", "", 0.0)); }),
              "the marker field spacing in row 0 is 0.5, not a whole number");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(error_of([&] { read_shapefile(name, fields("", "", "", nan)); }),
              "area limit is not a finite number");

    // text that is not a number, and a missing value in a row with a shape
    DBFHandle dbf = DBFOpen((name + ".dbf").c_str(), "rb+");
    DBFWriteStringAttribute(dbf, 2, ATTRIBUTE, "clay");
    DBFWriteNULLAttribute(dbf, 0, SPACING);
    DBFClose(dbf);
    EXPECT_EQ(error_of([&] { read_shapefile(name, fields("", "", "zone", 0.0)); }),
              "the attribute field zone in row 2 is \"clay\", not a number");
    EXPECT_EQ(error_of([&] { read_shapefile(name, fields("", "spacing", "", 0.0)); }),
              "the spacing field spacing in row 0 has no value");

    // without the .dbf file, a field cannot be read, but numbers can
    std::filesystem::remove(name + ".dbf");
    EXPECT_EQ(error_of([&] { read_shapefile(name, fields("marker", "", "", 0.0)); })
                  .find("cannot open the .dbf file of "),
              0u);
    EXPECT_EQ(read_shapefile(name, fields("", "", "", 0.0)).size(), 4u);
    EXPECT_EQ(error_of([&] { read_shapefile(folder / "none", fields("", "", "", 0.0)); })
                  .find("cannot open the shapefile "),
              0u);
}

TEST(Shapefile, LinesAndPoints)
{
    const Folder folder("lines");
    const std::string lines = folder / "lines";
    SHPHandle file = SHPCreate(lines.c_str(), SHPT_ARCZ);
    write_shape(file, SHPT_ARCZ, {{{0, 0}, {1, 1}, {2, 0}}, {{5, 5}, {6, 6}}});
    write_shape(file, SHPT_ARCZ, {{{9, 9}, {8, 8}}});
    SHPClose(file);
    const auto line_features = read_shapefile(lines, fields("", "", "", 0.0));
    ASSERT_EQ(line_features.size(), 3u);
    for (const Feature & feature : line_features) EXPECT_EQ(feature.kind, FeatureKind::LINE);
    EXPECT_EQ(line_features[0].points.size(), 3u);
    EXPECT_EQ(line_features[1].row, 0);
    // a line keeps its direction
    EXPECT_EQ(line_features[2].points[0].x, 9.0);

    const std::string points = folder / "points";
    file = SHPCreate(points.c_str(), SHPT_MULTIPOINT);
    write_shape(file, SHPT_MULTIPOINT, {{{1, 1}, {2, 2}, {3, 3}}});
    SHPClose(file);
    const auto point_features = read_shapefile(points, fields("", "", "", 0.0));
    ASSERT_EQ(point_features.size(), 3u);
    EXPECT_EQ(point_features[2].kind, FeatureKind::POINT);
    EXPECT_EQ(point_features[2].points.size(), 1u);
    EXPECT_EQ(point_features[2].points[0].x, 3.0);
}

TEST(Shapefile, Multipatch)
{
    const Folder folder("multipatch");
    const std::string rings = folder / "rings";
    SHPHandle file = SHPCreate(rings.c_str(), SHPT_MULTIPATCH);
    write_shape(file, SHPT_MULTIPATCH,
                {{{0, 0}, {0, 4}, {4, 4}, {4, 0}, {0, 0}}, {{1, 1}, {2, 1}, {2, 2}, {1, 1}}},
                {SHPP_OUTERRING, SHPP_INNERRING});
    SHPClose(file);
    const auto features = read_shapefile(rings, fields("", "", "", 0.0));
    ASSERT_EQ(features.size(), 2u);
    EXPECT_EQ(features[0].kind, FeatureKind::POLYGON);
    EXPECT_EQ(features[1].kind, FeatureKind::HOLE_POLYGON);
    EXPECT_EQ(features[1].points.size(), 3u);

    const std::string triangles = folder / "triangles";
    file = SHPCreate(triangles.c_str(), SHPT_MULTIPATCH);
    write_shape(file, SHPT_MULTIPATCH, {{{0, 0}, {1, 0}, {0, 1}, {1, 1}}}, {SHPP_TRISTRIP});
    SHPClose(file);
    EXPECT_EQ(error_of([&] { read_shapefile(triangles, fields("", "", "", 0.0)); }),
              "shape 0 of " + triangles +
                  " is a multipatch of triangles; convert it to polygons, for example with "
                  "ogr2ogr -nlt MULTIPOLYGON");
}

TEST(Shapefile, FeaturesMeshAsTheirCoordinates)
{
    const Folder folder("mesh");
    const std::string name = write_polygons(folder);
    const std::string points = folder / "regions";
    SHPHandle file = SHPCreate(points.c_str(), SHPT_POINT);
    write_shape(file, SHPT_POINT, {{{3, 3}}});
    SHPClose(file);

    Geometry from_files;
    add_features(from_files, read_shapefile(name, fields("marker", "", "", 0.0)),
                 PointsAs::VERTICES);
    add_features(from_files, read_shapefile(points, {{"", 0}, {"", 0}, {"", 5.0}, {"", 0.25}}),
                 PointsAs::REGIONS);
    EXPECT_EQ(from_files.polygons.size(), 3u);
    EXPECT_EQ(from_files.hole_polygons.size(), 1u);
    ASSERT_EQ(from_files.regions.size(), 1u);
    EXPECT_EQ(from_files.regions[0].attribute, 5.0);
    EXPECT_EQ(from_files.regions[0].area_limit, 0.25);

    Geometry from_coordinates;
    from_coordinates.polygons = {{{{0, 0}, {4, 0}, {4, 4}, {0, 4}}, 0.0, 1},
                                 {{{10, 0}, {12, 0}, {12, 2}, {10, 2}}, 0.0, 1},
                                 {{{20, 0}, {22, 0}, {22, 2}, {20, 2}}, 0.0, 3}};
    from_coordinates.hole_polygons = {{{{1, 1}, {2, 1}, {2, 2}, {1, 2}}, 0.0, 1}};
    from_coordinates.regions = {{{3, 3}, 5.0, 0.25}};
    MeshOptions options;
    options.maximum_area = 1.0;
    options.minimum_angle = 25.0;
    const Mesh a = generate_mesh(from_files, options);
    const Mesh b = generate_mesh(from_coordinates, options);
    EXPECT_EQ(a.vertices.size(), b.vertices.size());
    EXPECT_EQ(a.triangles, b.triangles);
    EXPECT_EQ(a.triangle_attributes, b.triangle_attributes);

    Geometry holes;
    add_features(holes, read_shapefile(points, fields("", "", "", 0.0)), PointsAs::HOLES);
    ASSERT_EQ(holes.holes.size(), 1u);
    EXPECT_EQ(holes.holes[0].x, 3.0);
}
