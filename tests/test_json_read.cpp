// The one set of tolerant JSON readers, shared by model, theme, scene and the
// painters: wrong types, non-objects, and whether a float rounds or truncates.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/json/read.hpp>

#include <string>
#include <vector>

using seventhslide::json::array_or_empty;
using seventhslide::json::bool_or;
using seventhslide::json::int_or;
using seventhslide::json::num_or;
using seventhslide::json::object_or_empty;
using seventhslide::json::str_or;
using seventhslide::json::strings_in;
using seventhslide::json::strings_or_empty;
using seventhslide::json::style_bool;
using seventhslide::json::style_int;

TEST_CASE("readers fall back for missing keys, wrong types and non-objects", "[json][read]")
{
    const auto obj = nlohmann::json::parse(R"({"s":"x","n":2.5,"b":true,"z":null})");

    REQUIRE(str_or(obj, "s") == "x");
    REQUIRE(str_or(obj, "missing", "fb") == "fb");
    REQUIRE(str_or(obj, "n", "fb") == "fb");   // wrong type
    REQUIRE(str_or(obj, "z", "fb") == "fb");   // null
    REQUIRE(num_or(obj, "n", 0.0) == 2.5);
    REQUIRE(num_or(obj, "s", 1.0) == 1.0);
    REQUIRE(bool_or(obj, "b", false));
    REQUIRE(bool_or(obj, "s", true));

    // A value that is not an object at all reads as all fallbacks, rather than
    // indexing something that has no keys.
    for (const auto& not_object : {nlohmann::json(nullptr), nlohmann::json(7),
                                   nlohmann::json("str"), nlohmann::json::array({1, 2})}) {
        REQUIRE(str_or(not_object, "s", "fb") == "fb");
        REQUIRE(num_or(not_object, "n", 1.5) == 1.5);
        REQUIRE(int_or(not_object, "n", 3) == 3);
        REQUIRE(bool_or(not_object, "b", true));
        REQUIRE(object_or_empty(not_object, "o").empty());
        REQUIRE(array_or_empty(not_object, "a").empty());
    }
}

TEST_CASE("int_or rounds and style_int truncates", "[json][read]")
{
    // Geometry from a theme means the nearest pixel; the flat style keys the
    // painters read have always truncated, and their output is pixel-compared.
    const auto obj = nlohmann::json::parse(R"({"a":47.6,"b":-47.6,"c":48})");

    REQUIRE(int_or(obj, "a", 0) == 48);
    REQUIRE(int_or(obj, "b", 0) == -48);
    REQUIRE(int_or(obj, "c", 0) == 48);

    REQUIRE(style_int(obj, "a", 0) == 47);
    REQUIRE(style_int(obj, "b", 0) == -47);
    REQUIRE(style_int(obj, "c", 0) == 48);
    REQUIRE(style_int(obj, "missing", 9) == 9);
}

TEST_CASE("int_or gives the fallback for a number no int holds", "[json][read]")
{
    // A stored port or height edited by hand: not whatever its cast wraps to.
    const auto obj = nlohmann::json::parse(R"({"big":1e12,"small":-1e12,"edge":2147483647})");
    CHECK(int_or(obj, "big", 7) == 7);
    CHECK(int_or(obj, "small", 7) == 7);
    CHECK(int_or(obj, "edge", 7) == 2147483647);
}

TEST_CASE("bool_or takes 0 / 1 but style_bool does not", "[json][read]")
{
    // Themes written by hand (and by the old build) store some switches as
    // integers; the painters' own keys are always real booleans.
    const auto obj = nlohmann::json::parse(R"({"one":1,"zero":0})");

    REQUIRE(bool_or(obj, "one", false));
    REQUIRE_FALSE(bool_or(obj, "zero", true));
    REQUIRE(style_bool(obj, "one", false) == false);
    REQUIRE(style_bool(obj, "zero", true) == true);
}

TEST_CASE("object_or_empty and array_or_empty keep the shape", "[json][read]")
{
    const auto obj = nlohmann::json::parse(R"({"o":{"k":1},"a":[1,2],"s":"x"})");

    REQUIRE(object_or_empty(obj, "o").at("k") == 1);
    REQUIRE(object_or_empty(obj, "a").is_object());
    REQUIRE(object_or_empty(obj, "a").empty());
    REQUIRE(array_or_empty(obj, "a").size() == 2);
    REQUIRE(array_or_empty(obj, "s").is_array());
    REQUIRE(array_or_empty(obj, "s").empty());
}

TEST_CASE("strings_in keeps only the strings of an array", "[json][read]")
{
    using Strings = std::vector<std::string>;
    const auto obj = nlohmann::json::parse(R"({"a":["x",1,null,"y",["z"]],"s":"x"})");

    REQUIRE(strings_in(obj.at("a")) == Strings{"x", "y"});
    REQUIRE(strings_in(obj.at("s")).empty());
    REQUIRE(strings_in(nullptr).empty());
    REQUIRE(strings_or_empty(obj, "a") == Strings{"x", "y"});
    REQUIRE(strings_or_empty(obj, "s").empty());
    REQUIRE(strings_or_empty(obj, "missing").empty());
    REQUIRE(strings_or_empty(nlohmann::json::array({"x"}), "a").empty());
}
