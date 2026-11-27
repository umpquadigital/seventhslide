#include <catch2/catch_test_macros.hpp>

#include <seventhslide/util/hash.hpp>

using seventhslide::util::fnv1a32_hex;
using seventhslide::util::fnv1a64_hex;

// Theme files are named with these hashes, so a change would orphan files
// already on disk: they must stay the published FNV-1a values.
TEST_CASE("fnv1a hex matches the published FNV-1a values", "[util]")
{
    CHECK(fnv1a32_hex("") == "811c9dc5");
    CHECK(fnv1a32_hex("a") == "e40c292c");
    CHECK(fnv1a32_hex("foobar") == "bf9cf968");
    CHECK(fnv1a64_hex("") == "cbf29ce484222325");
    CHECK(fnv1a64_hex("a") == "af63dc4c8601ec8c");
    CHECK(fnv1a64_hex("foobar") == "85944171f73967e8");
}

TEST_CASE("fnv1a hex keeps leading zeros and hashes bytes as unsigned", "[util]")
{
    CHECK(fnv1a32_hex("\x01") == "040c5b8c");
    CHECK(fnv1a64_hex("\xc3\xa9") == "0ac21707b7181e01");
    // Bytes past ASCII hash the same whether or not char is signed.
    CHECK(fnv1a32_hex("\xc3\xa9") == "1e9de8c1");
}
