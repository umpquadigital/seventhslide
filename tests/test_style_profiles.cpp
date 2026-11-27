#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/db/style_profile_repository.hpp>
#include <seventhslide/db/style_profiles.hpp>
#include <seventhslide/model/app_settings.hpp>

#include "test_qt_app.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>
#include <vector>

using seventhslide::db::ConfigRepository;
using seventhslide::db::Database;
using seventhslide::db::SongRepository;
using seventhslide::db::StyleProfileRepository;
using seventhslide::model::OutputConfig;
using seventhslide::model::Song;
using seventhslide::db::StyleProfileService;
using seventhslide::db::apply_output_defaults;
using seventhslide::db::blank_profile_data;
using seventhslide::db::k_active_profile_setting;
using seventhslide::db::normalize_profile_data;
using seventhslide::db::snapshot_live;
using seventhslide::db::theme_map_deltas;
using seventhslide::db::verse_order_deltas;

namespace {

nlohmann::json load_fixture()
{
    const std::string path =
        std::string(SEVENTHSLIDE_FIXTURES_DIR) + "/theme/style_profiles.expected.json";
    std::ifstream in(path);
    REQUIRE(in.good());
    nlohmann::json j;
    in >> j;
    return j;
}

Database open_db()
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

OutputConfig make_oc(std::string id, std::string name, nlohmann::json defaults)
{
    OutputConfig oc;
    oc.id = std::move(id);
    oc.name = std::move(name);
    oc.category_defaults = std::move(defaults);
    return oc;
}

/// Blob minus the keys added after the Python oracle, for golden comparison.
nlohmann::json oracle_subset(nlohmann::json blob)
{
    blob.erase(std::string{seventhslide::db::k_profile_key_song_verse_orders});
    blob.erase(std::string{seventhslide::db::k_profile_key_theme_priority});
    return blob;
}

} // namespace

TEST_CASE("Style profile pure blob helpers match the golden fixture", "[theme]")
{
    const auto doc = load_fixture();
    REQUIRE(doc.at("schema") == "seventhslide.style_profiles.v1");

    const auto& before = doc.at("apply").at("before");
    std::vector<OutputConfig> outputs;
    for (auto it = before.at("outputs").begin(); it != before.at("outputs").end(); ++it) {
        outputs.push_back(make_oc(it.key(), it.key(), *it));
    }

    std::vector<std::pair<std::int64_t, nlohmann::json>> songs;
    for (auto it = before.at("songs").begin(); it != before.at("songs").end(); ++it) {
        songs.emplace_back(std::stoll(it.key()), *it);
    }
    seventhslide::db::LiveProfileState live_state;
    live_state.song_theme_maps = songs;

    const auto live = snapshot_live(outputs, live_state);
    // Oracle goldens predate the verse-order / theme-priority keys; compare the
    // parity subset, then assert the post-oracle keys separately.
    REQUIRE(oracle_subset(live) == doc.at("snapshot_live"));
    REQUIRE(oracle_subset(blank_profile_data(live))
            == doc.at("blank_from_live"));

    const auto& profile = doc.at("apply").at("profile");
    apply_output_defaults(outputs, profile.at("outputs"));

    nlohmann::json after_outputs = nlohmann::json::object();
    for (const auto& oc : outputs) {
        after_outputs[oc.name] = oc.category_defaults;
    }
    REQUIRE(after_outputs == doc.at("apply").at("after").at("outputs"));

    const auto song_deltas = theme_map_deltas(songs, profile.at("songs"));

    nlohmann::json song_delta_json = nlohmann::json::array();
    for (const auto& [id, tm] : song_deltas) {
        song_delta_json.push_back({{"id", id}, {"theme_map", tm}});
    }
    REQUIRE(song_delta_json == doc.at("apply").at("song_deltas"));

    // Second apply on already-matching state -> no deltas.
    std::vector<std::pair<std::int64_t, nlohmann::json>> songs_after;
    for (auto it = doc.at("apply").at("after").at("songs").begin();
         it != doc.at("apply").at("after").at("songs").end();
         ++it) {
        songs_after.emplace_back(std::stoll(it.key()), *it);
    }
    REQUIRE(theme_map_deltas(songs_after, profile.at("songs")).empty());
    REQUIRE(doc.at("apply").at("noop_second_apply_song_writes").empty());
}

TEST_CASE("StyleProfileService seed activate switch capture", "[theme][db]")
{
    Database db = open_db();
    StyleProfileRepository profiles(db);
    SongRepository songs(db);
    ConfigRepository config(db);

    std::vector<OutputConfig> outputs = {
        make_oc("out-main", "Main", {{"song", {{"theme", "t_live"}, {"bg", "b_live"}}}}),
    };

    Song s1;
    s1.title = "One";
    s1.lyrics = "v1\nHi";
    s1.theme_map = {{"out-main", {{"song", {{"theme", "song_live"}}}}}};
    const auto sid = songs.insert(s1);

    StyleProfileService svc(profiles, songs, config, outputs);
    REQUIRE_FALSE(svc.active_profile_id().has_value());

    svc.ensure_seeded();
    const auto default_id = svc.active_profile_id();
    REQUIRE(default_id);
    auto def = profiles.get(*default_id);
    REQUIRE(def);
    REQUIRE(def->name == "Default");
    REQUIRE(def->data["songs"][std::to_string(sid)]["out-main"]["song"]["theme"] == "song_live");
    REQUIRE_FALSE(def->data.contains("ann_items"));

    // Blank profile: same output defaults, no library overrides.
    const auto blank_id = svc.create_blank("Clean");
    REQUIRE(svc.activate(blank_id));
    REQUIRE(svc.active_profile_id() == blank_id);
    REQUIRE(outputs[0].category_defaults["song"]["theme"] == "t_live");
    REQUIRE(songs.get(sid)->theme_map.empty());

    // Edit live while Clean is active, then switch back to Default -> captures Clean.
    outputs[0].category_defaults = {{"song", {{"theme", "t_clean_edit"}, {"bg", "b_x"}}}};
    songs.set_theme_maps({{sid, {{"out-main", {{"song", {{"theme", "during_clean"}}}}}}}});

    REQUIRE(svc.activate(*default_id));
    REQUIRE(outputs[0].category_defaults["song"]["theme"] == "t_live");
    REQUIRE(songs.get(sid)->theme_map["out-main"]["song"]["theme"] == "song_live");

    auto clean = profiles.get(blank_id);
    REQUIRE(clean);
    REQUIRE(clean->data["outputs"]["out-main"]["song"]["theme"] == "t_clean_edit");
    REQUIRE(clean->data["songs"][std::to_string(sid)]["out-main"]["song"]["theme"] == "during_clean");

    // Duplicate captures active first.
    outputs[0].category_defaults = {{"song", {{"theme", "t_dup_src"}}}};
    const auto dup_id = svc.duplicate(*default_id, "Copy");
    REQUIRE(dup_id);
    auto dup = profiles.get(*dup_id);
    REQUIRE(dup);
    REQUIRE(dup->data["outputs"]["out-main"]["song"]["theme"] == "t_dup_src");

    // Cannot delete last remaining after removing others carefully.
    REQUIRE(svc.remove(blank_id));
    REQUIRE(svc.remove(*dup_id));
    REQUIRE_FALSE(svc.remove(*default_id));  // last profile
    REQUIRE(profiles.list_summaries().size() == 1);
}

TEST_CASE("A profile switch that fails leaves the live outputs as they were", "[theme][db]")
{
    Database db = open_db();
    StyleProfileRepository profiles(db);
    SongRepository songs(db);
    ConfigRepository config(db);
    std::vector<OutputConfig> outputs = {
        make_oc("out-main", "Main", {{"song", {{"theme", "t_live"}, {"bg", "b_live"}}}}),
    };
    StyleProfileService svc(profiles, songs, config, outputs);
    svc.ensure_seeded();
    const auto default_id = svc.active_profile_id();
    REQUIRE(default_id);
    const auto other = svc.create_blank("Other");
    auto data = profiles.get(other)->data;
    data["outputs"]["out-main"]["song"]["theme"] = "t_other";
    profiles.save_data(other, data);

    // Saving the outputs, the last step of the switch, fails.
    db.exec("CREATE TRIGGER no_outputs BEFORE INSERT ON outputs "
            "BEGIN SELECT RAISE(ABORT, 'the disk is full'); END");
    REQUIRE_THROWS(svc.activate(other));
    CHECK(outputs[0].category_defaults["song"]["theme"] == "t_live");
    CHECK(svc.active_profile_id() == default_id);

    db.exec("DROP TRIGGER no_outputs");
    REQUIRE(svc.activate(other));
    CHECK(outputs[0].category_defaults["song"]["theme"] == "t_other");
}

TEST_CASE("StyleProfileService ensure_seeded repairs stale id without apply", "[theme][db]")
{
    Database db = open_db();
    StyleProfileRepository profiles(db);
    SongRepository songs(db);
    ConfigRepository config(db);

    std::vector<OutputConfig> outputs = {
        make_oc("out-main", "Main", {{"song", {{"theme", "keep_me"}}}}),
    };

    const auto pid = profiles.create(
        "Existing",
        nlohmann::json{
            {"outputs", {{"out-main", {{"song", {{"theme", "from_profile"}}}}}}},
            {"songs", nlohmann::json::object()},
            {"ann_items", nlohmann::json::object()},
        });

    config.set_setting(k_active_profile_setting, 99999);  // stale
    StyleProfileService svc(profiles, songs, config, outputs);
    svc.ensure_seeded();

    REQUIRE(svc.active_profile_id() == pid);
    // Repair must NOT apply the profile blob onto live outputs.
    REQUIRE(outputs[0].category_defaults["song"]["theme"] == "keep_me");
}

TEST_CASE("normalize_profile_data fills missing keys", "[theme]")
{
    const auto n = normalize_profile_data(nlohmann::json::object());
    REQUIRE(n.contains("outputs"));
    REQUIRE(n.contains("songs"));
    REQUIRE_FALSE(n.contains("ann_items"));
    REQUIRE(n.at("song_verse_orders").is_object());
    REQUIRE(n.at("theme_priority").is_array());
    REQUIRE(normalize_profile_data(nullptr)["songs"].is_object());
}

TEST_CASE("Profiles carry song verse order and theme priority", "[theme][db]")
{
    using seventhslide::model::k_setting_theme_priority;

    Database db = open_db();
    StyleProfileRepository profiles(db);
    SongRepository songs(db);
    ConfigRepository config(db);

    std::vector<OutputConfig> outputs = {make_oc("out-main", "Main", nlohmann::json::object())};

    Song song;
    song.title = "Amazing Grace";
    song.lyrics = "v1\nline";
    song.verse_order = "V1 C1 V2";
    const auto sid = songs.insert(song);
    config.set_setting(k_setting_theme_priority,
                       nlohmann::json::array({"global", "service", "content"}));

    StyleProfileService svc(profiles, songs, config, outputs);
    svc.ensure_seeded();
    const auto sunday = svc.active_profile_id();
    REQUIRE(sunday);

    // A blank profile owns both keys from birth: no verse overrides, live priority.
    const auto spanish = svc.create_blank("Spanish");
    REQUIRE(svc.activate(spanish));
    REQUIRE(songs.get(sid)->verse_order.empty());
    REQUIRE(config.get_setting(k_setting_theme_priority)->at(0) == "global");

    // Edit live under Spanish, then switch back - each profile keeps its own.
    songs.set_verse_orders({{sid, "V1 V1 C1"}});
    config.set_setting(k_setting_theme_priority,
                       nlohmann::json::array({"content", "service", "global"}));

    REQUIRE(svc.activate(*sunday));
    REQUIRE(songs.get(sid)->verse_order == "V1 C1 V2");
    REQUIRE(config.get_setting(k_setting_theme_priority)->at(0) == "global");

    REQUIRE(svc.activate(spanish));
    REQUIRE(songs.get(sid)->verse_order == "V1 V1 C1");
    REQUIRE(config.get_setting(k_setting_theme_priority)->at(0) == "content");
}

TEST_CASE("Profile deltas read a missing or mistyped snapshot entry as empty", "[theme]")
{
    using Orders = std::vector<std::pair<std::int64_t, std::string>>;
    const Orders orders{{1, "V1 C1"}, {2, ""}, {3, "V1"}, {4, "C1"}};
    // 1 matches; 2 is missing, which reads as empty and so matches; 3 is not
    // a string and 4 is null, which both read as empty.
    const nlohmann::json snap = {{"1", "V1 C1"}, {"3", 7}, {"4", nullptr}};
    CHECK(verse_order_deltas(orders, snap) == Orders{{3, ""}, {4, ""}});
    // A snapshot that is not an object holds nothing.
    CHECK(verse_order_deltas(orders, nlohmann::json::array()) == Orders{{1, ""}, {3, ""}, {4, ""}});

    using Maps = std::vector<std::pair<std::int64_t, nlohmann::json>>;
    const Maps maps{{1, {{"a", 1}}}, {2, nullptr}, {3, nlohmann::json::object()}, {4, {{"b", 2}}}};
    // 1 matches; a live null is an empty map, so 2 (missing) and 3 (a
    // string, read as empty) match too; 4's null snapshot is empty.
    const nlohmann::json map_snap = {{"1", {{"a", 1}}}, {"3", "x"}, {"4", nullptr}};
    CHECK(theme_map_deltas(maps, map_snap) == Maps{{4, nlohmann::json::object()}});
    CHECK(theme_map_deltas(maps, nlohmann::json("x"))
          == Maps{{1, nlohmann::json::object()}, {4, nlohmann::json::object()}});
}
