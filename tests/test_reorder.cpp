#include <seventhslide/util/reorder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using seventhslide::util::ReorderError;
using seventhslide::util::reorder_by_key;

namespace {

struct Row {
    std::string key;
    int payload = 0;

    bool operator==(const Row&) const = default;
};

const auto k_key_of = [](const Row& r) { return r.key; };

}  // namespace

TEST_CASE("reorder_by_key applies a full order", "[util][reorder]")
{
    std::vector<Row> rows{{"a", 1}, {"b", 2}, {"c", 3}};
    REQUIRE(reorder_by_key(rows, std::vector<std::string>{"c", "a", "b"}, k_key_of)
            == ReorderError::None);
    REQUIRE(rows == std::vector<Row>{{"c", 3}, {"a", 1}, {"b", 2}});
}

TEST_CASE("reorder_by_key reports the first problem and moves nothing", "[util][reorder]")
{
    const std::vector<Row> before{{"a", 1}, {"b", 2}, {"c", 3}};
    auto rows = before;

    REQUIRE(reorder_by_key(rows, std::vector<std::string>{"a", "b"}, k_key_of)
            == ReorderError::WrongCount);
    REQUIRE(reorder_by_key(rows, std::vector<std::string>{"b", "b", "x"}, k_key_of)
            == ReorderError::DuplicateKey);
    REQUIRE(reorder_by_key(rows, std::vector<std::string>{"b", "x", "b"}, k_key_of)
            == ReorderError::UnknownKey);
    REQUIRE(rows == before);
}

TEST_CASE("reorder_by_key never drops an element that shares a key", "[util][reorder]")
{
    // Two elements with one key cannot both be named, so no order is complete.
    const std::vector<Row> before{{"a", 1}, {"a", 2}, {"b", 3}};
    auto rows = before;
    REQUIRE(reorder_by_key(rows, std::vector<std::string>{"b", "a", "c"}, k_key_of)
            == ReorderError::UnknownKey);
    REQUIRE(rows == before);
}
