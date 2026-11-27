// What a person is shown for a failure: SeventhSlide's own words as they are,
// a database's or a library's said plainly (their own kept for the log).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <seventhslide/db/database.hpp>
#include <seventhslide/util/error_text.hpp>

#include <nlohmann/json.hpp>

#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <filesystem>
#include <fstream>
#include <new>
#include <stdexcept>
#include <string>

using Catch::Matchers::ContainsSubstring;
using seventhslide::db::Database;
using seventhslide::db::DatabaseError;
using seventhslide::util::message_for_people;

namespace {

/// The DatabaseError ``run`` throws.
template <typename Run>
DatabaseError refused(Run run)
{
    try {
        run();
    } catch (const DatabaseError& ex) {
        return ex;
    }
    FAIL("no DatabaseError");
    throw std::logic_error("unreachable");
}

}  // namespace

TEST_CASE("A database's refusal is said plainly, its own words kept for the log", "[db][errors]")
{
    seventhslide::test::ensure_qt_application();

    SECTION("a statement it refuses")
    {
        auto db = Database::open_memory();
        db.exec("CREATE TABLE t (x TEXT UNIQUE)");
        db.exec("INSERT INTO t VALUES ('a')");
        const auto ex = refused([&] { db.exec("INSERT INTO t VALUES ('a')"); });
        CHECK(ex.code() == 19);  // SQLITE_CONSTRAINT
        CHECK_THAT(ex.what(), ContainsSubstring("UNIQUE constraint failed: t.x"));
        CHECK(message_for_people(ex) == "The library could not do that.");
    }
    SECTION("a library another connection is writing")
    {
        const seventhslide::test::TempDir dir;
        auto writer = Database::open_file(dir.path / "library.db");
        auto other = Database::open_file(dir.path / "library.db");
        writer.exec("CREATE TABLE t (x TEXT)");
        writer.exec("BEGIN IMMEDIATE");
        other.exec("PRAGMA busy_timeout = 0");
        const auto ex = refused([&] { other.exec("INSERT INTO t VALUES ('b')"); });
        writer.exec("ROLLBACK");
        CHECK(ex.code() == 5);  // SQLITE_BUSY
        CHECK_THAT(ex.what(), ContainsSubstring("database is locked"));
        CHECK(message_for_people(ex) == "The library is busy with another change. Try again in a moment.");
    }
    SECTION("a library open read-only")
    {
        auto db = Database::open_memory();
        db.exec("CREATE TABLE t (x TEXT)");
        db.exec("PRAGMA query_only = ON");
        const auto ex = refused([&] { db.exec("INSERT INTO t VALUES ('c')"); });
        CHECK(ex.read_only());
        CHECK(message_for_people(ex) == "This library cannot be changed on this computer.");
    }
    SECTION("a file that is not a database")
    {
        const seventhslide::test::TempDir dir;
        std::ofstream(dir.path / "notes.db") << std::string(4096, 'x');
        const auto ex = refused([&] { (void)Database::open_file(dir.path / "notes.db"); });
        CHECK(ex.code() == 26);  // SQLITE_NOTADB
        CHECK(message_for_people(ex) == "The library's database file is damaged.");
    }
}

TEST_CASE("A library's failure is said plainly; SeventhSlide's own words are shown as they are",
          "[util][errors]")
{
    SECTION("the file system")
    {
        const seventhslide::test::TempDir dir;
        const auto missing = dir.path / "gone.mp4";
        try {
            (void)std::filesystem::file_size(missing);
            FAIL("no filesystem_error");
        } catch (const std::filesystem::filesystem_error& ex) {
            CHECK(message_for_people(ex)
                  == "A file could not be used (No such file or directory): " + missing.string());
        }
    }
    SECTION("JSON")
    {
        try {
            // Kept, not cast to void: GCC still warns (unused-result) for a cast.
            [[maybe_unused]] const auto parsed = nlohmann::json::parse("{not json");
            FAIL("no json exception");
        } catch (const nlohmann::json::exception& ex) {
            CHECK(message_for_people(ex)
                  == "Some data could not be read: it is not in the form SeventhSlide expects.");
        }
    }
    SECTION("memory")
    {
        CHECK(message_for_people(std::bad_alloc()) == "SeventhSlide ran out of memory.");
    }
    SECTION("SeventhSlide's own")
    {
        const std::runtime_error own("This invite has expired. Ask for a new one.");
        CHECK(message_for_people(own) == "This invite has expired. Ask for a new one.");
        const std::invalid_argument also("A size limit is not less than nothing");
        CHECK(message_for_people(also) == "A size limit is not less than nothing");
    }
}
