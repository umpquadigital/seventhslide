// Every table that syncs read as records and written back (sync_records.hpp):
// the documents carry uids where the library holds its own ids, so a record
// copied into another library, whose ids differ, points at the same things.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/sync_records.hpp>
#include <seventhslide/db/sync_tables.hpp>

#include "sync_test_helpers.hpp"
#include "test_qt_app.hpp"

#include <QSqlQuery>
#include <QString>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using nlohmann::json;
using seventhslide::db::Database;
using seventhslide::db::delete_record;
using seventhslide::db::read_record;
using seventhslide::db::record_keys;
using seventhslide::db::write_record;

namespace {

Database current_library()
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

/// A font's row. Not in ``fill_library``, whose rows' files the tests of moving
/// them make one by one.
void add_font(Database& db)
{
    db.exec("INSERT INTO font_files (filename, display_name, family, style, bold, italic, "
            "variable, added_at) VALUES ('ab90.ttf', 'Lato Bold', 'Lato', 'Bold', 1, 0, 0, 1000)");
}

std::string text(Database& db, const std::string& sql)
{
    QSqlQuery q(db.handle());
    REQUIRE(q.exec(QString::fromStdString(sql)));
    REQUIRE(q.next());
    return q.value(0).toString().toStdString();
}

std::int64_t number(Database& db, const std::string& sql)
{
    return db.query_int64(sql, -1);
}

std::string uid(Database& db, const std::string& table, std::int64_t id)
{
    return text(db, "SELECT uid FROM " + table + " WHERE id = " + std::to_string(id));
}

/// Rows added and deleted, so this library's next ids are not another's.
void shift_ids(Database& db)
{
    for (const std::string table : {"songs", "bibles", "style_profiles", "image_folders",
                                    "video_folders", "audio_folders", "freeform_folders",
                                    "services", "freeform_items", "web_folders", "web_pages"}) {
        const std::string row = table == "songs"       ? "(title, lyrics) VALUES ('x', '')"
                                : table == "web_pages" ? "(name, url) VALUES ('x', 'x')"
                                                       : "(name) VALUES ('x')";
        for (int i = 0; i < 5; ++i) {
            db.exec("INSERT INTO " + table + " " + row);
        }
        db.exec("DELETE FROM " + table);
    }
}

}  // namespace

TEST_CASE("A record's document holds uids, never this library's ids", "[db][sync]")
{
    Database db = current_library();
    seventhslide::test::fill_library(db);
    const auto song = uid(db, "songs", 1);

    const auto amazing = read_record(db, "songs", song);
    REQUIRE(amazing);
    CHECK((*amazing)["title"] == "Amazing Grace");
    CHECK((*amazing)["key"] == "G");
    CHECK_FALSE(amazing->contains("id"));
    CHECK_FALSE(amazing->contains("uid"));

    const auto item = read_record(db, "service_items",
                                  text(db, "SELECT uid FROM service_items WHERE order_num = 0"));
    REQUIRE(item);
    CHECK((*item)["service_id"] == uid(db, "services", 1));
    CHECK((*item)["song_id"] == song);

    const auto passage = read_record(db, "service_items",
                                     text(db, "SELECT uid FROM service_items WHERE order_num = 1"));
    REQUIRE(passage);
    CHECK(json::parse((*passage)["data"].get<std::string>())["bible_id"] == uid(db, "bibles", 1));
    const auto folder = read_record(db, "service_items",
                                    text(db, "SELECT uid FROM service_items WHERE order_num = 2"));
    REQUIRE(folder);
    CHECK(json::parse((*folder)["data"].get<std::string>())["folder_id"]
          == uid(db, "image_folders", 2));
    // The rows that go live with it, by uid.
    CHECK(json::parse((*folder)["data"].get<std::string>())["also_play"]
          == json::array({text(db, "SELECT uid FROM service_items WHERE order_num = 0")}));

    // A profile's songs are keyed by uid; a song no longer here is left out.
    const auto profile = read_record(db, "style_profiles", uid(db, "style_profiles", 1));
    REQUIRE(profile);
    const auto data = json::parse((*profile)["data"].get<std::string>());
    CHECK(data["songs"] == json{{song, {{"main", "t1"}}}});
    CHECK(data["song_verse_orders"] == json{{uid(db, "songs", 2), "V1 C"}});

    const auto active = read_record(db, "app_settings", "active_profile_id");
    REQUIRE(active);
    CHECK(json::parse((*active)["value"].get<std::string>()) == uid(db, "style_profiles", 1));
    // A setting without a reference is the text it was.
    CHECK(read_record(db, "app_settings", "songs_sort") == json{{"value", "\"title\""}});

    // A folder item is named by its folder's uid; the key says which folder
    // and file, so the document holds only the rest.
    const auto key = uid(db, "image_folders", 2) + "/ab12.png";
    CHECK(read_record(db, "image_folder_items", key) == json{{"sort_order", 3}});

    // A word list is the JSON text it was.
    const auto service = read_record(db, "services", uid(db, "services", 1));
    REQUIRE(service);
    CHECK((*service)["tags"] == "[\"Easter\"]");
}

TEST_CASE("A Bible's document holds its verses in the order they were added", "[db][sync]")
{
    Database db = current_library();
    seventhslide::test::fill_library(db);
    const auto bible = read_record(db, "bibles", uid(db, "bibles", 1));
    REQUIRE(bible);
    CHECK((*bible)["abbreviation"] == "KJV");
    CHECK((*bible)["verses"]
          == json{{"columns", {"book", "chapter", "verse_num", "text"}},
                  {"rows",
                   {{"John", 3, 16, "For God so loved"}, {"Genesis", 1, 1, "In the beginning"}}}});
}

TEST_CASE("Every column but the local names travels in a record's document", "[db][sync]")
{
    Database db = current_library();
    seventhslide::test::fill_library(db);
    add_font(db);
    for (const auto table : seventhslide::db::record_tables_in_order()) {
        INFO(table);
        const auto& t = *std::ranges::find(seventhslide::db::k_sync_tables, table,
                                           &seventhslide::db::SyncTable::table);
        std::set<std::string> expected;
        QSqlQuery q(db.handle());
        REQUIRE(q.exec(QStringLiteral("SELECT name FROM pragma_table_info('%1')")
                           .arg(QString::fromUtf8(table))));
        while (q.next()) {
            const auto column = q.value(0).toString().toStdString();
            const bool key_column = t.as == seventhslide::db::SyncAs::Key
                                    && std::string(t.key).find(column) != std::string::npos;
            if (column != "id" && column != "uid" && !key_column) {
                expected.insert(column);
            }
        }
        if (table == "bibles") {
            expected.insert("verses");
        }
        const auto keys = record_keys(db, table);
        REQUIRE_FALSE(keys.empty());
        const auto doc = read_record(db, table, keys.front());
        REQUIRE(doc);
        std::set<std::string> fields;
        for (const auto& [field, value] : doc->items()) {
            fields.insert(field);
        }
        CHECK(fields == expected);
    }
}

TEST_CASE("Every record copied into another library reads back the same", "[db][sync]")
{
    Database from = current_library();
    seventhslide::test::fill_library(from);
    add_font(from);
    Database to = current_library();
    shift_ids(to);

    for (const auto table : seventhslide::db::record_tables_in_order()) {
        for (const auto& key : record_keys(from, table)) {
            INFO(table << " " << key);
            const auto doc = read_record(from, table, key);
            REQUIRE(doc);
            const auto written = write_record(to, table, key, *doc);
            CHECK(written.written);
            CHECK(written.unresolved.empty());
        }
    }
    for (const auto table : seventhslide::db::record_tables_in_order()) {
        INFO(table);
        CHECK(record_keys(to, table) == record_keys(from, table));
        for (const auto& key : record_keys(from, table)) {
            INFO(key);
            CHECK(read_record(to, table, key) == read_record(from, table, key));
        }
    }

    // The copies have ids of their own, and point at each other by them.
    const auto song = uid(from, "songs", 1);
    const auto here = number(to, "SELECT id FROM songs WHERE uid = '" + song + "'");
    CHECK(here != 1);
    CHECK(number(to, "SELECT song_id FROM service_items WHERE order_num = 0") == here);
    CHECK(number(to, "SELECT COUNT(*) FROM verses") == 2);
    CHECK(seventhslide::db::library_uid(to) == seventhslide::db::library_uid(from));
}

TEST_CASE("A record written again replaces what it holds, and keeps what it leaves out",
          "[db][sync]")
{
    Database db = current_library();
    seventhslide::test::fill_library(db);
    const auto song = uid(db, "songs", 1);
    REQUIRE(write_record(db, "songs", song, json{{"title", "Amazing Grace!"}}).written);
    const auto doc = read_record(db, "songs", song);
    REQUIRE(doc);
    CHECK((*doc)["title"] == "Amazing Grace!");
    CHECK((*doc)["key"] == "G");
    CHECK(number(db, "SELECT COUNT(*) FROM songs") == 2);

    // A Bible written without its verses keeps them; with them, has those.
    const auto bible = uid(db, "bibles", 1);
    REQUIRE(write_record(db, "bibles", bible, json{{"enabled", 0}}).written);
    CHECK(number(db, "SELECT COUNT(*) FROM verses") == 2);
    REQUIRE(write_record(db, "bibles", bible,
                         json{{"verses",
                               {{"columns", {"book", "chapter", "verse_num", "text"}},
                                {"rows", {{"Psalms", 23, 1, "The Lord is my shepherd"}}}}}})
                .written);
    CHECK(text(db, "SELECT text FROM verses") == "The Lord is my shepherd");
    CHECK(number(db, "SELECT enabled FROM bibles") == 0);
}

TEST_CASE("A reference to a record that is not here", "[db][sync]")
{
    Database db = current_library();
    seventhslide::test::fill_library(db);
    const auto service = uid(db, "services", 1);
    const std::string missing = "00000000000000000000000000000000";

    SECTION("is written as null, and reported, where it may be")
    {
        const auto written = write_record(db, "service_items", "11111111111111111111111111111111",
                                          json{{"service_id", service},
                                               {"song_id", missing},
                                               {"order_num", 5},
                                               {"data", R"({"bible_id":")" + missing + R"("})"}});
        CHECK(written.written);
        CHECK(written.unresolved == std::vector<std::string>{"data.bible_id", "song_id"});
        CHECK(number(db, "SELECT COUNT(*) FROM service_items WHERE order_num = 5 "
                         "AND song_id IS NULL")
              == 1);
        CHECK(json::parse(text(db, "SELECT data FROM service_items WHERE order_num = 5"))
              == json{{"bible_id", nullptr}});
    }
    SECTION("leaves the record out where it cannot be without it")
    {
        const auto item = write_record(db, "service_items", "11111111111111111111111111111111",
                                       json{{"service_id", missing}, {"order_num", 5}});
        CHECK_FALSE(item.written);
        CHECK(item.unresolved == std::vector<std::string>{"service_id"});
        const auto filed = write_record(db, "image_folder_items", missing + "/ab12.png",
                                        json{{"sort_order", 0}});
        CHECK_FALSE(filed.written);
        CHECK(filed.unresolved == std::vector<std::string>{"folder_id"});
        CHECK(number(db, "SELECT COUNT(*) FROM service_items") == 3);
        CHECK(number(db, "SELECT COUNT(*) FROM image_folder_items") == 1);
    }
    SECTION("reads as null when the record it pointed at is gone")
    {
        db.exec("PRAGMA foreign_keys = OFF");
        db.exec("DELETE FROM songs WHERE id = 1");
        db.exec("PRAGMA foreign_keys = ON");
        const auto item = read_record(db, "service_items",
                                      text(db, "SELECT uid FROM service_items WHERE order_num = 0"));
        REQUIRE(item);
        CHECK((*item)["song_id"].is_null());
    }
}

TEST_CASE("Outputs are records named by their ids", "[db][sync]")
{
    Database db = current_library();
    seventhslide::test::fill_library(db);
    CHECK(record_keys(db, "outputs") == std::vector<std::string>{"main", "foldback"});
    const auto main = read_record(db, "outputs", "main");
    REQUIRE(main);
    CHECK(json::parse((*main)["data"].get<std::string>())["name"] == "Main");

    auto renamed = json::parse((*main)["data"].get<std::string>());
    renamed["name"] = "Sanctuary";
    REQUIRE(write_record(db, "outputs", "main", json{{"data", renamed.dump()}}).written);
    CHECK(json::parse((*read_record(db, "outputs", "main"))["data"].get<std::string>())["name"]
          == "Sanctuary");
    CHECK(number(db, "SELECT COUNT(*) FROM outputs") == 2);

    CHECK_THROWS_AS(write_record(db, "outputs", "lobby", json{{"data", renamed.dump()}}),
                    std::invalid_argument);
}

TEST_CASE("Deleting a record takes what belongs to it", "[db][sync]")
{
    Database db = current_library();
    seventhslide::test::fill_library(db);
    CHECK(delete_record(db, "bibles", uid(db, "bibles", 1)));
    CHECK(number(db, "SELECT COUNT(*) FROM verses") == 0);
    CHECK(delete_record(db, "image_folder_items", uid(db, "image_folders", 2) + "/ab12.png"));
    CHECK(number(db, "SELECT COUNT(*) FROM image_folder_items") == 0);
    CHECK_FALSE(delete_record(db, "songs", "00000000000000000000000000000000"));
    CHECK_FALSE(read_record(db, "bibles", uid(db, "songs", 1)));
}

TEST_CASE("Only what syncs on its own is a record, with only its own fields", "[db][sync]")
{
    Database db = current_library();
    seventhslide::test::fill_library(db);
    for (const std::string table : {"verses", "sync_outbox", "nonsense"}) {
        INFO(table);
        CHECK_THROWS_AS(record_keys(db, table), std::invalid_argument);
        CHECK_THROWS_AS(read_record(db, table, "x"), std::invalid_argument);
    }
    const auto song = uid(db, "songs", 1);
    CHECK_THROWS_AS(write_record(db, "songs", song, json{{"id", 7}}), std::invalid_argument);
    CHECK_THROWS_AS(write_record(db, "songs", song, json{{"uid", song}}), std::invalid_argument);
    CHECK_THROWS_AS(write_record(db, "songs", song, json{{"tempo", 120}}), std::invalid_argument);
    CHECK(text(db, "SELECT title FROM songs WHERE id = 1") == "Amazing Grace");
}

TEST_CASE("Tables are written after the tables their records point at", "[db][sync]")
{
    const auto order = seventhslide::db::record_tables_in_order();
    const auto at = [&](std::string_view table) {
        return std::ranges::find(order, table) - order.begin();
    };
    std::set<std::string_view> listed(order.begin(), order.end());
    for (const auto& t : seventhslide::db::k_sync_tables) {
        const bool own = t.as == seventhslide::db::SyncAs::Record
                         || t.as == seventhslide::db::SyncAs::Key
                         || t.as == seventhslide::db::SyncAs::JsonId;
        CHECK(listed.contains(t.table) == own);
    }
    for (const auto& r : seventhslide::db::k_sync_references) {
        INFO(r.table << "." << r.field);
        const std::string_view from = r.table == "verses" ? "bibles" : r.table;
        if (listed.contains(from) && listed.contains(r.target) && from != r.target) {
            CHECK(at(r.target) < at(from));
        }
    }
}

TEST_CASE("A record's references are turned by a statement prepared once, however many it holds",
          "[db][sync]")
{
    Database db = current_library();
    db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 400) "
            "INSERT INTO songs (id, title, lyrics) SELECT i, 'Hymn ' || i, '' FROM n");
    db.exec("INSERT INTO style_profiles (id, name, data) SELECT 1, 'Default', json_object('songs', "
            "(SELECT json_group_object(id, json_object('main', 't1')) FROM songs))");
    db.exec("INSERT INTO image_folders (id, name) VALUES (1, 'Backgrounds')");
    db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 400) "
            "INSERT INTO image_folder_items (folder_id, filename) SELECT 1, 'p' || i || '.png' FROM n");
    const auto profile = uid(db, "style_profiles", 1);
    const auto prepared = [&](auto&& run) {
        const auto before = db.statements_prepared();
        run();
        return db.statements_prepared() - before;
    };

    (void)db.table_columns("style_profiles");  // read once for good
    std::optional<json> doc;
    CHECK(prepared([&] { doc = read_record(db, "style_profiles", profile); }) <= 3);
    REQUIRE(doc);
    CHECK(json::parse((*doc)["data"].get<std::string>())["songs"].size() == 400);

    // Written back, its songs turn to this library's ids again.
    bool written = false;
    CHECK(prepared([&] { written = write_record(db, "style_profiles", profile, *doc).written; }) <= 6);
    CHECK(written);
    CHECK(json::parse(text(db, "SELECT data FROM style_profiles WHERE id = 1"))["songs"].size() == 400);

    // Each folder item's key names its folder by uid, read with the rest.
    std::vector<std::string> keys;
    CHECK(prepared([&] { keys = record_keys(db, "image_folder_items"); }) == 1);
    REQUIRE(keys.size() == 400);
    CHECK(keys.front() == uid(db, "image_folders", 1) + "/p1.png");
}
