// Tests the version macros and the Triangle version.

#include <string>

#include <gtest/gtest.h>

#include "triangle_status.h"
#include "triangle_utils_version.h"

TEST(Version, MacrosMatchVersionString)
{
  std::string parts = std::to_string(TRIANGLE_UTILS_VERSION_MAJOR) + "." +
                      std::to_string(TRIANGLE_UTILS_VERSION_MINOR) + "." +
                      std::to_string(TRIANGLE_UTILS_VERSION_PATCH);
  EXPECT_EQ(parts, TRIANGLE_UTILS_VERSION);
  EXPECT_STREQ(triangle_version(), "1.6");
}
