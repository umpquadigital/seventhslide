// The schema against db/sync_tables.hpp: every table says how it syncs, every
// table that syncs by record has its uid, and every reference between rows is
// listed for the sync engine to translate.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/media_kind.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/sync_bookkeeping.hpp>
#include <seventhslide/db/sync_records.hpp>
#include <seventhslide/db/sync_tables.hpp>
#include <seventhslide/theme/theme_library.hpp>

#include "sync_test_helpers.hpp"
#include "test_qt_app.hpp"

#include <QSqlQuery>
#include <QString>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;
using seventhslide::db::Database;
using seventhslide::db::k_sync_references;
using seventhslide::db::k_sync_tables;
using seventhslide::db::SyncAs;

namespace {

Database current_library()
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

std::set<std::string> names(Database& db, const QString& sql)
{
    std::set<std::string> out;
    QSqlQuery q(db.handle());
    REQUIRE(q.exec(sql));
    while (q.next()) {
        out.insert(q.value(0).toString().toStdString());
    }
    return out;
}

/// The library's own tables; SQLite's (``sqlite_sequence``) are not ours.
std::set<std::string> tables(Database& db)
{
    return names(db, QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' "
                                     "AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\'"));
}

std::set<std::string> columns(Database& db, std::string_view table)
{
    return names(db, QStringLiteral("SELECT name FROM pragma_table_info('%1')")
                         .arg(QString::fromUtf8(table)));
}

}  // namespace

TEST_CASE("Every table says how it syncs", "[db][sync]")
{
    Database db = current_library();
    std::set<std::string> listed;
    for (const auto& t : k_sync_tables) {
        INFO(t.table);
        CHECK(listed.insert(std::string(t.table)).second);  // once each
    }
    CHECK(tables(db) == listed);
}

TEST_CASE("Tables that sync by record name every row by uid", "[db][sync]")
{
    Database db = current_library();
    const auto indexes = names(db, QStringLiteral("SELECT sql FROM sqlite_master "
                                                  "WHERE type = 'index' AND sql IS NOT NULL"));
    const auto triggers = names(db, QStringLiteral("SELECT tbl_name FROM sqlite_master "
                                                   "WHERE type = 'trigger' AND sql LIKE "
                                                   "'%WHEN NEW.uid IS NULL%'"));
    for (const auto& t : k_sync_tables) {
        INFO(t.table);
        const bool record = t.as == SyncAs::Record;
        CHECK(columns(db, t.table).contains("uid") == record);
        CHECK(triggers.contains(std::string(t.table)) == record);
        const std::string unique = "CREATE UNIQUE INDEX idx_" + std::string(t.table) + "_uid ON "
                                   + std::string(t.table) + "(uid)";
        CHECK(indexes.contains(unique) == record);
    }
}

TEST_CASE("Every table that syncs marks the outbox on every write", "[db][sync]")
{
    Database db = current_library();
    const auto triggers = names(db, QStringLiteral("SELECT name FROM sqlite_master "
                                                   "WHERE type = 'trigger' AND sql LIKE "
                                                   "'%INSERT INTO sync_outbox%'"));
    std::set<std::string> expected;
    for (const auto& t : k_sync_tables) {
        if (t.as != SyncAs::Local) {
            for (const std::string_view write : {"insert", "update", "delete"}) {
                expected.insert("trg_" + std::string(t.table) + "_sync_" + std::string(write));
            }
        }
    }
    CHECK(triggers == expected);
}

TEST_CASE("Tables that sync by key have their key columns", "[db][sync]")
{
    Database db = current_library();
    for (const auto& t : k_sync_tables) {
        INFO(t.table);
        if (t.as == SyncAs::Key) {
            const auto have = columns(db, t.table);
            std::string_view rest = t.key;
            while (!rest.empty()) {
                const auto comma = rest.find(',');
                auto column = rest.substr(0, comma);
                while (column.starts_with(' ')) {
                    column.remove_prefix(1);
                }
                CHECK(have.contains(std::string(column)));
                rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            }
        } else if (t.as == SyncAs::Part) {
            CHECK(std::ranges::any_of(k_sync_tables, [&](const auto& o) { return o.table == t.key; }));
        } else if (t.as == SyncAs::JsonId) {
            CHECK(t.key.starts_with("data."));
            CHECK(columns(db, t.table).contains("data"));
        }
    }
}

TEST_CASE("Every reference between rows is listed for sync", "[db][sync]")
{
    Database db = current_library();
    std::set<std::pair<std::string, std::string>> foreign_keys;
    for (const auto& table : tables(db)) {
        QSqlQuery q(db.handle());
        REQUIRE(q.exec(QStringLiteral("SELECT \"from\", \"table\" FROM pragma_foreign_key_list('%1')")
                           .arg(QString::fromStdString(table))));
        while (q.next()) {
            foreign_keys.emplace(table + "." + q.value(0).toString().toStdString(),
                                 q.value(1).toString().toStdString());
        }
    }

    std::set<std::pair<std::string, std::string>> listed;
    const auto known = tables(db);
    for (const auto& r : k_sync_references) {
        INFO(r.table << "." << r.field);
        CHECK(known.contains(std::string(r.table)));
        CHECK(known.contains(std::string(r.target)));
        if (r.in != seventhslide::db::RefIn::Column) {
            CHECK_FALSE(r.note.empty());
        } else {
            listed.emplace(std::string(r.table) + "." + std::string(r.field), std::string(r.target));
        }
    }
    CHECK(foreign_keys == listed);
}

TEST_CASE("Each file a record owns is in its kind's folder, and its record syncs", "[db][sync]")
{
    using seventhslide::db::FileIn;
    using seventhslide::db::k_sync_files;
    // Every media kind's files belong to its registry, in its folder.
    for (const auto kind : seventhslide::db::k_media_kinds) {
        const auto folder = seventhslide::db::media_subdir(kind);
        INFO(folder);
        CHECK(std::ranges::count_if(k_sync_files, [&](const auto& f) {
                  return f.folder == folder && f.in == FileIn::Key;
              })
              == 1);
    }
    // Themes' backgrounds are named by URL from the outputs.
    const auto themes =
        seventhslide::theme::theme_assets_dir("media").filename().string();
    CHECK(std::ranges::any_of(k_sync_files, [&](const auto& f) {
        return f.folder == themes && f.in == FileIn::Url && f.table == "outputs";
    }));
    CHECK(std::string(seventhslide::theme::k_theme_bg_url_prefix) == "/static/" + themes + "/");
    // A file's record is named by the file's name; a record naming files by
    // URL syncs as a record.
    for (const auto& f : k_sync_files) {
        INFO(f.table);
        const auto t = std::ranges::find(k_sync_tables, f.table,
                                         &seventhslide::db::SyncTable::table);
        REQUIRE(t != k_sync_tables.end());
        if (f.in == FileIn::Key) {
            CHECK(t->as == SyncAs::Key);
            CHECK(t->key == "filename");
        } else {
            CHECK((t->as == SyncAs::Key || t->as == SyncAs::Record || t->as == SyncAs::JsonId));
        }
    }
}

TEST_CASE("Word lists, copies' names and the places files are named are columns", "[db][sync]")
{
    using seventhslide::db::k_sync_file_names;
    using seventhslide::db::k_sync_keep_both;
    using seventhslide::db::k_sync_word_lists;
    Database db = current_library();
    for (const auto& w : k_sync_word_lists) {
        INFO(w.table << "." << w.field);
        CHECK(columns(db, w.table).contains(std::string(w.field)));
    }
    for (const auto& k : k_sync_keep_both) {
        INFO(k.table << "." << k.name);
        CHECK(columns(db, k.table).contains(std::string(k.name)));
    }
    for (const auto& f : k_sync_file_names) {
        INFO(f.table << "." << f.column);
        CHECK(columns(db, f.table).contains(std::string(f.column)));
    }
    // A list of words is one: a table that gains tags or topics says so.
    for (const auto& table : tables(db)) {
        for (const auto& column : columns(db, table)) {
            if (column == "tags" || column == "topics") {
                INFO(table << "." << column);
                CHECK(std::ranges::any_of(k_sync_word_lists, [&](const auto& w) {
                    return w.table == table && w.field == column;
                }));
            }
        }
    }
}

TEST_CASE("A file's name is found only where a record may name one", "[db][sync]")
{
    using seventhslide::db::k_sync_file_names;
    Database db = current_library();
    seventhslide::test::fill_library(db);
    db.exec(R"(INSERT INTO service_items (service_id, order_num, item_type, data)
               VALUES (1, 3, 'image', '{"filename":"ab12.png","title":"Lilies"}'),
                      (1, 4, 'video', '{"filename":"cd34.mp4"}'),
                      (1, 5, 'audio', '{"filename":"ef56.mp3"}'))");
    db.exec(R"(INSERT INTO freeform_items (name, document) VALUES ('Welcome',
               '{"decks":[{"slides":[{"background":{"kind":"image","image":"images/ab12.png"},
                 "objects":[{"type":"image","filename":"ab12.png"}]}]}]}'))");
    // A title that reads as a file's name is a title.
    db.exec("INSERT INTO songs (title, lyrics) VALUES ('ab12.png', '[V1]\nline')");
    const std::vector<std::string> files{"ab12.png", "cd34.mp4", "ef56.mp3"};

    for (const auto table : seventhslide::db::record_tables_in_order()) {
        // A file's own record, or one owning files by URL alone. A Freeform
        // document owns its own files by URL and names the library's.
        if (std::ranges::any_of(seventhslide::db::k_sync_files, [&](const auto& f) {
                return f.table == table && (f.in == seventhslide::db::FileIn::Key || !f.per_owner);
            })) {
            continue;
        }
        for (const auto& key : seventhslide::db::record_keys(db, table)) {
            const auto document = seventhslide::db::read_record(db, table, key);
            REQUIRE(document.has_value());
            const auto names = seventhslide::db::names_in(table, key, *document);
            for (const auto& [column, value] : document->items()) {
                const auto text = value.is_string() ? value.get<std::string>() : value.dump();
                const bool declared = std::ranges::any_of(k_sync_file_names, [&](const auto& f) {
                    return f.table == table && f.column == column;
                });
                for (const auto& file : files) {
                    if (text.find(file) == std::string::npos) {
                        continue;
                    }
                    INFO(table << " " << key << " ." << column << " holds " << file);
                    if (table == "songs") {
                        CHECK_FALSE(names.contains(file));  // the title
                    } else {
                        CHECK(declared);
                        CHECK(names.contains(file));
                    }
                }
            }
        }
    }
    // A folder's entry names its file, and its folder, by its key.
    const auto entry = seventhslide::db::record_keys(db, "image_folder_items").front();
    const auto names = seventhslide::db::names_in("image_folder_items", entry, json::object());
    CHECK(names.contains("ab12.png"));
    CHECK(names.contains(entry.substr(0, entry.find('/'))));
}

TEST_CASE("What points at a record is found alike one name at a time or many", "[db][sync]")
{
    Database db = current_library();
    seventhslide::db::SyncBookkeeping book(db);
    const std::string song(32, 'a');
    const std::string folder(32, 'b');
    // A name from an early build, with what JSON writes otherwise in it.
    const std::string quoted = "my \"best\".png";
    const std::string item(32, 'c');
    const std::string entry = folder + "/" + quoted;
    book.set_shared("service_items", item,
                    {json{{"data", json{{"filename", quoted}, {"song", song}}.dump()}}, {}, std::nullopt});
    book.set_shared("image_folder_items", entry, {json{{"sort_order", 1}}, {}, std::nullopt});
    // A title is text.
    book.set_shared("songs", std::string(32, 'd'), {json{{"title", quoted}}, {}, std::nullopt});

    std::set<std::string> many{song, folder, quoted};
    for (char c = '0'; c <= '9'; ++c) {
        many.insert(std::string(31, 'e') + c);
    }
    auto together = book.mentioning(many);
    using Found = std::vector<std::pair<std::string, std::string>>;
    const std::map<std::string, Found> expected{
        {song, {{"service_items", item}}},
        {folder, {{"image_folder_items", entry}}},
        {quoted, {{"image_folder_items", entry}, {"service_items", item}}},
    };
    for (const auto& [name, found] : expected) {
        INFO(name);
        CHECK(together[name] == found);
        CHECK(book.mentioning(std::set<std::string>{name})[name] == found);
    }
    CHECK(together.size() == expected.size());
}
