#pragma once

// The golden fixtures under fixtures/ in the source tree, for the tests that
// check against them.

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

namespace seventhslide::test {

/// ``fixtures/<sub>`` in the source tree.
[[nodiscard]] inline std::filesystem::path fixtures_dir(std::string_view sub)
{
#ifdef SEVENTHSLIDE_FIXTURES_DIR
    return std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / sub;
#else
    return std::filesystem::path{"fixtures"} / sub;
#endif
}

/// The layout oracle's songs and their manifest (fixtures/oracle).
[[nodiscard]] inline std::filesystem::path oracle_dir()
{
    return fixtures_dir("oracle");
}

/// The JSON in ``path``, which must be there.
[[nodiscard]] inline nlohmann::json load_json(const std::filesystem::path& path)
{
    std::ifstream in(path);
    REQUIRE(in);
    nlohmann::json j;
    in >> j;
    return j;
}

}  // namespace seventhslide::test
