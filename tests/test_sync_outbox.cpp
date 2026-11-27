// The sync outbox (schema 6): every write to a table that syncs marks the
// record it touched, named as the other computers name it.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/db/sync_outbox.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/library.hpp>
#include <seventhslide/model/output_config.hpp>

#include "test_qt_app.hpp"

#include <QSqlQuery>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using seventhslide::db::Database;
using seventhslide::db::marked_records;
using seventhslide::db::MarkedRecord;

namespace {

Database current_library()
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

std::string uid_of(Database& db, const std::string& table, std::int64_t id)
{
    QSqlQuery q(db.handle());
    REQUIRE(q.exec(QStringLiteral("SELECT uid FROM %1 WHERE id = %2")
                       .arg(QString::fromStdString(table))
                       .arg(id)));
    REQUIRE(q.next());
    return q.value(0).toString().toStdString();
}

std::int64_t last_id(Database& db)
{
    return db.query_int64("SELECT last_insert_rowid()", 0);
}

using Marks = std::vector<MarkedRecord>;

/// ``marks`` in the outbox's order, for marks whose keys are random uids.
Marks sorted(Marks marks)
{
    std::ranges::sort(marks);
    return marks;
}

}  // namespace

template <>
struct Catch::StringMaker<MarkedRecord> {
    static std::string convert(const MarkedRecord& m) { return m.table + ":" + m.key; }
};

TEST_CASE("A record is marked by its uid when added, changed and deleted", "[db][sync]")
{
    Database db = current_library();
    seventhslide::db::SongRepository songs(db);
    seventhslide::model::Song song;
    song.title = "Amazing Grace";
    song.lyrics = "[V1]\nline";
    const auto id = songs.insert(song);
    const auto uid = uid_of(db, "songs", id);
    CHECK(marked_records(db) == Marks{{"songs", uid}});

    // Changed twice and deleted: still one mark, which outlives the row.
    db.exec("DELETE FROM sync_outbox");
    db.exec("UPDATE songs SET title = 'Amazing Grace!' WHERE id = " + std::to_string(id));
    db.exec("UPDATE songs SET key = 'G' WHERE id = " + std::to_string(id));
    CHECK(marked_records(db) == Marks{{"songs", uid}});
    db.exec("DELETE FROM sync_outbox");
    REQUIRE(songs.remove(id));
    CHECK(marked_records(db) == Marks{{"songs", uid}});
}

TEST_CASE("Every table that syncs by record marks a new row by its uid", "[db][sync]")
{
    Database db = current_library();
    db.exec("INSERT INTO services (name) VALUES ('Sabbath')");
    const auto service = last_id(db);
    db.exec("DELETE FROM sync_outbox");

    const std::vector<std::pair<std::string, std::string>> inserts = {
        {"songs", "INSERT INTO songs (title, lyrics) VALUES ('A', '')"},
        {"services", "INSERT INTO services (name) VALUES ('Vespers')"},
        {"service_items", "INSERT INTO service_items (service_id, order_num) VALUES ("
                              + std::to_string(service) + ", 0)"},
        {"freeform_folders", "INSERT INTO freeform_folders (name) VALUES ('F')"},
        {"freeform_items", "INSERT INTO freeform_items (name) VALUES ('Sermon')"},
        {"image_folders", "INSERT INTO image_folders (name) VALUES ('F')"},
        {"video_folders", "INSERT INTO video_folders (name) VALUES ('F')"},
        {"audio_folders", "INSERT INTO audio_folders (name) VALUES ('F')"},
        {"bibles", "INSERT INTO bibles (name) VALUES ('KJV')"},
        {"style_profiles", "INSERT INTO style_profiles (name) VALUES ('Default')"},
    };
    for (const auto& [table, sql] : inserts) {
        INFO(table);
        db.exec(sql);
        CHECK(marked_records(db) == Marks{{table, uid_of(db, table, last_id(db))}});
        db.exec("DELETE FROM sync_outbox");
    }
}

TEST_CASE("A row that syncs by key is marked by its key", "[db][sync]")
{
    Database db = current_library();
    db.exec("DELETE FROM sync_outbox");

    seventhslide::db::ConfigRepository config(db);
    config.set_setting("songs_sort", "title");
    config.set_setting("songs_sort", "author");
    db.exec("INSERT INTO video_files (filename, display_name) VALUES ('ab12.mp4', 'Intro')");
    db.exec("UPDATE video_files SET display_name = 'Welcome' WHERE filename = 'ab12.mp4'");
    CHECK(marked_records(db)
          == Marks{{"app_settings", "songs_sort"}, {"video_files", "ab12.mp4"}});

    // Renaming a key marks the record it was and the one it is.
    db.exec("DELETE FROM sync_outbox");
    db.exec("UPDATE video_files SET filename = 'cd34.mp4' WHERE filename = 'ab12.mp4'");
    CHECK(marked_records(db) == Marks{{"video_files", "ab12.mp4"}, {"video_files", "cd34.mp4"}});
}

TEST_CASE("A folder item is marked by its folder's uid and its filename", "[db][sync]")
{
    Database db = current_library();
    db.exec("INSERT INTO image_folders (name) VALUES ('Backgrounds')");
    const auto from = last_id(db);
    db.exec("INSERT INTO image_folders (name, parent_id) VALUES ('Easter', "
            + std::to_string(from) + ")");
    const auto to = last_id(db);
    const auto from_uid = uid_of(db, "image_folders", from);
    const auto to_uid = uid_of(db, "image_folders", to);
    db.exec("DELETE FROM sync_outbox");

    db.exec("INSERT INTO image_folder_items (folder_id, filename) VALUES ("
            + std::to_string(from) + ", 'ab12.png')");
    CHECK(marked_records(db) == Marks{{"image_folder_items", from_uid + "/ab12.png"}});

    // Moved to another folder: gone from one, in the other.
    db.exec("DELETE FROM sync_outbox");
    db.exec("UPDATE image_folder_items SET folder_id = " + std::to_string(to));
    CHECK(marked_records(db)
          == sorted({{"image_folder_items", from_uid + "/ab12.png"},
                     {"image_folder_items", to_uid + "/ab12.png"}}));

    // A folder deleted takes its folders and items with it; the folders'
    // marks cover the items, which mark nothing of their own.
    db.exec("DELETE FROM sync_outbox");
    db.exec("DELETE FROM image_folders WHERE id = " + std::to_string(from));
    CHECK(db.query_int("SELECT COUNT(*) FROM image_folder_items", -1) == 0);
    CHECK(marked_records(db)
          == sorted({{"image_folders", from_uid}, {"image_folders", to_uid}}));
}

TEST_CASE("A verse marks its Bible", "[db][sync]")
{
    Database db = current_library();
    seventhslide::db::BibleRepository bibles(db);
    const auto id = bibles.import_bible("KJV", "", {{{}, 0, "John", 3, 16, "For God so loved"},
                                                   {{}, 0, "John", 3, 17, "For God sent"}});
    const auto uid = uid_of(db, "bibles", id);
    CHECK(marked_records(db) == Marks{{"bibles", uid}});

    db.exec("DELETE FROM sync_outbox");
    db.exec("UPDATE verses SET text = 'For God so loved the world' WHERE verse_num = 16");
    CHECK(marked_records(db) == Marks{{"bibles", uid}});

    db.exec("DELETE FROM sync_outbox");
    REQUIRE(bibles.remove(id));
    CHECK(db.query_int("SELECT COUNT(*) FROM verses", -1) == 0);
    CHECK(marked_records(db) == Marks{{"bibles", uid}});
}

TEST_CASE("Saving the outputs marks them all at once", "[db][sync]")
{
    Database db = current_library();
    db.exec("DELETE FROM sync_outbox");
    seventhslide::db::ConfigRepository config(db);
    seventhslide::model::OutputConfig main;
    main.id = "main";
    main.name = "Main";
    seventhslide::model::OutputConfig foldback;
    foldback.id = "foldback";
    foldback.name = "Foldback";
    config.save_outputs({main, foldback});
    CHECK(marked_records(db) == Marks{{"outputs", ""}});
}
