// Calls to Triangle that report its errors as exceptions.

#include "run_triangle.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <new>
#include <string_view>

#include "error.hpp"

namespace triangle_utils
{

namespace
{

// Triangle's messages as one line, without its "Error:" prefix.
std::string triangle_reason(std::string_view messages)
{
    constexpr std::string_view ERROR_PREFIX = "Error:";
    const std::size_t prefix = messages.find(ERROR_PREFIX);
    if (prefix != std::string_view::npos) messages.remove_prefix(prefix + ERROR_PREFIX.size());
    std::string reason;
    for (const char character : messages)
    {
        if (std::isspace(static_cast<unsigned char>(character)))
        {
            if (!reason.empty() && reason.back() != ' ') reason += ' ';
        }
        else
        {
            reason += character;
        }
    }
    if (!reason.empty() && reason.back() == ' ') reason.pop_back();
    return reason;
}

} // namespace

std::string switch_number(double value)
{
    // the digits after the point that give 17 significant digits
    const int decimals = std::max(0, 16 - static_cast<int>(std::floor(std::log10(value))));
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(decimals) << value;
    std::string result = text.str();
    if (result.find('.') != std::string::npos)
    {
        result.erase(result.find_last_not_of('0') + 1);
        if (result.back() == '.') result.pop_back();
    }
    return result;
}

void run_triangle(const std::string & switches, triangulateio & input, TriangleOutput & output,
                  TriangleOutput * voronoi)
{
    const std::string quiet_switches = switches + "Q";
    // the reason is returned in the exception instead
    triangle_set_print(0);
    const int status = triangulate_status(quiet_switches.c_str(), &input, &output.get(),
                                          voronoi != nullptr ? &voronoi->get() : nullptr);
    if (status == 0) return;
    // after an error the output arrays are not valid and must not be freed
    output.get() = triangulateio{};
    if (voronoi != nullptr) voronoi->get() = triangulateio{};
    if (status == 2) throw Error("the refinement test stopped Triangle");
    const std::string reason = triangle_reason(triangle_messages());
    // Triangle's own allocation failure, reported as the core's would be
    if (reason == "Out of memory.") throw std::bad_alloc();
    if (reason.empty()) throw Error("Triangle could not mesh the input and gave no reason");
    throw Error(message("Triangle could not mesh the input: ", reason));
}

} // namespace triangle_utils
