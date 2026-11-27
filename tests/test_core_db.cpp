#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/query.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/db/song_repository.hpp>

#include "test_qt_app.hpp"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

#include <optional>
#include <vector>

using seventhslide::db::ConfigRepository;
using seventhslide::db::Database;
using seventhslide::db::ServiceRepository;
using seventhslide::db::SongRepository;
using seventhslide::db::song_to_service_snapshot;
using seventhslide::model::OutputConfig;
using seventhslide::model::Song;

namespace {

void ensure_qcore_application()
{
    seventhslide::test::ensure_qt_application();
}

Database open_core_db()
{
    ensure_qcore_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

int table_count(Database& db, const char* name)
{
    const std::string sql =
        std::string("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='")
        + name + "'";
    return db.query_int(sql, 0);
}

Song make_song(std::string title, std::string lyrics = "v1\nLine")
{
    Song s;
    s.title = std::move(title);
    s.lyrics = std::move(lyrics);
    s.verse_order = "v1";
    s.authors = {"Author"};
    s.copyright = "© Test";
    s.show_copyright = true;
    s.key = "G";
    s.theme_map = {{"Main", {{"song", {{"theme", "t1"}, {"bg", "b1"}}}}}};
    return s;
}

}  // namespace

TEST_CASE("Transaction commits once and nests with savepoints", "[db][core][tx]")
{
    Database db = open_core_db();
    db.exec("CREATE TABLE t (n INTEGER)");
    const auto rows = [&db] { return db.query_int("SELECT COUNT(*) FROM t", -1); };

    {
        seventhslide::db::Transaction tx(db);
        db.exec("INSERT INTO t (n) VALUES (1)");
        tx.commit();
    }
    REQUIRE(rows() == 1);

    // Dropped without commit: the row goes with it.
    {
        seventhslide::db::Transaction tx(db);
        db.exec("INSERT INTO t (n) VALUES (2)");
    }
    REQUIRE(rows() == 1);

    // An inner transaction is a savepoint: rolling it back keeps the outer one.
    {
        seventhslide::db::Transaction outer(db);
        db.exec("INSERT INTO t (n) VALUES (3)");
        {
            seventhslide::db::Transaction inner(db);
            db.exec("INSERT INTO t (n) VALUES (4)");
        }
        REQUIRE(db.transaction_depth() == 1);
        outer.commit();
    }
    REQUIRE(db.transaction_depth() == 0);
    REQUIRE(rows() == 2);
    REQUIRE(db.query_int("SELECT COUNT(*) FROM t WHERE n = 4", -1) == 0);
}

TEST_CASE("The schema creates the core tables", "[db][core]")
{
    Database db = open_core_db();
    REQUIRE(table_count(db, "songs") == 1);
    REQUIRE(table_count(db, "services") == 1);
    REQUIRE(table_count(db, "service_items") == 1);
    REQUIRE(table_count(db, "outputs") == 1);
    REQUIRE(table_count(db, "app_settings") == 1);
}

TEST_CASE("SongRepository CRUD round-trip", "[db][core][songs]")
{
    Database db = open_core_db();
    SongRepository songs(db);

    Song s = make_song("Holy Holy");
    const auto id = songs.insert(s);
    REQUIRE(id > 0);
    REQUIRE(s.id == id);

    auto loaded = songs.get(id);
    REQUIRE(loaded);
    REQUIRE(loaded->title == "Holy Holy");
    REQUIRE(loaded->lyrics == "v1\nLine");
    REQUIRE(loaded->authors == std::vector<seventhslide::model::Author>{"Author"});
    REQUIRE(loaded->show_copyright);
    REQUIRE(loaded->theme_map["Main"]["song"]["theme"] == "t1");
    REQUIRE(loaded->topics.empty());
    REQUIRE(loaded->comments.empty());

    loaded->title = "Holy Holy Holy";
    loaded->lyrics = "updated";
    loaded->topics = {"Advent", " advent ", "Hope"};
    loaded->comments = "Key of D for the youth band.";
    songs.update(*loaded);
    auto updated = songs.get(id);
    REQUIRE(updated->title == "Holy Holy Holy");
    REQUIRE(updated->lyrics == "updated");
    REQUIRE(updated->topics == std::vector<std::string>{"Advent", "Hope"});
    REQUIRE(updated->comments == "Key of D for the youth band.");

    Song other = make_song("A First");
    REQUIRE(songs.insert(other) > 0);
    const auto summaries = songs.list_summaries();
    REQUIRE(summaries.size() == 2);
    REQUIRE(summaries[0].title == "A First");
    REQUIRE(summaries[0].lyrics.empty());
    REQUIRE(summaries[1].title == "Holy Holy Holy");
    REQUIRE(summaries[1].topics == std::vector<std::string>{"Advent", "Hope"});
    REQUIRE(summaries[1].comments.empty());

    REQUIRE(songs.remove(id));
    REQUIRE_FALSE(songs.get(id));
}

TEST_CASE("service song snapshots are independent of library edits", "[db][core][snapshots]")
{
    Database db = open_core_db();
    SongRepository songs(db);
    ServiceRepository services(db);

    Song s = make_song("Snapshot Song", "original lyrics");
    const auto song_id = songs.insert(s);
    const auto service_id = services.create_service("Sunday");

    const auto item_id = services.add_song_to_service(service_id, song_id);
    REQUIRE(item_id);

    s.lyrics = "library changed";
    s.title = "Library Title";
    songs.update(s);

    auto items = services.list_items(service_id);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].title() == "Snapshot Song");
    REQUIRE(items[0].song().lyrics == "original lyrics");
    REQUIRE(items[0].song_id == song_id);
    REQUIRE_FALSE(items[0].has_overrides());

    // Service edit replaces snapshot and marks modified.
    auto snap = items[0].data;
    snap["lyrics"] = "service edit";
    snap["user_modified"] = true;
    services.replace_item_data(*item_id, snap);
    auto edited = services.get_item(*item_id);
    REQUIRE(edited->song().lyrics == "service edit");
    REQUIRE(edited->has_overrides());
}

TEST_CASE("deleting a song detaches snapshots and keeps service items", "[db][core][snapshots]")
{
    Database db = open_core_db();
    SongRepository songs(db);
    ServiceRepository services(db);

    Song s = make_song("Detach Me", "keep me");
    const auto song_id = songs.insert(s);
    const auto service_id = services.create_service("AM");
    const auto item_id = services.add_song_to_service(service_id, song_id);
    REQUIRE(item_id);

    REQUIRE(songs.remove(song_id));

    auto item = services.get_item(*item_id);
    REQUIRE(item);
    REQUIRE_FALSE(item->song_id.has_value());
    REQUIRE(item->title() == "Detach Me");
    REQUIRE(item->song().lyrics == "keep me");
    REQUIRE(item->has_overrides());
}

TEST_CASE("service items reorder, and go with their service", "[db][core][services]")
{
    Database db = open_core_db();
    SongRepository songs(db);
    ServiceRepository services(db);

    const auto sid = services.create_service("Worship");
    REQUIRE(services.get_service(sid));

    Song a = make_song("A");
    Song b = make_song("B");
    Song c = make_song("C");
    const auto aid = songs.insert(a);
    const auto bid = songs.insert(b);
    const auto cid = songs.insert(c);

    const auto i1 = services.add_song_to_service(sid, aid);
    const auto i2 = services.add_song_to_service(sid, bid);
    const auto i3 = services.add_song_to_service(sid, cid, /*at_index=*/0);
    REQUIRE(i1);
    REQUIRE(i2);
    REQUIRE(i3);

    auto items = services.list_items(sid);
    REQUIRE(items.size() == 3);
    REQUIRE(items[0].item_id == i3);
    REQUIRE(items[1].item_id == i1);
    REQUIRE(items[2].item_id == i2);

    services.reorder_items(sid, {*i2, *i3, *i1});
    items = services.list_items(sid);
    REQUIRE(items[0].item_id == i2);
    REQUIRE(items[1].item_id == i3);
    REQUIRE(items[2].item_id == i1);

    REQUIRE(services.add_item(sid, "divider", {{"title", "Break"}}) > 0);
    REQUIRE(services.list_items(sid).size() == 4);

    REQUIRE(services.delete_service(sid));
    // The items go with their service.
    REQUIRE_FALSE(services.get_service(sid));
    REQUIRE(services.list_items(sid).empty());
    REQUIRE_FALSE(services.delete_service(sid));
}

TEST_CASE("services keep tags and a date, and list newest first", "[db][core][services]")
{
    Database db = open_core_db();
    ServiceRepository services(db);

    const auto a = services.create_service("Alpha");
    const auto b = services.create_service("Beta");
    const auto c = services.create_service("Gamma");
    auto svc = services.get_service(a);
    REQUIRE(svc);
    REQUIRE(svc->tags.empty());
    REQUIRE(svc->created_at.find('T') != std::string::npos);
    REQUIRE(svc->service_at == svc->created_at);

    services.set_service_tags(a, {" Communion ", "communion", "Youth  Group"});
    CHECK(services.get_service(a)->tags == std::vector<std::string>{"Communion", "Youth Group"});

    services.set_service_at(a, "2026-08-17T09:30:00");
    services.set_service_at(b, "2026-08-24T09:30:00");
    services.set_service_at(c, "");
    CHECK(services.get_service(a)->service_at == "2026-08-17T09:30:00");
    CHECK(services.get_service(c)->service_at.empty());
    REQUIRE_THROWS(services.set_service_at(a, "next Tuesday"));

    // Newest first; no date last.
    const auto listed = services.list_services();
    REQUIRE(listed.size() == 3);
    CHECK(listed[0].id == b);
    CHECK(listed[1].id == a);
    CHECK(listed[2].id == c);
}

TEST_CASE("service tag counts take each tag once, by name", "[model][services]")
{
    seventhslide::model::Service one;
    one.tags = {"Youth", "Communion"};
    seventhslide::model::Service two;
    two.tags = {"youth"};
    const auto counts = seventhslide::model::service_tag_counts({one, two});
    CHECK(counts == std::vector<seventhslide::model::TagCount>{{"Communion", 1}, {"Youth", 2}});
}

TEST_CASE("ConfigRepository settings and outputs", "[db][core][config]")
{
    Database db = open_core_db();
    ConfigRepository config(db);

    config.set_setting("ccli_licence_number", "12345");
    config.set_setting("theme_priority", nlohmann::json::array({"content", "service", "global"}));
    auto settings = config.load_settings();
    REQUIRE(settings["ccli_licence_number"] == "12345");
    REQUIRE(settings["theme_priority"].size() == 3);

    OutputConfig out;
    out.name = "Main";
    out.canvas_width = 1920;
    out.canvas_height = 1080;
    // Element switches ride on the text theme, not on the output row: only
    // structural + intrinsic fields are persisted per output.
    out.song_themes.push_back(seventhslide::model::ThemeEntry{
        .id = "t1",
        .name = "Base",
        .style = {{"show_lyrics", false}, {"video_enabled", false}}});
    config.save_outputs({out});

    auto loaded = config.load_outputs();
    REQUIRE(loaded.size() == 1);
    REQUIRE(loaded[0].name == "Main");
    REQUIRE(loaded[0].canvas_width == 1920);
    REQUIRE(loaded[0].song_themes.size() == 1);
    REQUIRE(loaded[0].song_themes[0].style.at("show_lyrics") == false);
    REQUIRE(loaded[0].song_themes[0].style.at("video_enabled") == false);

    OutputConfig foldback;
    foldback.name = "Foldback";
    config.save_outputs({foldback, out});
    loaded = config.load_outputs();
    REQUIRE(loaded.size() == 2);
    REQUIRE(loaded[0].name == "Foldback");
    REQUIRE(loaded[1].name == "Main");
}

TEST_CASE("song_to_service_snapshot shape", "[db][core][snapshots]")
{
    Song s = make_song("Shape");
    const auto snap = song_to_service_snapshot(s, false);
    REQUIRE(snap["user_modified"] == false);
    REQUIRE(snap["title"] == "Shape");
    REQUIRE(snap.contains("lyrics"));
    REQUIRE(snap.contains("theme_map"));
    REQUIRE(snap["authors"]
            == nlohmann::json::array({{{"name", "Author"}, {"roles", nlohmann::json::array()}}}));
    REQUIRE(snap.contains("show_copyright"));
}

TEST_CASE("service snapshots carry author roles", "[db][core][snapshots]")
{
    using seventhslide::model::Author;
    using seventhslide::model::AuthorRole;
    using seventhslide::model::k_author_role_arrangement;
    using seventhslide::model::k_author_role_words;

    Song s = make_song("Credited");
    s.authors = {Author{"Ada", {AuthorRole{std::string(k_author_role_words), {}, {}}}},
                 Author{"Bo", {AuthorRole{std::string(k_author_role_arrangement), {}, {}}}}};

    seventhslide::model::ServiceItem item;
    item.item_type = std::string{seventhslide::model::k_item_type_song};
    item.data = song_to_service_snapshot(s, false);
    REQUIRE(item.song().authors == s.authors);
}

TEST_CASE("a database that fails to open leaves no connection behind", "[db]")
{
    seventhslide::test::ensure_qt_application();
    const auto before = QSqlDatabase::connectionNames();
    REQUIRE_THROWS(Database::open_file("/nonexistent-dir/for/sure/library.db"));
    REQUIRE(QSqlDatabase::connectionNames() == before);
}

TEST_CASE("Removing a rundown row drops it from every also-play list", "[db][core][services]")
{
    Database db = open_core_db();
    ServiceRepository services(db);
    const auto sid = services.create_service("Sunday");
    const auto lead = services.add_item(sid, "divider", {{"title", "Lead"}});
    const auto companion = services.add_item(sid, "divider", {{"title", "Companion"}});
    const auto kept = services.add_item(sid, "divider", {{"title", "Kept"}});

    auto item = *services.get_item(lead);
    seventhslide::model::set_service_item_also_play(item, {companion, kept});
    services.replace_item_data(lead, item.data);

    REQUIRE(services.remove_item(companion));
    const auto after = *services.get_item(lead);
    REQUIRE(seventhslide::model::service_item_also_play(after) == std::vector<std::int64_t>{kept});
    REQUIRE_FALSE(services.remove_item(companion));
}

TEST_CASE("query helpers bind in order and name the statement that failed", "[db]")
{
    using Catch::Matchers::StartsWith;
    using seventhslide::db::exec;
    using seventhslide::db::prepare;
    Database db = open_core_db();

    exec(db, QStringLiteral("CREATE TABLE t (id INTEGER PRIMARY KEY, a TEXT, b INTEGER NOT NULL)"),
         {}, "create");
    // A prepared statement runs again with fresh values in every position.
    auto insert = prepare(db, QStringLiteral("INSERT INTO t (a, b) VALUES (?, ?)"), "insert");
    exec(insert, {QStringLiteral("one"), 1}, "insert");
    exec(insert, {QVariant(), 2}, "insert");

    auto rows = exec(db, QStringLiteral("SELECT a, b FROM t WHERE b >= ? ORDER BY b"), {1},
                     "select");
    REQUIRE(rows.next());
    REQUIRE(rows.value(0).toString() == QStringLiteral("one"));
    REQUIRE(rows.next());
    REQUIRE(rows.value(0).isNull());
    REQUIRE(rows.value(1).toInt() == 2);
    REQUIRE_FALSE(rows.next());

    // Not SQL: the statement does not compile.
    REQUIRE_THROWS_WITH(exec(db, QStringLiteral("SELEKT 1"), {}, "read things"),
                        StartsWith("prepare read things: "));
    // Compiles but fails when run: NOT NULL.
    REQUIRE_THROWS_WITH(exec(insert, {QStringLiteral("three"), QVariant()}, "insert"),
                        StartsWith("insert: "));
}
