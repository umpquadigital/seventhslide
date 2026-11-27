#include "seventhslide/db/schema.hpp"

#include "seventhslide/db/query.hpp"

#include <QSqlQuery>
#include <QVariant>

#include <stdexcept>
#include <string>
#include <string_view>

namespace seventhslide::db {
namespace {

/// SQL for a fresh uid: 32 random lower-case hex digits.
constexpr std::string_view k_new_uid_sql = "lower(hex(randomblob(16)))";

/// ``<kind>_folders``: a folder tree whose folders sync by uid.
void create_folder_table(Database& db, std::string_view kind)
{
    const std::string folders = std::string(kind) + "_folders";
    db.exec("CREATE TABLE " + folders + " (\n"
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
            "    name TEXT NOT NULL,\n"
            "    sort_order INTEGER DEFAULT 0,\n"
            "    parent_id INTEGER,\n"
            "    uid TEXT,\n"
            "    FOREIGN KEY(parent_id) REFERENCES " + folders + "(id) ON DELETE CASCADE\n"
            ")");
    db.exec("CREATE INDEX idx_" + folders + "_parent ON " + folders + "(parent_id)");
}

/// ``<kind>_folders`` (a folder tree) and ``<kind>_folder_items`` (the files
/// filed in each folder, by filename) for one media kind. Every media kind
/// has the same pair: db::MediaRepository picks the pair by MediaKind.
void create_media_folder_tables(Database& db, std::string_view kind)
{
    const std::string folders = std::string(kind) + "_folders";
    const std::string items = std::string(kind) + "_folder_items";
    create_folder_table(db, kind);
    db.exec("CREATE TABLE " + items + " (\n"
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
            "    folder_id INTEGER NOT NULL,\n"
            "    filename TEXT NOT NULL,\n"
            "    sort_order INTEGER DEFAULT 0,\n"
            "    FOREIGN KEY(folder_id) REFERENCES " + folders + "(id) ON DELETE CASCADE\n"
            ")");
    db.exec("CREATE INDEX idx_" + items + "_folder ON " + items + "(folder_id)");
    db.exec("CREATE INDEX idx_" + items + "_filename ON " + items + "(filename)");
    db.exec("CREATE UNIQUE INDEX idx_" + items + "_unique ON " + items + "(folder_id, filename)");
}

/// ``<kind>_files``: a media kind's registry. A file is in it because it was
/// imported, never because it sits in the media folder.
void create_media_registry(Database& db, std::string_view kind)
{
    // An audio file's own fades (model::set_audio_fades): last, as schema 15
    // added it to the libraries before it.
    const std::string audio_fades =
        kind == "audio" ? ",\n    audio_fades TEXT NOT NULL DEFAULT '{}'" : "";
    db.exec("CREATE TABLE " + std::string(kind) + R"SQL(_files (
    filename TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    library_visible INTEGER DEFAULT 1)SQL"
            + audio_fades + "\n)\n");
}

/// ``table``'s ``uid`` column: the name a row has on every computer a library
/// is shared with (``rowid`` is only this file's). The unique index refuses a
/// row copied with its uid, as a copy is a record of its own; the trigger
/// gives each new row one unless it came with one (a record arriving from
/// another computer), so no insert can forget. A column default would do
/// the trigger's work in a new table, but every library has the trigger, and
/// a library made now has to be the same as the rest.
void index_uid(Database& db, std::string_view table)
{
    const std::string t(table);
    db.exec("CREATE UNIQUE INDEX idx_" + t + "_uid ON " + t + "(uid)");
    db.exec("CREATE TRIGGER trg_" + t + "_uid AFTER INSERT ON " + t
            + " FOR EACH ROW WHEN NEW.uid IS NULL BEGIN UPDATE " + t + " SET uid = "
            + std::string(k_new_uid_sql) + " WHERE rowid = NEW.rowid; END");
}

/// Mark in ``sync_outbox`` the record every write to ``table`` touches: a
/// record of ``record_table``, named by ``key_of``, an SQL expression over a
/// row of ``table`` written with ``ROW.`` (the trigger's ``NEW.`` and
/// ``OLD.``). An update marks the record before and after, in case the write
/// moved a row from one to another; a key that is NULL marks nothing.
void mark_changes(Database& db, std::string_view table, std::string_view record_table,
                  std::string_view key_of)
{
    const auto key = [&](std::string_view row) {
        std::string out(key_of);
        for (std::size_t at = 0; (at = out.find("ROW.", at)) != std::string::npos;
             at += row.size()) {
            out.replace(at, 4, row);
        }
        return out;
    };
    // Not ``INSERT OR IGNORE``: a trigger takes the conflict rule of the
    // statement that fired it, so an upsert's write would fail on a record
    // already marked. A mark already there is left out instead.
    const std::string t(table);
    const std::string r(record_table);
    const std::string mark = "INSERT INTO sync_outbox (record_table, record_key) SELECT '" + r
                             + "', k FROM (";
    const std::string end = ") WHERE k IS NOT NULL AND NOT EXISTS (SELECT 1 FROM sync_outbox "
                            "WHERE record_table = '"
                            + r + "' AND record_key = k); END";
    db.exec("CREATE TRIGGER trg_" + t + "_sync_insert AFTER INSERT ON " + t
            + " FOR EACH ROW BEGIN " + mark + "SELECT " + key("NEW.") + " AS k" + end);
    db.exec("CREATE TRIGGER trg_" + t + "_sync_update AFTER UPDATE ON " + t
            + " FOR EACH ROW BEGIN " + mark + "SELECT " + key("OLD.") + " AS k UNION SELECT "
            + key("NEW.") + end);
    db.exec("CREATE TRIGGER trg_" + t + "_sync_delete AFTER DELETE ON " + t
            + " FOR EACH ROW BEGIN " + mark + "SELECT " + key("OLD.") + " AS k" + end);
}

/// The content: songs, services, outputs and settings, the media registries
/// and folders, Bibles, Freeform documents, style profiles and web pages.
void create_content_tables(Database& db)
{
    db.exec(R"SQL(
CREATE TABLE songs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    lyrics TEXT NOT NULL,
    verse_order TEXT,
    authors TEXT,
    songbook_name TEXT,
    songbook_entry TEXT,
    theme_map TEXT,
    copyright TEXT,
    ccli_song_number TEXT,
    show_copyright INTEGER DEFAULT 0,
    key TEXT,
    topics TEXT NOT NULL DEFAULT '[]',
    comments TEXT NOT NULL DEFAULT '',
    uid TEXT
)
)SQL");
    db.exec("CREATE INDEX idx_songs_title ON songs(title COLLATE NOCASE)");

    db.exec(R"SQL(
CREATE TABLE services (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    theme_map TEXT,
    sort_order INTEGER DEFAULT 0,
    created_at TEXT DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now','localtime')),
    service_at TEXT DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now','localtime')),
    tags TEXT NOT NULL DEFAULT '[]',
    uid TEXT,
    audio_fades TEXT NOT NULL DEFAULT '{}'
)
)SQL");

    db.exec(R"SQL(
CREATE TABLE service_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    service_id INTEGER NOT NULL,
    song_id INTEGER,
    order_num INTEGER NOT NULL,
    item_type TEXT DEFAULT 'song',
    data TEXT,
    uid TEXT,
    FOREIGN KEY(service_id) REFERENCES services(id) ON DELETE CASCADE,
    FOREIGN KEY(song_id) REFERENCES songs(id) ON DELETE SET NULL
)
)SQL");
    db.exec("CREATE INDEX idx_service_items_service ON service_items(service_id)");
    db.exec("CREATE INDEX idx_service_items_song ON service_items(song_id)");
    db.exec("CREATE INDEX idx_service_items_type ON service_items(item_type)");

    db.exec(R"SQL(
CREATE TABLE outputs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    order_num INTEGER NOT NULL DEFAULT 0,
    data TEXT NOT NULL
)
)SQL");

    db.exec(R"SQL(
CREATE TABLE app_settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
)
)SQL");

    // Media libraries: each kind's registry and folders.
    for (const std::string_view kind : {"image", "video", "audio"}) {
        create_media_registry(db, kind);
        create_media_folder_tables(db, kind);
    }

    // The library's fonts (FontRepository): one row per face, one file per row.
    db.exec(R"SQL(
CREATE TABLE font_files (
    filename TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    family TEXT NOT NULL,
    style TEXT NOT NULL,
    bold INTEGER NOT NULL DEFAULT 0,
    italic INTEGER NOT NULL DEFAULT 0,
    variable INTEGER NOT NULL DEFAULT 0,
    added_at INTEGER NOT NULL DEFAULT 0
)
)SQL");

    db.exec(R"SQL(
CREATE TABLE bibles (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    copyright TEXT,
    created_at TEXT DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now','localtime')),
    abbreviation TEXT NOT NULL DEFAULT '',
    enabled INTEGER NOT NULL DEFAULT 1,
    uid TEXT
)
)SQL");

    db.exec(R"SQL(
CREATE TABLE verses (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    bible_id INTEGER NOT NULL,
    book TEXT NOT NULL,
    chapter INTEGER NOT NULL,
    verse_num INTEGER NOT NULL,
    text TEXT NOT NULL,
    FOREIGN KEY(bible_id) REFERENCES bibles(id) ON DELETE CASCADE
)
)SQL");
    db.exec("CREATE UNIQUE INDEX idx_verses_lookup "
            "ON verses(bible_id, book, chapter, verse_num)");

    // Freeform library. ``document`` is the freeform::Document JSON.
    // Documents are rows of their own, so the folders have no items table.
    create_folder_table(db, "freeform");
    db.exec(R"SQL(
CREATE TABLE freeform_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    folder_id INTEGER,
    sort_order INTEGER DEFAULT 0,
    tags TEXT NOT NULL DEFAULT '[]',
    document TEXT NOT NULL DEFAULT '{}',
    thumb_output_id TEXT NOT NULL DEFAULT '',
    thumb_key TEXT NOT NULL DEFAULT '',
    uid TEXT,
    FOREIGN KEY(folder_id) REFERENCES freeform_folders(id) ON DELETE SET NULL
)
)SQL");
    db.exec("CREATE INDEX idx_freeform_items_folder ON freeform_items(folder_id)");

    db.exec(R"SQL(
CREATE TABLE style_profiles (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    sort_order INTEGER DEFAULT 0,
    data TEXT NOT NULL DEFAULT '{}',
    uid TEXT
)
)SQL");

    // The Browser tab's web pages (``model::WebPage``), rows of their own
    // like Freeform documents.
    create_folder_table(db, "web");
    db.exec(R"SQL(
CREATE TABLE web_pages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    url TEXT NOT NULL,
    width INTEGER NOT NULL DEFAULT 1920,
    height INTEGER NOT NULL DEFAULT 1080,
    zoom REAL NOT NULL DEFAULT 1.0,
    folder_id INTEGER,
    sort_order INTEGER DEFAULT 0,
    uid TEXT,
    FOREIGN KEY(folder_id) REFERENCES web_folders(id) ON DELETE SET NULL
)
)SQL");
    db.exec("CREATE INDEX idx_web_pages_folder ON web_pages(folder_id)");

    // Facts about the library itself, starting with its own uid.
    db.exec(R"SQL(
CREATE TABLE library_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
)
)SQL");
    db.exec("INSERT INTO library_meta (key, value) VALUES ('" + std::string(k_library_uid_key)
            + "', " + std::string(k_new_uid_sql) + ")");
}

/// This computer's bookkeeping for syncing (``sync_bookkeeping.hpp``) and the
/// sync outbox (``sync_outbox.hpp``). Every write to a table that syncs marks
/// the record it touched, whether or not the library syncs: a record is
/// marked once however often it changes, so the outbox is never larger than
/// the library.
void create_sync_tables(Database& db)
{
    db.exec(R"SQL(
CREATE TABLE sync_outbox (
    record_table TEXT NOT NULL,
    record_key TEXT NOT NULL,
    PRIMARY KEY (record_table, record_key)
) WITHOUT ROWID
)SQL");
    db.exec(R"SQL(
CREATE TABLE sync_shared (
    record_table TEXT NOT NULL,
    record_key TEXT NOT NULL,
    document TEXT,
    versions TEXT NOT NULL,
    id_here INTEGER,
    PRIMARY KEY (record_table, record_key)
) WITHOUT ROWID
)SQL");
    db.exec(R"SQL(
CREATE TABLE sync_applied (
    machine_id TEXT PRIMARY KEY,
    sequence INTEGER NOT NULL
)
)SQL");
    db.exec(R"SQL(
CREATE TABLE sync_state (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
)
)SQL");
    // Changes from other computers that wait for what they need: a service
    // item whose service, or a song a service item names, has not arrived.
    db.exec(R"SQL(
CREATE TABLE sync_held (
    record_table TEXT NOT NULL,
    record_key TEXT NOT NULL,
    machine_id TEXT NOT NULL,
    sequence INTEGER NOT NULL,
    change TEXT NOT NULL,
    PRIMARY KEY (record_table, record_key, machine_id, sequence)
) WITHOUT ROWID
)SQL");
    // Each media file as the change (or snapshot) that brought it declared
    // it: its size, its root in ``sha256`` (the SHA-256 of its pieces'
    // hashes, sync/media_pieces.hpp), and the version of the change that
    // first declared it (the order the size limit takes files in).
    db.exec(R"SQL(
CREATE TABLE sync_media (
    path TEXT PRIMARY KEY,
    size INTEGER NOT NULL,
    sha256 TEXT NOT NULL,
    counter INTEGER NOT NULL,
    machine TEXT NOT NULL
) WITHOUT ROWID
)SQL");

    // Every table that syncs record by record has uids (see sync_tables.hpp
    // for how each table syncs).
    for (const std::string_view table :
         {"songs", "services", "service_items", "freeform_folders", "freeform_items",
          "image_folders", "video_folders", "audio_folders", "bibles", "style_profiles",
          "web_folders", "web_pages"}) {
        index_uid(db, table);
        mark_changes(db, table, table, "ROW.uid");
    }
    mark_changes(db, "library_meta", "library_meta", "ROW.key");
    mark_changes(db, "app_settings", "app_settings", "ROW.key");
    for (const std::string_view kind : {"image", "video", "audio"}) {
        const std::string k(kind);
        mark_changes(db, k + "_files", k + "_files", "ROW.filename");
        // Named by the folder's uid. A folder deleted takes its items with it,
        // and is gone by the time their triggers run: its own mark covers them.
        mark_changes(db, k + "_folder_items", k + "_folder_items",
                     "(SELECT uid FROM " + k + "_folders WHERE id = ROW.folder_id) || '/' || "
                     "ROW.filename");
    }
    mark_changes(db, "font_files", "font_files", "ROW.filename");
    // Every save rewrites all the outputs, so the mark is for the whole table.
    mark_changes(db, "outputs", "outputs", "''");
    // A verse is part of its Bible. As with folder items, a Bible deleted is
    // gone before its verses' triggers run, and its own mark covers them.
    mark_changes(db, "verses", "bibles", "(SELECT uid FROM bibles WHERE id = ROW.bible_id)");
}

[[nodiscard]] bool has_tables(Database& db)
{
    return db.query_int("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table'", 0) > 0;
}

}  // namespace

int schema_version(Database& db)
{
    return db.query_int("PRAGMA user_version", 0);
}

std::string library_uid(Database& db)
{
    auto q = exec(db, QStringLiteral("SELECT value FROM library_meta WHERE key = ?"),
                  {sql_text(k_library_uid_key)}, "read the library's uid");
    if (!q.next()) {
        throw std::runtime_error("This library has no uid");
    }
    return q.value(0).toString().toStdString();
}

void create_schema(Database& db)
{
    Transaction tx(db);
    create_content_tables(db);
    create_sync_tables(db);
    // PRAGMA takes no bound parameters; the value is a constant.
    db.exec("PRAGMA user_version = " + std::to_string(k_schema_version));
    tx.commit();
    db.forget_columns();
}

bool ensure_schema(Database& db)
{
    const int version = schema_version(db);
    if (version == k_schema_version) {
        return false;
    }
    if (version > k_schema_version) {
        throw std::runtime_error("This library was made by a newer version of SeventhSlide "
                                 "(schema "
                                 + std::to_string(version) + ", this build reads "
                                 + std::to_string(k_schema_version) + ").");
    }
    if (version >= 1 || has_tables(db)) {
        throw std::runtime_error("This file is not a SeventhSlide library, or was made by a "
                                 "pre-release build this version cannot read.");
    }
    create_schema(db);
    return true;
}

}  // namespace seventhslide::db
