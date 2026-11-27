// Features read from ESRI shapefiles with shapelib (src/shapelib/).

#include "shapefile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "error.hpp"
#include "shapelib/shapefil.h"

namespace triangle_utils
{

namespace
{

// shapelib's last message, which its hooks would otherwise print
std::string shapelib_message;

void keep_message(const char * text)
{
    shapelib_message = text;
}

// shapelib's file functions, keeping its messages
SAHooks hooks()
{
    SAHooks result;
    SASetupDefaultHooks(&result);
    result.Error = keep_message;
    return result;
}

struct ShapeFileClose
{
    void operator()(SHPInfo * file) const { SHPClose(file); }
};

struct DbfFileClose
{
    void operator()(DBFInfo * file) const { DBFClose(file); }
};

struct ShapeDestroy
{
    void operator()(SHPObject * shape) const { SHPDestroyObject(shape); }
};

// Error for a file that cannot be opened, with shapelib's reason.
Error open_error(const std::string & name)
{
    return Error(message("cannot open the shapefile ", name,
                         shapelib_message.empty() ? "" : ": " + shapelib_message));
}

// Twice the signed area of a ring, positive if counterclockwise.
double twice_area(const std::vector<Point> & ring)
{
    // relative to the first point, so a small ring far from the origin keeps
    // its sign
    double sum = 0.0;
    for (std::size_t i = 1; i + 1 < ring.size(); i++)
        sum += (ring[i].x - ring[0].x) * (ring[i + 1].y - ring[0].y) -
               (ring[i + 1].x - ring[0].x) * (ring[i].y - ring[0].y);
    return sum;
}

// A ring's points counterclockwise from its first, without a last point
// that repeats the first.
std::vector<Point> counterclockwise(std::vector<Point> ring)
{
    if (ring.size() > 1 && ring.front().x == ring.back().x && ring.front().y == ring.back().y)
        ring.pop_back();
    if (twice_area(ring) < 0.0) std::reverse(ring.begin() + 1, ring.end());
    return ring;
}

// The value of a feature from a row of the .dbf file.
class ValueReader
{
public:
    ValueReader(DBFInfo * dbf, const FeatureValue & value, const char * name, bool whole)
        : m_dbf(dbf), m_value(value), m_name(name), m_whole(whole)
    {
        if (value.field.empty())
        {
            check(value.number, name);
            return;
        }
        if (dbf == nullptr)
            throw Error(message("the ", name, " field ", value.field,
                                " cannot be read, since the shapefile has no .dbf file"));
        int found = 0;
        for (int field = 0; field < DBFGetFieldCount(dbf); field++)
        {
            char field_name[XBASE_FLDNAME_LEN_READ + 1] = {};
            DBFGetFieldInfo(dbf, field, field_name, nullptr, nullptr);
            if (value.field == field_name)
            {
                m_field = field;
                found++;
            }
        }
        if (found == 0)
            throw Error(message("the ", name, " field ", value.field, " is not in the .dbf file"));
        if (found > 1)
            throw Error(message("the ", name, " field ", value.field,
                                " is in the .dbf file more than once"));
        m_type = DBFGetFieldInfo(dbf, m_field, nullptr, nullptr, nullptr);
        if (m_type != FTString && m_type != FTInteger && m_type != FTDouble)
            throw Error(
                message("the ", name, " field ", value.field, " is not a number or text field"));
    }

    // The value of a row.
    double operator()(int row) const
    {
        if (m_field < 0) return m_value.number;
        const std::string where =
            message("the ", m_name, " field ", m_value.field, " in row ", row);
        if (DBFIsAttributeNULL(m_dbf, row, m_field)) throw Error(where + " has no value");
        double number = 0.0;
        if (m_type == FTString)
        {
            const std::string text = DBFReadStringAttribute(m_dbf, row, m_field);
            char * end = nullptr;
            number = std::strtod(text.c_str(), &end);
            if (text.empty() || end != text.c_str() + text.size())
                throw Error(message(where, " is \"", text, "\", not a number"));
        }
        else
            // an integer field too, as gridgen#188 found its values need
            number = DBFReadDoubleAttribute(m_dbf, row, m_field);
        check(number, where.c_str());
        return number;
    }

private:
    void check(double number, const char * where) const
    {
        if (!std::isfinite(number)) throw Error(message(where, " is not a finite number"));
        if (m_whole &&
            (number != std::floor(number) || std::abs(number) > std::numeric_limits<int>::max()))
            throw Error(message(where, " is ", number, ", not a whole number"));
    }

    DBFInfo * m_dbf;
    FeatureValue m_value;
    const char * m_name;
    bool m_whole;
    int m_field = -1;
    DBFFieldType m_type = FTInvalid;
};

} // namespace

std::vector<Feature> read_shapefile(const std::string & name, const FeatureValues & values)
{
    const SAHooks file_hooks = hooks();
    shapelib_message.clear();
    const std::unique_ptr<SHPInfo, ShapeFileClose> shapes(
        SHPOpenLL(name.c_str(), "rb", &file_hooks));
    if (!shapes) throw open_error(name);
    int shape_count = 0, shape_type = 0;
    SHPGetInfo(shapes.get(), &shape_count, &shape_type, nullptr, nullptr);

    // the .dbf file only if a value comes from it
    std::unique_ptr<DBFInfo, DbfFileClose> dbf;
    for (const FeatureValue * value :
         {&values.marker, &values.spacing, &values.attribute, &values.area_limit})
        if (!value->field.empty() && !dbf)
        {
            shapelib_message.clear();
            dbf.reset(DBFOpenLL(name.c_str(), "rb", &file_hooks));
            if (!dbf)
                throw Error(message("cannot open the .dbf file of ", name,
                                    shapelib_message.empty() ? "" : ": " + shapelib_message));
            if (DBFGetRecordCount(dbf.get()) < shape_count)
                throw Error(message("the .dbf file of ", name, " has ",
                                    DBFGetRecordCount(dbf.get()), " rows, but the shapefile has ",
                                    shape_count, " shapes"));
        }
    const ValueReader marker(dbf.get(), values.marker, "marker", true);
    const ValueReader spacing(dbf.get(), values.spacing, "spacing", false);
    const ValueReader attribute(dbf.get(), values.attribute, "attribute", false);
    const ValueReader area_limit(dbf.get(), values.area_limit, "area limit", false);

    std::vector<Feature> features;
    for (int row = 0; row < shape_count; row++)
    {
        shapelib_message.clear();
        const std::unique_ptr<SHPObject, ShapeDestroy> shape(SHPReadObject(shapes.get(), row));
        if (!shape)
            throw Error(message("shape ", row, " of ", name, " cannot be read",
                                shapelib_message.empty() ? "" : ": " + shapelib_message));
        if (shape->nSHPType == SHPT_NULL || shape->nVertices == 0) continue;
        const int type = shape->nSHPType;
        const bool polygon = type == SHPT_POLYGON || type == SHPT_POLYGONZ || type == SHPT_POLYGONM;
        // a polygon's outer rings clockwise and its holes counterclockwise,
        // as a shapefile should have them
        if (polygon)
        {
            // as a plain polygon, since shapelib does not test a ring whose z
            // varies for being inside another, and the mesh is in x and y
            shape->nSHPType = SHPT_POLYGON;
            SHPRewindObject(shapes.get(), shape.get());
        }

        // the points of each part, or of each point
        std::vector<std::pair<FeatureKind, std::vector<Point>>> parts;
        const auto part_points = [&](int part)
        {
            const int start = shape->nParts == 0 ? 0 : shape->panPartStart[part];
            const int end =
                part + 1 < shape->nParts ? shape->panPartStart[part + 1] : shape->nVertices;
            std::vector<Point> points;
            for (int vertex = start; vertex < end; vertex++)
                points.push_back({shape->padfX[vertex], shape->padfY[vertex]});
            return points;
        };
        if (type == SHPT_POINT || type == SHPT_POINTZ || type == SHPT_POINTM ||
            type == SHPT_MULTIPOINT || type == SHPT_MULTIPOINTZ || type == SHPT_MULTIPOINTM)
        {
            for (int vertex = 0; vertex < shape->nVertices; vertex++)
                parts.push_back(
                    {FeatureKind::POINT, {{shape->padfX[vertex], shape->padfY[vertex]}}});
        }
        else if (type == SHPT_ARC || type == SHPT_ARCZ || type == SHPT_ARCM)
        {
            for (int part = 0; part < std::max(shape->nParts, 1); part++)
                parts.push_back({FeatureKind::LINE, part_points(part)});
        }
        else if (polygon)
        {
            for (int part = 0; part < std::max(shape->nParts, 1); part++)
            {
                std::vector<Point> ring = part_points(part);
                const FeatureKind kind =
                    twice_area(ring) > 0.0 ? FeatureKind::HOLE_POLYGON : FeatureKind::POLYGON;
                parts.push_back({kind, counterclockwise(std::move(ring))});
            }
        }
        else if (type == SHPT_MULTIPATCH)
        {
            for (int part = 0; part < shape->nParts; part++)
            {
                const int part_type = shape->panPartType[part];
                if (part_type == SHPP_TRISTRIP || part_type == SHPP_TRIFAN)
                    throw Error(message("shape ", row, " of ", name,
                                        " is a multipatch of triangles; convert it to polygons, "
                                        "for example with ogr2ogr -nlt MULTIPOLYGON"));
                // a first ring begins a polygon, and the rings after it are
                // its holes
                const FeatureKind kind = part_type == SHPP_OUTERRING || part_type == SHPP_FIRSTRING
                                             ? FeatureKind::POLYGON
                                             : FeatureKind::HOLE_POLYGON;
                parts.push_back({kind, counterclockwise(part_points(part))});
            }
        }
        else
            throw Error(message("shape ", row, " of ", name, " has shape type ", type,
                                ", which is not a point, line, polygon, or multipatch"));

        Feature feature;
        feature.row = row;
        feature.marker = static_cast<int>(marker(row));
        feature.spacing = spacing(row);
        feature.attribute = attribute(row);
        feature.area_limit = area_limit(row);
        for (auto & [kind, points] : parts)
        {
            if (points.empty()) continue;
            feature.kind = kind;
            feature.points = std::move(points);
            features.push_back(feature);
        }
    }
    return features;
}

} // namespace triangle_utils
