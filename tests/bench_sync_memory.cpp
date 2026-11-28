// The memory a computer takes a large library's snapshot in with: writing
// it, joining from it, and catching up from it after long away. A program
// of its own, with only core in it, so little else counts; each step is a
// run of its own, so its peak is measured alone:
//
//   export SEVENTHSLIDE_BENCH_DIR=/tmp/sync-memory
//   ./seventhslide_bench_sync_memory "bench: sync memory: make"
//   /usr/bin/time -v ./seventhslide_bench_sync_memory "bench: sync memory: nothing"
//   /usr/bin/time -v ./seventhslide_bench_sync_memory "bench: sync memory: write"
//   /usr/bin/time -v ./seventhslide_bench_sync_memory "bench: sync memory: join"
//   /usr/bin/time -v ./seventhslide_bench_sync_memory "bench: sync memory: catch up"
//
// "Maximum resident set size" is the peak; "nothing" is what the program
// takes before any of it. The library is 10 Bibles and 2,000 songs, a
// snapshot of about 55 MB (``docs/SERVER.md``). The time syncs take is
// measured below, on a library of its own.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/crypto/signing.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/sync_outbox.hpp>
#include <seventhslide/sync/identity.hpp>
#include <seventhslide/sync/sync_engine.hpp>
#include <seventhslide/sync/sync_store.hpp>

#include <QCoreApplication>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace {

using seventhslide::db::Database;
using seventhslide::sync::FolderStore;
using seventhslide::sync::Identity;
using seventhslide::sync::SyncEngine;

std::filesystem::path bench_dir()
{
    static int argc = 1;
    static char arg0[] = "seventhslide_bench_sync_memory";
    static char* argv[] = {arg0, nullptr};
    // Never destroyed, as the tests' (``test_qt_app.hpp``): destroyed after
    // main returns, it crashed the program on its way out.
    if (QCoreApplication::instance() == nullptr) {
        new QCoreApplication(argc, argv);
    }
    const char* dir = std::getenv("SEVENTHSLIDE_BENCH_DIR");
    REQUIRE(dir != nullptr);
    return dir;
}

Database open_library(const std::filesystem::path& file)
{
    auto db = Database::open_file(file);
    seventhslide::db::ensure_schema(db);
    return db;
}

/// A computer: what it signs is not read back here, so a new key each run.
Identity identity(const std::string& machine)
{
    return {machine, machine, seventhslide::crypto::SigningKey::generate()};
}

}  // namespace

TEST_CASE("bench: sync memory: make", "[.bench][.sync_memory]")
{
    const auto dir = bench_dir();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    FolderStore store(dir / "store");
    std::string library;
    {
        auto church_db = open_library(dir / "church.db");
        library = seventhslide::db::library_uid(church_db);
        for (int b = 1; b <= 10; ++b) {
            church_db.exec("INSERT INTO bibles (id, name) VALUES (" + std::to_string(b) + ", 'Bible "
                           + std::to_string(b) + "')");
            church_db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 31102) "
                           "INSERT INTO verses (bible_id, book, chapter, verse_num, text) SELECT "
                           + std::to_string(b)
                           + ", 'Book ' || (i / 1000), (i / 30) % 150 + 1, i % 30 + 1, "
                             "substr(replace(hex(randomblob(70)), '0', ' '), 1, 130) FROM n");
        }
        church_db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 2000) "
                       "INSERT INTO songs (title, lyrics) SELECT 'Hymn ' || i, "
                       "'[V1]\n' || substr(replace(hex(randomblob(1000)), '0', ' '), 1, 2000) FROM n");
        SyncEngine church(church_db, store, dir / "church-media", identity("church"));
        church.share_everything();
        church.sync();

        // The laptop has it all, then goes away.
        {
            auto laptop_db = open_library(dir / "laptop.db");
            SyncEngine laptop(laptop_db, store, dir / "laptop-media", identity("laptop"));
            laptop.join(library);
            laptop.sync();
        }
        std::filesystem::rename(dir / "laptop.db", dir / "laptop-away.db");

        // Meanwhile a few songs change, a snapshot is written, and the files
        // it holds go.
        church_db.exec("UPDATE songs SET key = 'A' WHERE title IN ('Hymn 5', 'Hymn 500', 'Hymn 1500')");
        church.sync();
        REQUIRE(church.snapshot());
        const auto files = store.sequences(seventhslide::sync::Numbered::Changes, "church");
        store.drop_changes_through("church", files.back());
    }
    std::uintmax_t bytes = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir / "store")) {
        if (entry.is_regular_file() && entry.path().string().find("snapshot") != std::string::npos) {
            bytes += entry.file_size();
        }
    }
    std::cout << "snapshot: " << bytes / 1000000 << " MB\n";
}

TEST_CASE("bench: sync memory: nothing", "[.bench][.sync_memory]")
{
    const auto dir = bench_dir();
    std::filesystem::remove(dir / "nothing.db");
    auto db = open_library(dir / "nothing.db");
    CHECK_FALSE(seventhslide::db::library_uid(db).empty());
}

TEST_CASE("bench: sync memory: write", "[.bench][.sync_memory]")
{
    // Into a store of its own, so every part is written, none only touched.
    const auto dir = bench_dir();
    std::filesystem::remove_all(dir / "write-store");
    std::filesystem::copy_file(dir / "church.db", dir / "write.db", std::filesystem::copy_options::overwrite_existing);
    FolderStore store(dir / "write-store");
    auto db = Database::open_file(dir / "write.db");
    SyncEngine church(db, store, dir / "church-media", identity("church"));
    CHECK(church.snapshot());
    CHECK(store.parts().size() > 10);
}

TEST_CASE("bench: sync memory: join", "[.bench][.sync_memory]")
{
    const auto dir = bench_dir();
    std::filesystem::remove(dir / "join.db");
    FolderStore store(dir / "store");
    auto church_db = Database::open_file(dir / "church.db");
    const auto library = seventhslide::db::library_uid(church_db);
    auto db = open_library(dir / "join.db");
    SyncEngine office(db, store, dir / "office-media", identity("office"));
    office.join(library);
    CHECK(db.query_int64("SELECT COUNT(*) FROM verses", 0) == 311020);
}

TEST_CASE("bench: sync memory: catch up", "[.bench][.sync_memory]")
{
    const auto dir = bench_dir();
    std::filesystem::copy_file(dir / "laptop-away.db", dir / "laptop.db",
                               std::filesystem::copy_options::overwrite_existing);
    FolderStore store(dir / "store");
    auto db = Database::open_file(dir / "laptop.db");
    SyncEngine laptop(db, store, dir / "laptop-media", identity("laptop"));
    laptop.set_snapshot_after(0);
    CHECK(laptop.sync().caught_up);
    CHECK(db.query_int64("SELECT COUNT(*) FROM songs WHERE key = 'A'", 0) == 3);
}

// The time a sync takes on a library of a church's size: 5,000 songs, 300
// services of 12 items, 5,000 pictures in 50 folders, and a profile naming
// 3,000 songs. Each step after "make" is timed on fresh copies:
//
//   ./seventhslide_bench_sync_memory "bench: sync time: make"
//   ./seventhslide_bench_sync_memory "[.sync_time]"

namespace {

std::filesystem::path time_dir()
{
    return bench_dir() / "time";
}

/// ``from`` copied to ``to``, a file or a folder, replacing what was there.
void fresh_copy(const std::filesystem::path& from, const std::filesystem::path& to)
{
    std::filesystem::remove_all(to);
    std::filesystem::copy(from, to, std::filesystem::copy_options::recursive);
}

template <typename F>
double seconds_of(F&& f)
{
    const auto start = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void fill_church(Database& db)
{
    db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 5000) "
            "INSERT INTO songs (id, title, lyrics) SELECT i, 'Hymn ' || i, "
            "'[V1]\n' || substr(replace(hex(randomblob(600)), '0', ' '), 1, 1200) FROM n");
    db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 300) "
            "INSERT INTO services (id, name) SELECT i, 'Service ' || i FROM n");
    db.exec("WITH RECURSIVE n(i) AS (SELECT 0 UNION ALL SELECT i + 1 FROM n WHERE i < 3599) "
            "INSERT INTO service_items (service_id, song_id, order_num, item_type) "
            "SELECT i / 12 + 1, (i * 7) % 5000 + 1, i % 12, 'song' FROM n");
    db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 50) "
            "INSERT INTO image_folders (id, name) SELECT i, 'Folder ' || i FROM n");
    db.exec("WITH RECURSIVE n(i) AS (SELECT 0 UNION ALL SELECT i + 1 FROM n WHERE i < 4999) "
            "INSERT INTO image_files (filename, display_name) SELECT 'p' || i || '.png', 'Picture ' || i FROM n");
    db.exec("WITH RECURSIVE n(i) AS (SELECT 0 UNION ALL SELECT i + 1 FROM n WHERE i < 4999) "
            "INSERT INTO image_folder_items (folder_id, filename, sort_order) "
            "SELECT i / 100 + 1, 'p' || i || '.png', i % 100 FROM n");
    db.exec("INSERT INTO style_profiles (id, name, data) SELECT 1, 'Default', "
            "json_object('songs', json((WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 3000) "
            "SELECT json_group_object(i, json_object('main', 't1')) FROM n)))");
}

}  // namespace

TEST_CASE("bench: sync time: make", "[.bench][.sync_make]")
{
    const auto dir = time_dir();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        auto db = open_library(dir / "church.db");
        fill_church(db);
    }
    fresh_copy(dir / "church.db", dir / "unshared.db");
    FolderStore store(dir / "store");
    std::string library;
    {
        auto church_db = Database::open_file(dir / "church.db");
        library = seventhslide::db::library_uid(church_db);
        SyncEngine church(church_db, store, dir / "church-media", identity("church"));
        church.set_snapshot_after(0);
        church.share_everything();
        church.sync();
        {
            auto laptop_db = open_library(dir / "laptop.db");
            SyncEngine laptop(laptop_db, store, dir / "laptop-media", identity("laptop"));
            laptop.join(library);
        }
        fresh_copy(dir / "laptop.db", dir / "joined.db");
        fresh_copy(dir / "store", dir / "store-shared");
        {
            auto laptop_db = Database::open_file(dir / "laptop.db");
            SyncEngine laptop(laptop_db, store, dir / "laptop-media", identity("laptop"));
            laptop.set_snapshot_after(0);
            laptop.sync();
            REQUIRE(laptop_db.query_int64("SELECT COUNT(*) FROM songs", 0) == 5000);
        }
        fresh_copy(dir / "laptop.db", dir / "laptop-base.db");
        fresh_copy(dir / "store", dir / "store-base");

        // 200 change files, each of two songs edited, one added and a
        // service item deleted.
        for (int i = 0; i < 200; ++i) {
            church_db.exec("UPDATE songs SET key = 'K" + std::to_string(i) + "' WHERE id IN ("
                           + std::to_string(i * 13 % 5000 + 1) + ", " + std::to_string(i * 29 % 5000 + 1) + ")");
            church_db.exec("INSERT INTO songs (title, lyrics) VALUES ('New " + std::to_string(i) + "', '[V1]\nline')");
            church_db.exec("DELETE FROM service_items WHERE id = " + std::to_string(i * 17 + 1));
            church.sync();
        }
    }
    const auto files = store.sequences(seventhslide::sync::Numbered::Changes, "church");
    std::cout << "change files from the church: " << files.size() << "\n";
}

TEST_CASE("bench: sync time: share everything", "[.bench][.sync_time]")
{
    const auto dir = time_dir();
    fresh_copy(dir / "unshared.db", dir / "run.db");
    std::filesystem::remove_all(dir / "run-store");
    FolderStore store(dir / "run-store");
    auto db = Database::open_file(dir / "run.db");
    SyncEngine church(db, store, dir / "church-media", identity("church"));
    church.set_snapshot_after(0);
    std::cout << "share everything: mark " << seconds_of([&] { church.share_everything(); }) << " s, ";
    std::cout << "send " << seconds_of([&] { church.sync(); }) << " s\n";
    CHECK(seventhslide::db::marked_records(db).empty());
}

TEST_CASE("bench: sync time: join by replaying", "[.bench][.sync_time]")
{
    const auto dir = time_dir();
    fresh_copy(dir / "joined.db", dir / "run.db");
    fresh_copy(dir / "store-shared", dir / "run-store");
    FolderStore store(dir / "run-store");
    auto db = Database::open_file(dir / "run.db");
    SyncEngine laptop(db, store, dir / "laptop-media", identity("laptop"));
    laptop.set_snapshot_after(0);
    seventhslide::sync::SyncResult result;
    std::cout << "join by replaying: " << seconds_of([&] { result = laptop.sync(); }) << " s, "
              << result.files_applied << " files\n";
    CHECK(db.query_int64("SELECT COUNT(*) FROM songs", 0) == 5000);
}

TEST_CASE("bench: sync time: 200 files", "[.bench][.sync_time]")
{
    const auto dir = time_dir();
    // Marked here: nothing, every song, or the items of 25 services, whose
    // table points at the songs the files add and the items they delete.
    const std::pair<const char*, const char*> marks[] = {
        {"none marked", ""},
        {"5,000 songs marked", "UPDATE songs SET comments = 'laptop'"},
        {"300 service items marked", "UPDATE service_items SET data = '{}' WHERE service_id <= 25"},
    };
    for (const auto& [what, mark] : marks) {
        for (const bool incoming : {false, true}) {
            fresh_copy(dir / "laptop-base.db", dir / "run.db");
            fresh_copy(dir / (incoming ? "store" : "store-base"), dir / "run-store");
            FolderStore store(dir / "run-store");
            auto db = Database::open_file(dir / "run.db");
            if (*mark != '\0') {
                db.exec(mark);
            }
            SyncEngine laptop(db, store, dir / "laptop-media", identity("laptop"));
            laptop.set_snapshot_after(0);
            seventhslide::sync::SyncResult result;
            std::cout << (incoming ? "200 files, " : "no files, ") << what << ": "
                      << seconds_of([&] { result = laptop.sync(); }) << " s, " << result.files_applied
                      << " files\n";
            CHECK(result.files_applied == (incoming ? 200 : 0));
        }
    }
}
