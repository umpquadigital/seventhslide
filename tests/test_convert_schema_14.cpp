// The one-off from schema 12 (tools/convert_library_schema_14.py): run as
// anyone would run it, on a library as schema 12 had it, and read back by the app's own code once the next one-off
// (convert_library_schema_15.py) has brought it on to this build's schema. It
// goes with the tool.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/model/content_kind.hpp>
#include <seventhslide/theme/song_theme_pack.hpp>
#include <seventhslide/theme/theme_category.hpp>
#include <seventhslide/theme/theme_elements.hpp>
#include <seventhslide/theme/theme_library.hpp>
#include <seventhslide/theme/theme_picks.hpp>
#include <seventhslide/theme/theme_resolver.hpp>

#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <QProcess>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QString>
#include <QVariant>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

using nlohmann::json;
using seventhslide::db::Database;
using seventhslide::db::ensure_schema;
using seventhslide::db::schema_version;
using seventhslide::model::ContentKind;
using seventhslide::theme::ThemeCategory;
using seventhslide::theme::ThemeKind;
using seventhslide::theme::ThemePick;

namespace {

struct ConvertRun {
    int status = -1;
    std::string said;
};

ConvertRun convert_library(const std::filesystem::path& library,
                           const char* tool_name = "convert_library_schema_14.py")
{
    const auto tool = std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR}.parent_path() / "tools"
                      / tool_name;
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral("python3"),
                  {QStringLiteral("-I"), QString::fromStdString(tool.string()),
                   QString::fromStdString(library.string())});
    REQUIRE(process.waitForFinished(30000));
    return {process.exitCode(), process.readAll().toStdString()};
}

[[nodiscard]] bool have_python()
{
    return !QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty();
}

/// The one text value ``sql`` reads.
[[nodiscard]] std::string text_of(Database& db, const QString& sql)
{
    QSqlQuery q(db.handle());
    REQUIRE(q.exec(sql));
    REQUIRE(q.next());
    return q.value(0).toString().toStdString();
}

[[nodiscard]] json json_of(Database& db, const QString& sql)
{
    return json::parse(text_of(db, sql));
}

void run(Database& db, const QString& sql, const std::vector<QVariant>& values = {})
{
    QSqlQuery q(db.handle());
    REQUIRE(q.prepare(sql));
    for (const auto& value : values) {
        q.addBindValue(value);
    }
    REQUIRE(q.exec());
}

[[nodiscard]] QVariant text(const json& value)
{
    return QString::fromStdString(value.dump());
}

/// A text theme as schema 12 kept it: the whole design, every context's
/// settings, and keys of every kind.
[[nodiscard]] json old_text_style()
{
    json transitions = json::object();
    for (const auto ctx : seventhslide::theme::k_content_contexts) {
        transitions[std::string(seventhslide::theme::to_string(ctx))] = {{"type", "fade"},
                                                                          {"duration_ms", 400}};
    }
    return {
        {"elements", seventhslide::theme::default_elements().to_json()},
        {"transitions", transitions},
        {"follow_lines", 2},
        {"show_title_slides", false},
        {"bible_fluid_verses", true},
        {"audio", {{"fade_in_ms", 300}}},
    };
}

/// A pick as schema 12 stored it.
[[nodiscard]] json old_pick(const char* text, const char* bg)
{
    return {{"text", text}, {"bg", bg}};
}

[[nodiscard]] json old_song_pack(const json& chip)
{
    return {{"active", "a1"},
            {"arrangements",
             json::array({{{"id", "a1"}, {"name", "Default"}, {"order", "TH1 V1"},
                           {"chips", {{"TH1", chip}}}}})}};
}

/// A library file as schema 12 had it: an output with one text theme and two
/// backgrounds, and theme choices on everything that holds them. Nothing is
/// marked to send; the song and the output have shared copies.
void make_old_library(const std::filesystem::path& file, int version = 12)
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_file(file);
    ensure_schema(db);
    const json output = {
        {"id", "out-main"},
        {"name", "Main"},
        {"text_themes", json::array({{{"id", "t1"},
                                      {"name", "Words"},
                                      {"title_slide", {{"layout", "x"}}},
                                      {"style", old_text_style()}}})},
        {"bg_themes", json::array({{{"id", "b1"}, {"name", "Blue"}, {"style", json::object()}},
                                   {{"id", "b2"}, {"name", "Dark"}, {"style", json::object()}}})},
        {"category_defaults", {{"song", old_pick("t1", "b1")}, {"bible", old_pick("t1", "b2")}}},
    };
    run(db, QStringLiteral("INSERT INTO outputs (order_num, data) VALUES (0, ?)"), {text(output)});
    const auto chips = old_song_pack({{"out-main", old_pick("t1", "b2")}});
    run(db, QStringLiteral("INSERT INTO songs (title, lyrics, theme_map) VALUES ('Abide', "
                           "'[V1]\nline', ?)"),
        {text(chips)});
    run(db, QStringLiteral("INSERT INTO services (name, theme_map) VALUES ('Sabbath', ?)"),
        {text(json{{"out-main", old_pick("t1", "b1")}})});
    const json item_map = {{"out-main", old_pick("t1", "b2")}};
    const std::vector<std::pair<const char*, json>> items{
        {"song", {{"title", "Abide"}, {"theme_map", chips}}},
        {"bible", {{"ref", "John 3:16"}, {"theme_map", item_map}}},
        {"image", {{"filename", "a.jpg"}, {"theme_map", item_map}}},
        {"divider", {{"title", "Word"}, {"theme_map", item_map}}},
        {"freeform", {{"name", "Slides"}, {"theme_map", item_map}}},
    };
    int order = 0;
    for (const auto& [type, data] : items) {
        run(db, QStringLiteral("INSERT INTO service_items (service_id, order_num, item_type, data) "
                               "VALUES (1, ?, ?, ?)"),
            {++order, QString::fromLatin1(type), text(data)});
    }
    run(db, QStringLiteral("INSERT INTO style_profiles (name, data) VALUES ('Evening', ?)"),
        {text(json{{"outputs", {{"out-main", output["category_defaults"]}}},
                   {"songs", {{"1", chips}}}})});
    // What sync last shared of the song and the output: as they are.
    run(db, QStringLiteral("INSERT INTO sync_shared (record_table, record_key, document, versions) "
                           "SELECT 'songs', uid, json_object('title', title, 'theme_map', "
                           "theme_map), '{}' FROM songs"));
    run(db, QStringLiteral("INSERT INTO sync_shared (record_table, record_key, document, versions) "
                           "SELECT 'outputs', 'out-main', json_object('data', data, 'order_num', "
                           "order_num), '{}' FROM outputs"));
    db.exec("DELETE FROM sync_outbox");
    db.exec("PRAGMA user_version = " + std::to_string(version));
}

[[nodiscard]] std::vector<std::filesystem::path> copies_beside(const std::filesystem::path& file)
{
    std::vector<std::filesystem::path> out;
    for (const auto& entry : std::filesystem::directory_iterator(file.parent_path())) {
        if (entry.path().filename().string().find(".before-schema-14-") != std::string::npos) {
            out.push_back(entry.path());
        }
    }
    return out;
}

[[nodiscard]] seventhslide::theme::ThemeIds ids_for(const seventhslide::model::OutputConfig& oc,
                                                    ContentKind kind)
{
    seventhslide::theme::ThemeSelection sel;
    sel.kind = kind;
    return seventhslide::theme::resolve_theme_ids(oc, sel);
}

}  // namespace

TEST_CASE("A converted library holds Song, Bible and Media themes, and picks per category",
          "[db][schema][convert]")
{
    if (!have_python()) {
        SKIP("no python3 here");
    }
    const seventhslide::test::TempDir dir;
    const auto file = dir.path / "seventhslide.db";
    make_old_library(file);

    const auto converted = convert_library(dir.path);
    INFO(converted.said);
    REQUIRE(converted.status == 0);
    CHECK(converted.said.find("converted to schema 14") != std::string::npos);

    // Once is enough: again, and nothing happens.
    const auto again = convert_library(file);
    INFO(again.said);
    CHECK(again.status == 0);
    CHECK(again.said.find("already at schema 14") != std::string::npos);

    // On to this build's schema.
    const auto onward = convert_library(file, "convert_library_schema_15.py");
    INFO(onward.said);
    REQUIRE(onward.status == 0);

    {
        Database db = Database::open_file(file);
        CHECK_FALSE(ensure_schema(db));  // read as it is, not refused or made anew
        // Every computer converts its own copy: nothing to send for it.
        CHECK(db.query_int("SELECT COUNT(*) FROM sync_outbox", -1) == 0);

        // The text theme is one of each kind, under its id, as the app keeps one.
        const auto outputs = seventhslide::db::ConfigRepository(db).load_outputs();
        REQUIRE(outputs.size() == 1);
        const auto& oc = outputs.front();
        for (const auto kind : {ThemeKind::Song, ThemeKind::Bible, ThemeKind::Media}) {
            const auto& themes = seventhslide::theme::theme_list(oc, kind);
            REQUIRE(themes.size() == 1);
            CHECK(themes.front().id == "t1");
            CHECK(themes.front().name == "Words");
            // The old entry's ``title_slide`` is a key no build reads: not kept.
            CHECK_FALSE(json(themes.front()).contains("title_slide"));
            CHECK(themes.front().style == seventhslide::theme::filter_theme_style(kind, old_text_style()));
        }
        CHECK(oc.bg_themes.size() == 2);

        // What plays under the defaults is what played before: media under the
        // song default's theme, on black.
        using Ids = seventhslide::theme::ThemeIds;
        CHECK(ids_for(oc, ContentKind::Song) == Ids{.theme_id = "t1", .bg_id = "b1"});
        CHECK(ids_for(oc, ContentKind::Bible) == Ids{.theme_id = "t1", .bg_id = "b2"});
        CHECK(ids_for(oc, ContentKind::Image) == Ids{.theme_id = "t1", .bg_id = ""});
        CHECK(ids_for(oc, ContentKind::Divider) == Ids{.theme_id = "", .bg_id = "b1"});

        // A song's chips choose song themes.
        const auto chips_of = [](const json& pack) {
            return seventhslide::theme::SongThemeMap::from_json(pack).chip("TH1").picks("out-main");
        };
        const auto song_map = json_of(db, QStringLiteral("SELECT theme_map FROM songs"));
        CHECK(chips_of(song_map).of(ThemeCategory::Song) == ThemePick{.theme = "t1", .bg = "b2"});

        // A service's choices are every category's.
        const auto service = seventhslide::theme::service_themes_from_json(
            json_of(db, QStringLiteral("SELECT theme_map FROM services")));
        const auto& picked = service.picks("out-main");
        CHECK(picked.of(ThemeCategory::Song) == ThemePick{.theme = "t1", .bg = "b1"});
        CHECK(picked.of(ThemeCategory::Bible) == ThemePick{.theme = "t1", .bg = "b1"});
        CHECK(picked.of(ThemeCategory::Media) == ThemePick{.theme = "t1", .bg = ""});

        // A rundown item's are its kind's, and only those.
        const auto item_map = [&](const char* type) {
            return json_of(db, QStringLiteral("SELECT data FROM service_items WHERE item_type = '%1'")
                                   .arg(QString::fromLatin1(type)))
                .at("theme_map");
        };
        CHECK(chips_of(item_map("song")).of(ThemeCategory::Song) == ThemePick{.theme = "t1", .bg = "b2"});
        CHECK(item_map("bible") == json{{"out-main", {{"bible", {{"theme", "t1"}, {"bg", "b2"}}}}}});
        CHECK(item_map("image") == json{{"out-main", {{"media", {{"theme", "t1"}}}}}});
        CHECK(item_map("divider") == json{{"out-main", {{"song", {{"bg", "b2"}}}}}});
        CHECK(item_map("freeform") == json::object());
        for (const auto* type : {"bible", "image", "divider"}) {
            // Each is what the app reads back, and writes, for that kind.
            const auto kind = seventhslide::model::content_kind_from_string(type);
            CHECK(seventhslide::theme::OutputThemeMap::from_json(
                      item_map(type), seventhslide::theme::categories_for(kind))
                      .to_json()
                  == item_map(type));
        }

        // A style profile's copies are converted with what they copy.
        const auto profile = json_of(db, QStringLiteral("SELECT data FROM style_profiles"));
        CHECK(profile.at("outputs").at("out-main") == oc.category_defaults);
        CHECK(profile.at("songs").at("1") == song_map);

        // What sync last shared is converted the same way: a record left unmarked
        // is just as its shared copy has it.
        CHECK(json_of(db, QStringLiteral("SELECT document FROM sync_shared WHERE record_table = "
                                         "'songs'"))
                  .at("theme_map")
              == song_map.dump());
        CHECK(json_of(db, QStringLiteral("SELECT document FROM sync_shared WHERE record_table = "
                                         "'outputs'"))
                  .at("data")
              == text_of(db, QStringLiteral("SELECT data FROM outputs")));
    }

    // The copy kept is the library as it was.
    const auto copies = copies_beside(file);
    REQUIRE(copies.size() == 1);
    {
        Database copy = Database::open_file(copies.front());
        CHECK(schema_version(copy) == 12);
        CHECK(json_of(copy, QStringLiteral("SELECT data FROM outputs")).contains("text_themes"));
    }
}

TEST_CASE("A library open, unsynced, or at another version is not converted",
          "[db][schema][convert]")
{
    if (!have_python()) {
        SKIP("no python3 here");
    }
    const seventhslide::test::TempDir dir;
    const auto file = dir.path / "seventhslide.db";
    make_old_library(file);
    {
        // As the app holds it: open, between writes.
        Database open = Database::open_file(file);
        CHECK(open.query_int("SELECT COUNT(*) FROM songs", 0) == 1);
        const auto run = convert_library(file);
        INFO(run.said);
        CHECK(run.status == 1);
        CHECK(run.said.find("close SeventhSlide") != std::string::npos);
        CHECK(schema_version(open) == 12);
    }
    CHECK(copies_beside(file).empty());

    // Changes received and not yet applied would come in as schema 12 wrote them.
    {
        Database db = Database::open_file(file);
        db.exec("INSERT INTO sync_held (record_table, record_key, machine_id, sequence, change) "
                "VALUES ('songs', 'x', 'm', 1, '{}')");
    }
    const auto held = convert_library(file);
    INFO(held.said);
    CHECK(held.status == 1);
    CHECK(held.said.find("not yet applied") != std::string::npos);
    CHECK(copies_beside(file).empty());

    const seventhslide::test::TempDir older;
    const auto old_file = older.path / "seventhslide.db";
    make_old_library(old_file, 11);
    const auto refused = convert_library(old_file);
    INFO(refused.said);
    CHECK(refused.status == 1);
    CHECK(refused.said.find("at schema 11") != std::string::npos);
    CHECK(copies_beside(old_file).empty());
}
