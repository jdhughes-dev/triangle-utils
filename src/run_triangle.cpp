// Calls to Triangle that report its errors as exceptions.

#include "run_triangle.hpp"

#include <cctype>
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
    if (reason.empty()) throw Error("Triangle could not mesh the input and gave no reason");
    throw Error(message("Triangle could not mesh the input: ", reason));
}

} // namespace triangle_utils
