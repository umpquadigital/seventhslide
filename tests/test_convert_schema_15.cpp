// The one-off from schema 14 (tools/convert_library_schema_15.py): run as
// anyone would run it, on a library as schema 14 had it, and read back by the
// app's own code. It goes with the tool.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/service_repository.hpp>

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

namespace {

struct ConvertRun {
    int status = -1;
    std::string said;
};

ConvertRun convert_library(const std::filesystem::path& library)
{
    const auto tool = std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR}.parent_path() / "tools"
                      / "convert_library_schema_15.py";
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

void run(Database& db, const QString& sql, const std::vector<QVariant>& values = {})
{
    QSqlQuery q(db.handle());
    REQUIRE(q.prepare(sql));
    for (const auto& value : values) {
        q.addBindValue(value);
    }
    REQUIRE(q.exec());
}

[[nodiscard]] std::string text_of(Database& db, const QString& sql)
{
    QSqlQuery q(db.handle());
    REQUIRE(q.exec(sql));
    REQUIRE(q.next());
    return q.value(0).toString().toStdString();
}

/// ``table``'s columns as SQLite lists them: name, type, not-null, default.
[[nodiscard]] std::vector<std::string> columns_of(Database& db, const char* table)
{
    QSqlQuery q(db.handle());
    REQUIRE(q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(QLatin1String(table))));
    std::vector<std::string> out;
    while (q.next()) {
        out.push_back((q.value(1).toString() + QLatin1Char(' ') + q.value(2).toString()
                       + QLatin1Char(' ') + q.value(3).toString() + QLatin1Char(' ')
                       + q.value(4).toString())
                          .toStdString());
    }
    return out;
}

/// A library file as schema 14 had it: a service and an audio file, each
/// with a shared copy, and nothing marked to send.
void make_old_library(const std::filesystem::path& file, int version = 14)
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_file(file);
    ensure_schema(db);
    run(db, QStringLiteral("INSERT INTO services (name, theme_map) VALUES ('Sunday', '{}')"));
    run(db, QStringLiteral("INSERT INTO audio_files (filename, display_name) "
                           "VALUES ('bed.wav', 'Bed.wav')"));
    db.exec("ALTER TABLE services DROP COLUMN audio_fades");
    db.exec("ALTER TABLE audio_files DROP COLUMN audio_fades");
    run(db, QStringLiteral("INSERT INTO sync_shared (record_table, record_key, document, versions) "
                           "SELECT 'services', uid, json_object('name', name, 'theme_map', "
                           "theme_map), '{}' FROM services"));
    run(db, QStringLiteral("INSERT INTO sync_shared (record_table, record_key, document, versions) "
                           "SELECT 'audio_files', filename, json_object('display_name', "
                           "display_name), '{}' FROM audio_files"));
    db.exec("DELETE FROM sync_outbox");
    db.exec("PRAGMA user_version = " + std::to_string(version));
}

}  // namespace

TEST_CASE("A converted library keeps a service's and an audio file's own fades",
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
    CHECK(converted.said.find("converted to schema 15") != std::string::npos);

    {
        Database db = Database::open_file(file);
        CHECK_FALSE(ensure_schema(db));  // read as it is, not refused or made anew
        CHECK(db.query_int("SELECT COUNT(*) FROM sync_outbox", -1) == 0);

        // The tables are as a library made now has them, column for column.
        const seventhslide::test::TempDir fresh_dir;
        Database fresh = Database::open_file(fresh_dir.path / "fresh.db");
        ensure_schema(fresh);
        CHECK(columns_of(db, "services") == columns_of(fresh, "services"));
        CHECK(columns_of(db, "audio_files") == columns_of(fresh, "audio_files"));

        // Nothing set: everything fades by each output's own. And it is kept.
        seventhslide::db::ServiceRepository services(db);
        const auto all = services.list_services();
        REQUIRE(all.size() == 1);
        CHECK(all.front().audio_fades == json::object());
        seventhslide::db::MediaRepository media(db);
        CHECK(media.audio_fades("bed.wav") == json::object());
        media.set_audio_fades("bed.wav", {{"fade_out_ms", 1500}});
        CHECK(media.audio_fades("bed.wav") == json{{"fade_out_ms", 1500}});

        // The shared copies say what the rows said, so nothing looks changed.
        for (const char* table : {"services", "audio_files"}) {
            const auto doc = json::parse(text_of(
                db, QStringLiteral("SELECT document FROM sync_shared WHERE record_table = '%1'")
                        .arg(QLatin1String(table))));
            CHECK(doc.at("audio_fades") == "{}");
        }
    }

    // Once is enough: again, and nothing happens.
    const auto again = convert_library(file);
    INFO(again.said);
    CHECK(again.status == 0);
    CHECK(again.said.find("already at schema 15") != std::string::npos);
}

TEST_CASE("A library at schema 12 is sent to the schema 14 conversion first",
          "[db][schema][convert]")
{
    if (!have_python()) {
        SKIP("no python3 here");
    }
    const seventhslide::test::TempDir dir;
    const auto file = dir.path / "seventhslide.db";
    make_old_library(file, 12);
    const auto refused = convert_library(file);
    INFO(refused.said);
    CHECK(refused.status == 1);
    CHECK(refused.said.find("convert_library_schema_14.py") != std::string::npos);
    {
        Database db = Database::open_file(file);
        CHECK(schema_version(db) == 12);
    }
}
