// This computer's signing key file (library/computer_key.hpp).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <seventhslide/library/computer_key.hpp>

#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

TEST_CASE("A computer's key is made once and kept for its user alone", "[library][sync]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir dir;
    const auto data = dir.path / "data";
    const auto file = seventhslide::library::computer_key_file(data);
    CHECK_FALSE(std::filesystem::exists(file));

    const auto key = seventhslide::library::load_computer_key(data);
    REQUIRE(std::filesystem::exists(file));
    CHECK(seventhslide::library::load_computer_key(data) == key);
#ifndef _WIN32
    const auto perms = std::filesystem::status(file).permissions();
    CHECK((perms & std::filesystem::perms::group_all) == std::filesystem::perms::none);
    CHECK((perms & std::filesystem::perms::others_all) == std::filesystem::perms::none);
#endif

    SECTION("a file that is not a key is not replaced")
    {
        std::ofstream(file) << "not a key";
        CHECK_THROWS_WITH(seventhslide::library::load_computer_key(data),
                          Catch::Matchers::ContainsSubstring("not a key"));
        std::ifstream in(file);
        std::string contents;
        std::getline(in, contents);
        CHECK(contents == "not a key");
    }
}
