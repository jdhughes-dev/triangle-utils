// The error the core throws when it cannot complete an action.

#ifndef TRIANGLE_UTILS_ERROR_HPP
#define TRIANGLE_UTILS_ERROR_HPP

#include <sstream>
#include <stdexcept>
#include <string>

namespace triangle_utils
{

// An error with a message that names the input and what is wrong with it.
class Error : public std::runtime_error
{
public:
    explicit Error(const std::string & message) : std::runtime_error(message) {}
};

// A message joined from its parts, which can be strings or numbers.
template <typename... Parts> std::string message(const Parts &... parts)
{
    std::ostringstream text;
    (text << ... << parts);
    return text.str();
}

} // namespace triangle_utils

#endif
