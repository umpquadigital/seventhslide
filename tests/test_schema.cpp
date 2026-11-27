#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/db/style_profile_repository.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/library.hpp>

#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <QSqlQuery>
#include <QString>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <string>
#include <vector>

using seventhslide::db::Database;
using seventhslide::db::ensure_schema;
using seventhslide::db::k_schema_version;
using seventhslide::db::schema_version;

namespace {

Database open_empty()
{
    seventhslide::test::ensure_qt_application();
    return Database::open_memory();
}

int table_count(Database& db)
{
    return db.query_int("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table'", -1);
}

/// The schema as SQLite keeps it, written to read well in a diff: the
/// version; each table with its kind, its columns (type, NOT NULL, default,
/// place in the primary key) and its foreign keys; then every index and
/// trigger as written. SQLite's own tables and indexes are left out.
std::string schema_shape(Database& db)
{
    std::string out = "user_version " + std::to_string(schema_version(db)) + "\n";
    QSqlQuery tables(db.handle());
    REQUIRE(tables.exec(QStringLiteral("SELECT name, sql FROM sqlite_master WHERE type = 'table' "
                                       "AND name NOT LIKE 'sqlite_%' ORDER BY name")));
    while (tables.next()) {
        const QString table = tables.value(0).toString();
        const QString sql = tables.value(1).toString().toUpper();
        out += "\ntable " + table.toStdString();
        if (sql.contains(QLatin1String("AUTOINCREMENT"))) {
            out += " autoincrement";
        }
        if (sql.contains(QLatin1String("WITHOUT ROWID"))) {
            out += " without-rowid";
        }
        out += "\n";
        QSqlQuery columns(db.handle());
        REQUIRE(columns.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)));
        while (columns.next()) {
            out += "  " + columns.value(1).toString().toStdString() + " "
                   + columns.value(2).toString().toStdString();
            if (columns.value(3).toInt() != 0) {
                out += " not-null";
            }
            if (!columns.value(4).isNull()) {
                out += " default " + columns.value(4).toString().toStdString();
            }
            if (columns.value(5).toInt() > 0) {
                out += " pk" + columns.value(5).toString().toStdString();
            }
            out += "\n";
        }
        QSqlQuery keys(db.handle());
        REQUIRE(keys.exec(QStringLiteral("PRAGMA foreign_key_list(%1)").arg(table)));
        while (keys.next()) {
            out += "  foreign key " + keys.value(3).toString().toStdString() + " -> "
                   + keys.value(2).toString().toStdString() + "("
                   + keys.value(4).toString().toStdString() + ") on update "
                   + keys.value(5).toString().toStdString() + " on delete "
                   + keys.value(6).toString().toStdString() + "\n";
        }
    }
    QSqlQuery rest(db.handle());
    REQUIRE(rest.exec(QStringLiteral("SELECT type, name, tbl_name, sql FROM sqlite_master "
                                     "WHERE type IN ('index', 'trigger') "
                                     "AND name NOT LIKE 'sqlite_%' ORDER BY type, name")));
    while (rest.next()) {
        out += "\n" + rest.value(0).toString().toStdString() + " "
               + rest.value(1).toString().toStdString() + " on "
               + rest.value(2).toString().toStdString() + "\n  "
               + rest.value(3).toString().toStdString() + "\n";
    }
    return out;
}

/// The schema every library has, checked in. Rewritten from a new library
/// when SEVENTHSLIDE_UPDATE_SCHEMA is set: for a change made on purpose, whose
/// diff is then reviewed with it.
std::filesystem::path golden_schema_file()
{
    return std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "schema" / "library.txt";
}

[[nodiscard]] bool update_golden_schema()
{
    const char* v = std::getenv("SEVENTHSLIDE_UPDATE_SCHEMA");
    return v != nullptr && v[0] != '\0' && std::string(v) != "0";
}

}  // namespace

TEST_CASE("A new library has the schema in fixtures/schema", "[db][schema]")
{
    Database db = open_empty();
    ensure_schema(db);
    const std::string shape = schema_shape(db);
    const auto file = golden_schema_file();
    if (update_golden_schema()) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << shape;
    }
    std::ifstream in(file, std::ios::binary);
    REQUIRE(in.good());
    std::stringstream golden;
    golden << in.rdbuf();
    if (shape != golden.str()) {
        // Say where they part, rather than print both whole.
        std::istringstream now(shape);
        std::istringstream was(golden.str());
        std::string a;
        std::string b;
        int line = 0;
        while (true) {
            const bool more_now = static_cast<bool>(std::getline(now, a));
            const bool more_was = static_cast<bool>(std::getline(was, b));
            ++line;
            if (!more_now && !more_was) {
                break;
            }
            if (!more_now || !more_was || a != b) {
                INFO("line " << line << ": the schema has \"" << (more_now ? a : "(end)")
                              << "\", " << file.string() << " has \""
                              << (more_was ? b : "(end)") << "\". A change made on purpose: "
                              << "run with SEVENTHSLIDE_UPDATE_SCHEMA=1 and review the diff.");
                FAIL("the schema is not the one checked in");
            }
        }
    }
}

TEST_CASE("An empty database gets the current schema", "[db][schema]")
{
    Database db = open_empty();
    REQUIRE(schema_version(db) == 0);
    ensure_schema(db);
    REQUIRE(schema_version(db) == k_schema_version);
    REQUIRE(db.query_int("SELECT COUNT(*) FROM sqlite_master WHERE name = 'freeform_items'", 0)
            == 1);
}

TEST_CASE("Opening a current library changes nothing", "[db][schema]")
{
    Database db = open_empty();
    ensure_schema(db);
    const int tables = table_count(db);
    ensure_schema(db);
    REQUIRE(table_count(db) == tables);
    REQUIRE(schema_version(db) == k_schema_version);
}

TEST_CASE("A library from a newer build is refused", "[db][schema]")
{
    Database db = open_empty();
    ensure_schema(db);
    db.exec("PRAGMA user_version = " + std::to_string(k_schema_version + 1));
    REQUIRE_THROWS(ensure_schema(db));
}

TEST_CASE("A library from an earlier build is refused and left as it was", "[db][schema]")
{
    // No released build made one, so there is nothing to upgrade from.
    Database db = open_empty();
    ensure_schema(db);
    db.exec("PRAGMA user_version = " + std::to_string(k_schema_version - 1));
    const int tables = table_count(db);
    REQUIRE_THROWS(ensure_schema(db));
    CHECK(schema_version(db) == k_schema_version - 1);
    CHECK(table_count(db) == tables);
}

TEST_CASE("A database with unknown tables is refused and left alone", "[db][schema]")
{
    Database db = open_empty();
    db.exec("CREATE TABLE something_else (n INTEGER)");
    REQUIRE_THROWS(ensure_schema(db));
    REQUIRE(table_count(db) == 1);
    REQUIRE(schema_version(db) == 0);
}

namespace {

/// Every uid in ``table``, in row order.
std::vector<std::string> uids(Database& db, const char* table)
{
    std::vector<std::string> out;
    QSqlQuery q(db.handle());
    REQUIRE(q.exec(QStringLiteral("SELECT uid FROM %1 ORDER BY rowid").arg(QLatin1String(table))));
    while (q.next()) {
        out.push_back(q.value(0).toString().toStdString());
    }
    return out;
}

bool is_uid(const std::string& s)
{
    return s.size() == 32 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}

}  // namespace

TEST_CASE("A library keeps its uid", "[db][schema][sync]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    std::string uid;
    {
        Database db = Database::open_file(dir.path / "library.db");
        ensure_schema(db);
        uid = seventhslide::db::library_uid(db);
        REQUIRE(is_uid(uid));
    }
    Database db = Database::open_file(dir.path / "library.db");
    ensure_schema(db);
    CHECK(seventhslide::db::library_uid(db) == uid);

    // Another library is another uid.
    Database other = open_empty();
    ensure_schema(other);
    CHECK(seventhslide::db::library_uid(other) != uid);
}

TEST_CASE("Every new record gets a uid of its own", "[db][schema][sync]")
{
    Database db = open_empty();
    ensure_schema(db);

    // However it is added: the repositories never name the uid.
    seventhslide::model::Song song;
    song.title = "Amazing Grace";
    song.lyrics = "[V1]\nline";
    (void)seventhslide::db::SongRepository(db).insert(song);
    seventhslide::db::FreeformRepository freeform(db);
    seventhslide::model::FreeformItem item;
    item.name = "Welcome";
    const auto original = freeform.create_item(item);
    // A copy, the way Duplicate makes one, is a record of its own.
    auto copy = *freeform.get_item(original);
    copy.id.reset();
    (void)freeform.create_item(copy);
    seventhslide::db::StyleProfileRepository profiles(db);
    (void)profiles.create("Sabbath", seventhslide::model::StyleProfile::empty_data());
    (void)profiles.create("Vespers", seventhslide::model::StyleProfile::empty_data());

    CHECK(is_uid(uids(db, "songs").at(0)));
    const auto items = uids(db, "freeform_items");
    REQUIRE(items.size() == 2);
    CHECK(is_uid(items[0]));
    CHECK(is_uid(items[1]));
    CHECK(items[0] != items[1]);
    const auto profile_uids = uids(db, "style_profiles");
    REQUIRE(profile_uids.size() == 2);
    CHECK(profile_uids[0] != profile_uids[1]);

    // A record arriving from another computer keeps the uid it came with...
    const std::string arriving(32, 'a');
    db.exec("INSERT INTO songs (title, lyrics, uid) VALUES ('It Is Well', '', '" + arriving + "')");
    CHECK(uids(db, "songs").at(1) == arriving);
    // ...and no two rows share one.
    CHECK_THROWS(db.exec("INSERT INTO songs (title, lyrics, uid) VALUES ('Copy', '', '"
                         + arriving + "')"));
    // Editing a record keeps its uid.
    const auto before = uids(db, "songs").at(0);
    db.exec("UPDATE songs SET title = 'Amazing Grace (My Chains)' WHERE rowid = 1");
    CHECK(uids(db, "songs").at(0) == before);
}

