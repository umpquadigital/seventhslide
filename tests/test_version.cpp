#include <catch2/catch_test_macros.hpp>

#include <seventhslide/version.hpp>

TEST_CASE("version string is non-empty semver-like", "[smoke]")
{
    const auto v = seventhslide::version_string();
    REQUIRE_FALSE(v.empty());
    REQUIRE(v.find('.') != std::string_view::npos);
    // Filled in from the CMake project version, so never left as the placeholder.
    REQUIRE(v.find('@') == std::string_view::npos);
}
