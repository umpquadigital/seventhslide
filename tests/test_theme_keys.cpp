// Structural invariants of the hand-maintained theme key tables. Two properties
// are load-bearing and silent when broken:
//
//   * ``key_in`` binary-searches, so an out-of-order array makes an existing key
//     stop resolving - themes lose a field with no error anywhere.
//   * The text and background tables do not share a key. A key claimed by both
//     would let one theme kind overwrite the other when both are applied.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/model/theme_keys.hpp>

#include <algorithm>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace seventhslide::model;

namespace {

std::vector<std::string_view> as_vector(std::span<const std::string_view> keys)
{
    return {keys.begin(), keys.end()};
}

std::set<std::string_view> as_set(std::span<const std::string_view> keys)
{
    return {keys.begin(), keys.end()};
}

/// Names shared by two key sets, for a readable failure message.
std::vector<std::string_view> overlap(const std::set<std::string_view>& a,
                                      const std::set<std::string_view>& b)
{
    std::vector<std::string_view> out;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
    return out;
}

std::string join(const std::vector<std::string_view>& names)
{
    std::string out;
    for (const std::string_view n : names) {
        if (!out.empty()) {
            out += ", ";
        }
        out += std::string(n);
    }
    return out;
}

struct Table {
    const char* name;
    std::span<const std::string_view> keys;
};

/// Every table reached through ``key_in``. Sortedness is a correctness
/// requirement for these and these only - the priority / category tables below
/// are ordered by meaning instead.
const std::vector<Table>& binary_searched_tables()
{
    static const std::vector<Table> tables{
        {"k_song_theme_keys", k_song_theme_keys},
        {"k_bible_theme_keys", k_bible_theme_keys},
        {"k_media_theme_keys", k_media_theme_keys},
        {"k_bg_theme_keys", k_bg_theme_keys},
        {"k_output_structural_keys", k_output_structural_keys},
    };
    return tables;
}

/// Every key some kind of text theme stores. The kinds share ``elements``
/// and ``transitions``, each holding only its own contexts' in them.
std::set<std::string_view> text_theme_keys()
{
    auto keys = as_set(k_song_theme_keys);
    for (const auto k : k_bible_theme_keys) {
        keys.insert(k);
    }
    for (const auto k : k_media_theme_keys) {
        keys.insert(k);
    }
    return keys;
}

} // namespace

TEST_CASE("theme key tables reached by key_in are sorted", "[model][theme_keys]")
{
    for (const Table& t : binary_searched_tables()) {
        INFO("table " << t.name);
        const std::vector<std::string_view> keys = as_vector(t.keys);
        REQUIRE_FALSE(keys.empty());

        auto out_of_order = std::adjacent_find(
            keys.begin(), keys.end(),
            [](std::string_view a, std::string_view b) { return a >= b; });
        if (out_of_order != keys.end()) {
            // Names the offending pair rather than just failing on a bool.
            INFO("out of order: \"" << *out_of_order << "\" precedes \""
                                    << *std::next(out_of_order) << "\"");
            REQUIRE(std::is_sorted(keys.begin(), keys.end()));
        }
        REQUIRE(std::is_sorted(keys.begin(), keys.end()));
    }
}

TEST_CASE("theme key tables hold no duplicates", "[model][theme_keys]")
{
    const std::vector<Table> tables{
        {"k_song_theme_keys", k_song_theme_keys},
        {"k_bible_theme_keys", k_bible_theme_keys},
        {"k_media_theme_keys", k_media_theme_keys},
        {"k_bg_theme_keys", k_bg_theme_keys},
        {"k_output_structural_keys", k_output_structural_keys},
        {"k_output_intrinsic_keys", k_output_intrinsic_keys},
        {"k_default_theme_priority", k_default_theme_priority},
    };
    for (const Table& t : tables) {
        INFO("table " << t.name);
        REQUIRE(as_set(t.keys).size() == t.keys.size());
    }
}

TEST_CASE("text, background and intrinsic keys stay disjoint", "[model][theme_keys]")
{
    const std::set<std::string_view> text = text_theme_keys();
    const std::set<std::string_view> bg = as_set(k_bg_theme_keys);
    const std::set<std::string_view> intrinsic =
        as_set(k_output_intrinsic_keys);
    REQUIRE(overlap(text, bg).empty());
    REQUIRE(overlap(text, intrinsic).empty());
    REQUIRE(overlap(bg, intrinsic).empty());
}

TEST_CASE("text and bg theme keys never overlap", "[model][theme_keys]")
{
    // A key claimed by both kinds would let a background theme silently
    // overwrite a text theme's field (and the reverse) when both are applied.
    const std::set<std::string_view> text = text_theme_keys();
    const std::set<std::string_view> bg = as_set(k_bg_theme_keys);

    const std::vector<std::string_view> both = overlap(text, bg);
    INFO("claimed by both text and bg themes: " << join(both));
    REQUIRE(both.empty());
}

TEST_CASE("theme keys and structural keys are disjoint", "[model][theme_keys]")
{
    const std::set<std::string_view> structural =
        as_set(k_output_structural_keys);
    const std::set<std::string_view> text = text_theme_keys();
    const std::set<std::string_view> bg = as_set(k_bg_theme_keys);
    REQUIRE(overlap(text, structural).empty());
    REQUIRE(overlap(bg, structural).empty());
}

TEST_CASE("key_in answers for real members and non-members", "[model][theme_keys]")
{
    // Guards the binary search itself: first, last, and middle elements of a
    // table must all resolve, and a near-miss must not.
    for (const Table& t : binary_searched_tables()) {
        INFO("table " << t.name);
        REQUIRE(key_in(t.keys.front(), t.keys));
        REQUIRE(key_in(t.keys.back(), t.keys));
        REQUIRE(key_in(t.keys[t.keys.size() / 2], t.keys));
        REQUIRE_FALSE(key_in("", t.keys));
        REQUIRE_FALSE(key_in("zzz_not_a_key", t.keys));
    }

    REQUIRE(is_bg_theme_key(k_bg_theme_keys[0]));
    REQUIRE_FALSE(is_bg_theme_key("font_family"));
    REQUIRE_FALSE(key_in("name", k_song_theme_keys));
    REQUIRE_FALSE(key_in("is_blank", k_song_theme_keys));
}

TEST_CASE("theme priority normalization keeps permutations only", "[model][theme_keys]")
{
    const std::vector<std::string> defaults(std::begin(k_default_theme_priority),
                                            std::end(k_default_theme_priority));

    // The default is itself a valid permutation, so it survives normalization.
    REQUIRE(normalize_theme_priority(defaults) == defaults);

    // A permutation is kept in the operator's order, not re-sorted.
    std::vector<std::string> reversed(defaults.rbegin(), defaults.rend());
    REQUIRE(normalize_theme_priority(reversed) == reversed);

    // Surrounding whitespace is not part of a tier name.
    std::vector<std::string> padded;
    for (const auto& tier : reversed) {
        padded.push_back(" " + tier + "\t");
    }
    REQUIRE(normalize_theme_priority(padded) == reversed);

    // Anything that is not a permutation resets to the default.
    REQUIRE(normalize_theme_priority({}) == defaults);
    REQUIRE(normalize_theme_priority({"global"}) == defaults);
    REQUIRE(normalize_theme_priority({"global", "global", "global"}) == defaults);
    REQUIRE(normalize_theme_priority({"global", "content", "nonsense"}) == defaults);
}
