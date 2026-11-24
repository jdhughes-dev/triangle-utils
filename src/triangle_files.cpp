// Reading and writing Triangle's files (.node, .ele, .poly, .neigh).

#include "triangle_files.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>

#include "error.hpp"

namespace triangle_utils
{

namespace
{

// the largest count whose index products (up to 3 * count) fit Triangle's int
constexpr std::int64_t MAX_COUNT = std::numeric_limits<int>::max() / 3;

// The fields of each line of a file, skipping blank lines and comments (#).
class LineReader
{
public:
    LineReader(std::istream & input, const std::string & name) : m_input(input), m_name(name) {}

    // Read the next line with fields; false at the end of the file.
    bool next()
    {
        std::string line;
        while (std::getline(m_input, line))
        {
            m_line_number++;
            // the byte order mark some editors write at the start of UTF-8
            constexpr std::string_view BYTE_ORDER_MARK = "\xEF\xBB\xBF";
            if (m_line_number == 1 && line.starts_with(BYTE_ORDER_MARK))
                line.erase(0, BYTE_ORDER_MARK.size());
            const std::size_t comment = line.find('#');
            if (comment != std::string::npos) line.erase(comment);
            m_fields.clear();
            std::istringstream fields(line);
            std::string field;
            while (fields >> field) m_fields.push_back(field);
            if (!m_fields.empty()) return true;
        }
        if (m_input.bad()) throw Error(message("could not read ", m_name));
        return false;
    }

    // Read the next line, which must have at least minimum fields.
    void require(std::size_t minimum, const char * what)
    {
        if (!next()) throw Error(message(m_name, " ends before ", what));
        if (m_fields.size() < minimum)
            fail(message(what, " has ", m_fields.size(), " values, but ", minimum, " are needed"));
    }

    std::size_t size() const { return m_fields.size(); }

    // An optional number, which Triangle reads as 0 when it is missing.
    double optional_real(std::size_t field, const char * what) const
    {
        return field < m_fields.size() ? real(field, what) : 0.0;
    }

    int optional_integer(std::size_t field, const char * what) const
    {
        return field < m_fields.size() ? integer(field, what) : 0;
    }

    int integer(std::size_t field, const char * what) const
    {
        const std::string & text = m_fields.at(field);
        char * end = nullptr;
        errno = 0;
        // a whole field in decimal; Triangle also reads hexadecimal and octal
        // and ignores what follows the digits
        const long value = std::strtol(text.c_str(), &end, 10);
        if (end == text.c_str() || end != text.c_str() + text.size())
            fail(message(what, " \"", text, "\" is not an integer"));
        if (errno == ERANGE || value < std::numeric_limits<int>::min() ||
            value > std::numeric_limits<int>::max())
            fail(message(what, " \"", text, "\" is too large"));
        return static_cast<int>(value);
    }

    double real(std::size_t field, const char * what) const
    {
        const std::string & text = m_fields.at(field);
        char * end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (end != text.c_str() + text.size())
            fail(message(what, " \"", text, "\" is not a number"));
        if (!std::isfinite(value)) fail(message(what, " \"", text, "\" is not finite"));
        return value;
    }

    // A count from a header, at least zero and at most MAX_COUNT.
    std::size_t count(std::size_t field, const char * what) const
    {
        const int value = integer(field, what);
        if (value < 0 || value > MAX_COUNT)
            fail(message(what, " is ", value, ", but it must be 0 to ", MAX_COUNT));
        return static_cast<std::size_t>(value);
    }

    // Convert a vertex number to a zero-based index of one of count vertices.
    int vertex(std::size_t field, int first_number, std::size_t count, const char * what) const
    {
        const std::int64_t index = static_cast<std::int64_t>(integer(field, what)) - first_number;
        if (index < 0 || index >= static_cast<std::int64_t>(count))
            fail(message(what, " refers to vertex ", index + first_number,
                         ", but the vertices are ", first_number, " to ",
                         first_number + static_cast<std::int64_t>(count) - 1));
        return static_cast<int>(index);
    }

    [[noreturn]] void fail(const std::string & reason) const
    {
        throw Error(message(m_name, " line ", m_line_number, ": ", reason));
    }

private:
    std::istream & m_input;
    std::string m_name;
    std::size_t m_line_number = 0;
    std::vector<std::string> m_fields;
};

// Read a vertex section: its header and its vertices.
VertexFile read_vertices(LineReader & reader)
{
    VertexFile result;
    // as Triangle does, a header without a dimension, attribute count, or
    // marker count has 2, 0, and 0, and any marker count but 0 means markers
    reader.require(1, "the vertex header");
    const std::size_t count = reader.count(0, "the vertex count");
    if (reader.size() > 1 && reader.integer(1, "the dimension") != 2)
        reader.fail("the dimension must be 2");
    if (reader.size() > 2)
        result.attribute_count = static_cast<int>(reader.count(2, "the vertex attribute count"));
    const int marker_count = reader.optional_integer(3, "the vertex marker count") != 0 ? 1 : 0;

    for (std::size_t vertex = 0; vertex < count; vertex++)
    {
        reader.require(3, "a vertex");
        // as Triangle does, the first vertex sets the numbering if it is 0 or 1
        if (vertex == 0)
        {
            const int number = reader.integer(0, "the vertex number");
            if (number == 0 || number == 1) result.first_number = number;
        }
        result.vertices.push_back(
            {reader.real(1, "the x coordinate"), reader.real(2, "the y coordinate")});
        for (int attribute = 0; attribute < result.attribute_count; attribute++)
        {
            result.attributes.push_back(reader.optional_real(3 + attribute, "a vertex attribute"));
        }
        if (marker_count == 1)
            result.markers.push_back(
                reader.optional_integer(3 + result.attribute_count, "the vertex marker"));
    }
    return result;
}

// An output stream that writes numbers the same in every locale, with the
// digits to read doubles back exactly.
void prepare(std::ostream & output)
{
    output.imbue(std::locale::classic());
    output.precision(std::numeric_limits<double>::max_digits10);
}

// Throw Error if an array does not have the size its counts need.
void check_size(std::size_t size, std::size_t expected, const char * what)
{
    if (size != expected)
        throw Error(message("there are ", size, " ", what, ", but ", expected, " are needed"));
}

void write_vertices(std::ostream & output, const VertexFile & vertices)
{
    if (vertices.attribute_count < 0) throw Error("the vertex attribute count is negative");
    check_size(vertices.attributes.size(),
               vertices.vertices.size() * static_cast<std::size_t>(vertices.attribute_count),
               "vertex attributes");
    if (!vertices.markers.empty())
        check_size(vertices.markers.size(), vertices.vertices.size(), "vertex markers");
    const bool has_markers = !vertices.markers.empty();
    output << vertices.vertices.size() << " 2 " << vertices.attribute_count << " "
           << (has_markers ? 1 : 0) << "\n";
    for (std::size_t vertex = 0; vertex < vertices.vertices.size(); vertex++)
    {
        output << vertices.first_number + static_cast<std::int64_t>(vertex) << " "
               << vertices.vertices[vertex].x << " " << vertices.vertices[vertex].y;
        for (int attribute = 0; attribute < vertices.attribute_count; attribute++)
        {
            output << " " << vertices.attributes[vertex * vertices.attribute_count + attribute];
        }
        if (has_markers) output << " " << vertices.markers[vertex];
        output << "\n";
    }
}

void check_written(std::ostream & output)
{
    if (!output) throw Error("could not write the file");
}

} // namespace

VertexFile read_node(std::istream & input, const std::string & name)
{
    LineReader reader(input, name);
    return read_vertices(reader);
}

TriangleFile read_ele(std::istream & input, const std::string & name, const VertexFile & vertices)
{
    LineReader reader(input, name);
    TriangleFile result;
    // as Triangle does, a header without a corner count or attribute count
    // has 3 and 0, and only the first 3 corners of a triangle are read
    reader.require(1, "the triangle header");
    const std::size_t count = reader.count(0, "the triangle count");
    std::size_t corner_count = 3;
    if (reader.size() > 1)
    {
        const int corners = reader.integer(1, "the corner count");
        if (corners < 3 || corners > 6) reader.fail("the corner count must be 3 to 6");
        corner_count = static_cast<std::size_t>(corners);
    }
    if (reader.size() > 2)
        result.attribute_count = static_cast<int>(reader.count(2, "the triangle attribute count"));

    for (std::size_t triangle = 0; triangle < count; triangle++)
    {
        reader.require(1 + corner_count, "a triangle");
        std::array<int, 3> corners{};
        for (std::size_t corner = 0; corner < 3; corner++)
        {
            corners[corner] = reader.vertex(1 + corner, vertices.first_number,
                                            vertices.vertices.size(), "a triangle corner");
        }
        result.triangles.push_back(corners);
        for (int attribute = 0; attribute < result.attribute_count; attribute++)
        {
            result.attributes.push_back(
                reader.optional_real(1 + corner_count + attribute, "a triangle attribute"));
        }
    }
    return result;
}

PolyFile read_poly(std::istream & input, const std::string & name,
                   const VertexFile * separate_vertices)
{
    LineReader reader(input, name);
    PolyFile result;
    result.vertices = read_vertices(reader);
    const VertexFile * vertices = &result.vertices;
    if (result.vertices.vertices.empty())
    {
        if (separate_vertices == nullptr)
            throw Error(message(name, " has no vertices, and no .node file was given"));
        vertices = separate_vertices;
    }

    reader.require(1, "the segment header");
    const std::size_t segment_count = reader.count(0, "the segment count");
    const int marker_count = reader.optional_integer(1, "the segment marker count") != 0 ? 1 : 0;
    for (std::size_t segment = 0; segment < segment_count; segment++)
    {
        reader.require(3, "a segment");
        result.segments.push_back(
            {reader.vertex(1, vertices->first_number, vertices->vertices.size(), "a segment"),
             reader.vertex(2, vertices->first_number, vertices->vertices.size(), "a segment")});
        if (marker_count == 1)
            result.segment_markers.push_back(reader.optional_integer(3, "the segment marker"));
    }

    reader.require(1, "the hole header");
    const std::size_t hole_count = reader.count(0, "the hole count");
    for (std::size_t hole = 0; hole < hole_count; hole++)
    {
        reader.require(3, "a hole");
        result.holes.push_back(
            {reader.real(1, "the x coordinate"), reader.real(2, "the y coordinate")});
    }

    // the regions are optional
    if (!reader.next()) return result;
    const std::size_t region_count = reader.count(0, "the region count");
    for (std::size_t region = 0; region < region_count; region++)
    {
        reader.require(4, "a region");
        const double attribute = reader.real(3, "the region attribute");
        // as Triangle does, a missing maximum area takes the attribute's value
        const double area_limit =
            reader.size() > 4 ? reader.real(4, "the maximum area") : attribute;
        result.regions.push_back(
            {{reader.real(1, "the x coordinate"), reader.real(2, "the y coordinate")},
             attribute,
             area_limit});
    }
    return result;
}

std::vector<std::array<int, 3>> read_neigh(std::istream & input, const std::string & name,
                                           int first_number, std::size_t triangle_count)
{
    LineReader reader(input, name);
    reader.require(2, "the neighbor header");
    const std::size_t count = reader.count(0, "the triangle count");
    if (count != triangle_count)
        reader.fail(
            message("the file has ", count, " triangles, but the mesh has ", triangle_count));
    if (reader.integer(1, "the neighbor count") != 3) reader.fail("the neighbor count must be 3");

    std::vector<std::array<int, 3>> result;
    for (std::size_t triangle = 0; triangle < count; triangle++)
    {
        reader.require(4, "a triangle");
        std::array<int, 3> neighbors{};
        for (std::size_t corner = 0; corner < 3; corner++)
        {
            const int number = reader.integer(1 + corner, "a neighbor");
            if (number == -1)
            {
                neighbors[corner] = NO_NEIGHBOR;
                continue;
            }
            const std::int64_t index = static_cast<std::int64_t>(number) - first_number;
            if (index < 0 || index >= static_cast<std::int64_t>(count))
                reader.fail(message("neighbor ", number, " is not a triangle of the file"));
            neighbors[corner] = static_cast<int>(index);
        }
        result.push_back(neighbors);
    }
    return result;
}

void write_node(std::ostream & output, const VertexFile & vertices)
{
    prepare(output);
    write_vertices(output, vertices);
    check_written(output);
}

void write_ele(std::ostream & output, const TriangleFile & triangles, int first_number)
{
    if (triangles.attribute_count < 0) throw Error("the triangle attribute count is negative");
    check_size(triangles.attributes.size(),
               triangles.triangles.size() * static_cast<std::size_t>(triangles.attribute_count),
               "triangle attributes");
    prepare(output);
    output << triangles.triangles.size() << " 3 " << triangles.attribute_count << "\n";
    for (std::size_t triangle = 0; triangle < triangles.triangles.size(); triangle++)
    {
        output << first_number + static_cast<std::int64_t>(triangle);
        for (const int corner : triangles.triangles[triangle])
        {
            output << " " << first_number + static_cast<std::int64_t>(corner);
        }
        for (int attribute = 0; attribute < triangles.attribute_count; attribute++)
        {
            output << " " << triangles.attributes[triangle * triangles.attribute_count + attribute];
        }
        output << "\n";
    }
    check_written(output);
}

void write_poly(std::ostream & output, const PolyFile & poly, int first_number)
{
    prepare(output);
    write_vertices(output, poly.vertices);
    if (!poly.vertices.vertices.empty()) first_number = poly.vertices.first_number;
    const bool has_markers = !poly.segment_markers.empty();
    if (has_markers)
        check_size(poly.segment_markers.size(), poly.segments.size(), "segment markers");
    output << poly.segments.size() << " " << (has_markers ? 1 : 0) << "\n";
    for (std::size_t segment = 0; segment < poly.segments.size(); segment++)
    {
        output << first_number + static_cast<std::int64_t>(segment) << " "
               << first_number + static_cast<std::int64_t>(poly.segments[segment][0]) << " "
               << first_number + static_cast<std::int64_t>(poly.segments[segment][1]);
        if (has_markers) output << " " << poly.segment_markers[segment];
        output << "\n";
    }
    output << poly.holes.size() << "\n";
    for (std::size_t hole = 0; hole < poly.holes.size(); hole++)
    {
        output << first_number + static_cast<std::int64_t>(hole) << " " << poly.holes[hole].x << " "
               << poly.holes[hole].y << "\n";
    }
    output << poly.regions.size() << "\n";
    for (std::size_t region = 0; region < poly.regions.size(); region++)
    {
        const Region & item = poly.regions[region];
        output << first_number + static_cast<std::int64_t>(region) << " " << item.point.x << " "
               << item.point.y << " " << item.attribute << " " << item.area_limit << "\n";
    }
    check_written(output);
}

void write_neigh(std::ostream & output, const std::vector<std::array<int, 3>> & neighbors,
                 int first_number)
{
    prepare(output);
    output << neighbors.size() << " 3\n";
    for (std::size_t triangle = 0; triangle < neighbors.size(); triangle++)
    {
        output << first_number + static_cast<std::int64_t>(triangle);
        for (const int neighbor : neighbors[triangle])
        {
            output << " "
                   << (neighbor == NO_NEIGHBOR
                           ? -1
                           : first_number + static_cast<std::int64_t>(neighbor));
        }
        output << "\n";
    }
    check_written(output);
}

} // namespace triangle_utils
