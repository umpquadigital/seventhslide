#include <catch2/catch_test_macros.hpp>

#include <seventhslide/theme/song_theme_pack.hpp>

#include <nlohmann/json.hpp>

#include <string_view>

using nlohmann::json;
using seventhslide::theme::canonical_theme_chip_id;
using seventhslide::theme::SongThemeMap;
using seventhslide::theme::split_order_by_theme_chips;
using seventhslide::theme::verse_order_has_theme_chips;

namespace {

/// What a song stores for ``raw``.
[[nodiscard]] json compact(const json& raw)
{
    return SongThemeMap::from_json(raw).to_json();
}

/// What chip ``chip_id`` of ``raw`` picks, as stored.
[[nodiscard]] json chip_of(const json& raw, std::string_view chip_id)
{
    return SongThemeMap::from_json(raw).chip(chip_id).to_json();
}

}  // namespace

TEST_CASE("canonical theme chip ids", "[theme][song_pack]")
{
    REQUIRE(canonical_theme_chip_id("th1") == "TH1");
    REQUIRE(canonical_theme_chip_id("TH12") == "TH12");
    REQUIRE(canonical_theme_chip_id("t1").empty());
    REQUIRE(canonical_theme_chip_id("v1").empty());
}

TEST_CASE("split verse order by theme chips", "[theme][song_pack]")
{
    const auto segs = split_order_by_theme_chips("TH1 T1 V1 TH2 C1");
    REQUIRE(segs.size() == 2);
    REQUIRE(segs[0].chip_id == "TH1");
    REQUIRE(segs[0].verse_order == "t1 v1");
    REQUIRE(segs[1].chip_id == "TH2");
    REQUIRE(segs[1].verse_order == "c1");

    const auto leading = split_order_by_theme_chips("V1 TH1 C1");
    REQUIRE(leading.size() == 2);
    REQUIRE(leading[0].chip_id.empty());
    REQUIRE(leading[0].verse_order == "v1");
    REQUIRE(leading[1].chip_id == "TH1");
    REQUIRE(leading[1].verse_order == "c1");

    REQUIRE_FALSE(verse_order_has_theme_chips("v1 c1 t1"));
    REQUIRE(verse_order_has_theme_chips("th1 v1"));
}

TEST_CASE("A song theme map without arrangements picks no themes", "[theme][song_pack]")
{
    // A bare per-output map is not a song theme map: nothing reads it as one.
    const json bare = {{"out-main", {{"song", {{"theme", "tid"}, {"bg", "bid"}}}}}};
    REQUIRE(SongThemeMap::from_json(bare).empty());
    REQUIRE(compact(bare).empty());
    REQUIRE(chip_of(bare, "TH1").empty());
    REQUIRE(compact(json::object()).empty());
    REQUIRE(compact(json::array()).empty());
}

TEST_CASE("A song theme map stores arrangements and chip maps", "[theme][song_pack]")
{
    const json stored = {
        {"active", "a1"},
        {"arrangements",
         json::array({json{{"id", "a1"},
                           {"name", "Sunday"},
                           {"order", "TH1 V1 TH2 C1"},
                           {"chips",
                            {{"TH1", {{"out-main", {{"song", {{"theme", "t-a"}, {"bg", "b-a"}}}}}}},
                             {"th2", {{"out-main", {{"song", {{"theme", "t-b"}}}}}}}}}}})}};

    const auto compacted = compact(stored);
    REQUIRE(compacted["active"] == "a1");
    REQUIRE(compacted["arrangements"].size() == 1);
    REQUIRE(compacted["arrangements"][0]["chips"].contains("TH2"));

    REQUIRE(chip_of(stored, "").empty());
    REQUIRE(chip_of(stored, "TH1")["out-main"]["song"]["theme"] == "t-a");
    REQUIRE(chip_of(stored, "TH2")["out-main"]["song"]["theme"] == "t-b");
    REQUIRE_FALSE(chip_of(stored, "TH2")["out-main"]["song"].contains("bg"));
    // A chip is found whatever case the play order spells it in.
    REQUIRE(chip_of(stored, "th1") == chip_of(stored, "TH1"));
    REQUIRE(chip_of(stored, "TH3").empty());

    // What is stored reads back as itself.
    REQUIRE(SongThemeMap::from_json(compacted) == SongThemeMap::from_json(stored));
    REQUIRE(compact(compacted) == compacted);
}

TEST_CASE("Compacting a song theme map tolerates malformed fields", "[theme][song_pack]")
{
    const json stored = {
        {"active", 7},
        {"arrangements",
         json::array({json{{"id", 3}},
                      json{{"id", "a2"}, {"name", 5}, {"chips", "nope"}},
                      json{{"id", "a3"}, {"chips", {{"TH1", {{"out-main", {{"song", {{"theme", 9}}}}}}}}}}})}};
    const auto compacted = compact(stored);
    REQUIRE(compacted["arrangements"].size() == 2);
    REQUIRE(compacted["active"] == "a2");
    REQUIRE(compacted["arrangements"][0]["name"] == "");
    REQUIRE(compacted["arrangements"][1]["chips"]["TH1"].empty());
}

TEST_CASE("A song's theme chip picks only the song's themes", "[theme][song_pack]")
{
    const json stored = {
        {"active", "a1"},
        {"arrangements",
         json::array({json{{"id", "a1"},
                           {"order", "TH1 V1"},
                           {"chips",
                            {{"TH1",
                              {{"out-main",
                                {{"song", {{"theme", "t-a"}}},
                                 {"bible", {{"theme", "t-b"}, {"bg", "b-b"}}},
                                 {"media", {{"theme", "t-m"}}}}}}}}}}})}};
    // A song plays only song looks: a chip's other picks are not kept.
    REQUIRE(chip_of(stored, "TH1") == json{{"out-main", {{"song", {{"theme", "t-a"}}}}}});
    REQUIRE(compact(stored)["arrangements"][0]["chips"]["TH1"]
            == json{{"out-main", {{"song", {{"theme", "t-a"}}}}}});
}

// A song's map is read the one way it is stored: a map that never went
// through a save (synced, imported, edited by hand) plays as it would once
// saved, so the song editor and the output never disagree.
TEST_CASE("A song theme map plays as it is stored", "[theme][song_pack]")
{
    const json pick_a = {{"out-main", {{"song", {{"theme", "t-a"}}}}}};
    const json pick_b = {{"out-main", {{"song", {{"theme", "t-b"}}}}}};

    // An arrangement without an id is not one; with no active named, the
    // first that is plays.
    const json idless_first = {
        {"arrangements",
         json::array({json{{"chips", {{"TH1", pick_a}}}},
                      json{{"id", "a2"}, {"chips", {{"TH1", pick_b}}}}})}};
    REQUIRE(SongThemeMap::from_json(idless_first).active() == "a2");
    REQUIRE(chip_of(idless_first, "TH1") == pick_b);

    // Two spellings of one chip are one chip: the one a save keeps plays.
    const json both_cases = {
        {"active", "a1"},
        {"arrangements",
         json::array({json{{"id", "a1"}, {"chips", {{"TH1", pick_a}, {"th1", pick_b}}}}})}};
    REQUIRE(compact(both_cases)["arrangements"][0]["chips"] == json{{"TH1", pick_b}});
    REQUIRE(chip_of(both_cases, "TH1") == pick_b);
}

TEST_CASE("The song editor's arrangements pack to what a song stores", "[theme][song_theme_pack]")
{
    using seventhslide::theme::k_default_arrangement_name;
    using seventhslide::theme::pack_song_theme_map;
    const nlohmann::json chips = {{"th1", {{"out1", {{"song", {{"theme", " t1 "}}}}}}}, {"V1", {{"x", 1}}}};
    const auto one = [&](std::string name, std::string order) {
        return nlohmann::json::array({{{"id", "a1"}, {"name", name}, {"order", order}, {"chips", chips}}});
    };

    // Only the unnamed default, and no theme chip in its order: no themes of its own.
    CHECK(pack_song_theme_map(one(std::string(k_default_arrangement_name), "V1 C1"), "a1").empty());
    CHECK(pack_song_theme_map(one("", "V1"), "a1").empty());
    CHECK(pack_song_theme_map(nlohmann::json::array(), "a1").empty());

    // A theme chip in the order, or a name of its own, is stored, compacted.
    const auto with_chip = pack_song_theme_map(one("Default", "TH1 V1"), "a1").to_json();
    REQUIRE(with_chip.at("arrangements").size() == 1);
    CHECK(with_chip.at("active") == "a1");
    CHECK(with_chip.at("arrangements")[0].at("chips")
          == nlohmann::json{{"TH1", {{"out1", {{"song", {{"theme", "t1"}}}}}}}});
    CHECK(pack_song_theme_map(one("Live", "V1"), "a1").arrangements()[0].name == "Live");

    // Two arrangements always store; a missing id and name are filled in.
    const auto two = pack_song_theme_map(
        nlohmann::json::array({{{"id", "a1"}, {"name", ""}, {"order", "V1"}},
                               {{"id", ""}, {"name", "Acoustic"}, {"order", "C1"}}}),
        "").to_json();
    CHECK(two.at("active") == "a1");
    CHECK(two.at("arrangements")[0].at("name") == k_default_arrangement_name);
    CHECK(two.at("arrangements")[1].at("id") == "a2");
}
