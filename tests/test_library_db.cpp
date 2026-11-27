#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/db/style_profile_repository.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/parse/bible.hpp>

#include "test_qt_app.hpp"

#include <QSqlQuery>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using seventhslide::db::FreeformRepository;
using seventhslide::db::BibleRepository;
using seventhslide::db::Database;
using seventhslide::db::MediaKind;
using seventhslide::db::MediaRepository;
using seventhslide::db::ServiceRepository;
using seventhslide::db::StyleProfileRepository;
using seventhslide::freeform::document_is_native;
using seventhslide::freeform::make_document;
using seventhslide::model::FreeformItem;
using seventhslide::model::BibleVerse;
using seventhslide::model::StyleProfile;

namespace {

void ensure_qcore_application()
{
    seventhslide::test::ensure_qt_application();
}

Database open_library_db()
{
    ensure_qcore_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

int table_exists(Database& db, const char* name)
{
    const std::string sql =
        std::string("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='")
        + name + "'";
    return db.query_int(sql, 0);
}

}  // namespace

TEST_CASE("A table's columns are read once, and again once the schema is made", "[db][library]")
{
    ensure_qcore_application();
    Database db = Database::open_memory();
    // Asked before the schema is there: none, and not kept that way.
    CHECK(db.table_columns("songs").empty());
    seventhslide::db::ensure_schema(db);
    const auto& songs = db.table_columns("songs");
    REQUIRE_FALSE(songs.empty());
    CHECK(songs.front().name == "id");
    CHECK(songs.front().primary);
    CHECK(&db.table_columns("songs") == &songs);
}

TEST_CASE("initial schema creates media bible freeform and style tables", "[db][library]")
{
    Database db = open_library_db();
    REQUIRE(db.query_int(
                "SELECT COUNT(*) FROM pragma_table_info('freeform_items') WHERE name='document'", 0)
            == 1);
    REQUIRE(db.query_int("SELECT COUNT(*) FROM pragma_table_info('freeform_items') "
                         "WHERE name IN ('fields', 'theme_map')",
                         0)
            == 0);
    REQUIRE(table_exists(db, "image_files") == 1);
    REQUIRE(table_exists(db, "image_folders") == 1);
    REQUIRE(table_exists(db, "video_folders") == 1);
    REQUIRE(table_exists(db, "audio_folders") == 1);
    REQUIRE(table_exists(db, "audio_folder_items") == 1);
    REQUIRE(table_exists(db, "bibles") == 1);
    REQUIRE(table_exists(db, "verses") == 1);
    REQUIRE(table_exists(db, "ann_layouts") == 0);
    REQUIRE(table_exists(db, "freeform_folders") == 1);
    REQUIRE(table_exists(db, "freeform_items") == 1);
    REQUIRE(table_exists(db, "style_profiles") == 1);
}

TEST_CASE("MediaRepository image upsert preserves library_visible", "[db][library][media]")
{
    Database db = open_library_db();
    MediaRepository media(db);

    media.upsert_file(seventhslide::db::MediaKind::Image, "abc.jpg", "Alpha");
    auto f = media.get_file(seventhslide::db::MediaKind::Image, "abc.jpg");
    REQUIRE(f);
    REQUIRE(f->display_name == "Alpha");
    REQUIRE(f->library_visible);

    media.set_file_visible(seventhslide::db::MediaKind::Image, "abc.jpg", false);
    media.upsert_file(seventhslide::db::MediaKind::Image, "abc.jpg", "Alpha Renamed");
    f = media.get_file(seventhslide::db::MediaKind::Image, "abc.jpg");
    REQUIRE(f->display_name == "Alpha Renamed");
    REQUIRE_FALSE(f->library_visible);

    REQUIRE(media.list_files(seventhslide::db::MediaKind::Image, true).empty());
    REQUIRE(media.list_files(seventhslide::db::MediaKind::Image, false).size() == 1);
}

TEST_CASE("MediaRepository folders and service image refs", "[db][library][media]")
{
    Database db = open_library_db();
    MediaRepository media(db);
    ServiceRepository services(db);

    const auto root = media.folders(MediaKind::Image).create("Slides");
    const auto child = media.folders(MediaKind::Image).create("Nested", root);
    REQUIRE_FALSE(media.folders(MediaKind::Image).move(root, child)); // cycle

    media.upsert_file(seventhslide::db::MediaKind::Image, "pic.png", "Pic");
    REQUIRE(media.add_folder_item(MediaKind::Image, child, "pic.png") > 0);
    REQUIRE(media.list_folder_items(MediaKind::Image, child).size() == 1);
    REQUIRE(media.folders(MediaKind::Image).children(root).size() == 1);
    REQUIRE(media.list_linked_filenames(MediaKind::Image).count("pic.png") == 1);
    media.move_filename_to_folder(MediaKind::Image, "pic.png", root);
    REQUIRE(media.list_folder_items(MediaKind::Image, root).size() == 1);
    REQUIRE(media.list_folder_items(MediaKind::Image, child).empty());
    media.move_filename_to_folder(MediaKind::Image, "pic.png", child);

    const auto sid = services.create_service("S");
    REQUIRE(services.add_item(sid, "image", {{"filename", "pic.png"}}) > 0);
    REQUIRE(media.referenced_images({"pic.png"}).contains("pic.png"));

    const auto files = media.delete_folder_tree(MediaKind::Image, root);
    REQUIRE(files.size() == 1);
    REQUIRE(media.folders(MediaKind::Image).list().empty());
}

namespace {

/// A library with ``n`` service items and ``n`` Freeform items that show
/// ``a.png``.
void add_image_users(Database& db, int n)
{
    ServiceRepository services(db);
    FreeformRepository freeforms(db);
    const auto sid = services.create_service("S");
    for (int i = 0; i < n; ++i) {
        (void)services.add_item(sid, "image", {{"filename", "a.png"}});
        auto doc = make_document("Promo", {"Main"});
        seventhslide::freeform::Object img;
        img.id = seventhslide::freeform::alloc_id(doc, 'o');
        img.type = seventhslide::freeform::ObjectType::Image;
        img.filename = "a.png";
        doc.decks.front().slides.front().objects.push_back(img);
        FreeformItem item;
        item.name = "Promo";
        item.document = doc.to_json();
        (void)freeforms.create_item(item);
    }
}

/// How many rows name ``a.png`` (service items and Freeform documents).
int rows_naming_a_png(Database& db)
{
    return db.query_int("SELECT (SELECT COUNT(*) FROM service_items WHERE data LIKE '%a.png%')"
                        " + (SELECT COUNT(*) FROM freeform_items WHERE document LIKE '%a.png%')",
                        -1);
}

/// Make every write to a Freeform item fail, as a full disk would.
void refuse_freeform_writes(Database& db)
{
    QSqlQuery q(db.handle());
    REQUIRE(q.exec("CREATE TEMP TRIGGER refuse BEFORE UPDATE ON freeform_items "
                   "BEGIN SELECT RAISE(ABORT, 'refused'); END"));
}

}  // namespace

TEST_CASE("retarget_file prepares each statement once, however many rows change",
          "[db][library][media]")
{
    const auto prepared_for = [](int rows) {
        Database db = open_library_db();
        add_image_users(db, rows);
        MediaRepository media(db);
        const auto before = db.statements_prepared();
        CHECK(media.retarget_file(MediaKind::Image, "a.png", "b.png")
              == static_cast<std::size_t>(2 * rows));
        CHECK(rows_naming_a_png(db) == 0);
        return db.statements_prepared() - before;
    };
    CHECK(prepared_for(1) == prepared_for(6));
}

TEST_CASE("retarget_file changes everything or nothing", "[db][library][media]")
{
    SECTION("on its own")
    {
        Database db = open_library_db();
        add_image_users(db, 2);
        refuse_freeform_writes(db);
        MediaRepository media(db);
        // The service items are written before the Freeform items fail.
        REQUIRE_THROWS(media.retarget_file(MediaKind::Image, "a.png", "b.png"));
        CHECK(db.transaction_depth() == 0);
        CHECK(rows_naming_a_png(db) == 4);
    }
    SECTION("inside the caller's transaction, which goes on")
    {
        Database db = open_library_db();
        add_image_users(db, 2);
        refuse_freeform_writes(db);
        MediaRepository media(db);
        {
            seventhslide::db::Transaction tx(db);
            media.upsert_file(MediaKind::Image, "b.png", "B");
            REQUIRE_THROWS(media.retarget_file(MediaKind::Image, "a.png", "b.png"));
            CHECK(db.transaction_depth() == 1);
            tx.commit();
        }
        CHECK(rows_naming_a_png(db) == 4);
        CHECK(db.query_int("SELECT COUNT(*) FROM image_files WHERE filename = 'b.png'", 0) == 1);
    }
    SECTION("inside the caller's transaction, which it commits or rolls back with")
    {
        Database db = open_library_db();
        add_image_users(db, 2);
        MediaRepository media(db);
        {
            seventhslide::db::Transaction tx(db);
            CHECK(media.retarget_file(MediaKind::Image, "a.png", "b.png") == 4);
            CHECK(db.transaction_depth() == 1);
        }
        CHECK(rows_naming_a_png(db) == 4);
        {
            seventhslide::db::Transaction tx(db);
            CHECK(media.retarget_file(MediaKind::Image, "a.png", "b.png") == 4);
            tx.commit();
        }
        CHECK(rows_naming_a_png(db) == 0);
    }
}

TEST_CASE("BibleRepository import and lookup", "[db][library][bible]")
{
    Database db = open_library_db();
    BibleRepository bibles(db);

    std::vector<BibleVerse> verses = {
        {.book = "John", .chapter = 3, .verse_num = 16, .text = "For God so loved…"},
        {.book = "John", .chapter = 3, .verse_num = 17, .text = "For God sent…"},
        {.book = "Genesis", .chapter = 1, .verse_num = 1, .text = "In the beginning…"},
    };
    const auto id = bibles.import_bible("KJV", "Public domain", verses);
    REQUIRE(id > 0);

    auto books = bibles.list_books(id);
    REQUIRE(books.size() == 2);
    REQUIRE(books[0] == "John"); // import order via MIN(id)
    REQUIRE(books[1] == "Genesis");

    auto ch = bibles.get_chapter(id, "John", 3);
    REQUIRE(ch.size() == 2);
    REQUIRE(ch[0].verse_num == 16);

    REQUIRE(bibles.remove(id));
    REQUIRE(bibles.get_chapter(id, "John", 3).empty());
}

TEST_CASE("A Bible too large to sync is refused at import, adding nothing", "[db][library][bible]")
{
    Database db = open_library_db();
    BibleRepository bibles(db);
    // Some 16 MB of verses: larger than any record syncs.
    std::vector<BibleVerse> verses;
    for (int i = 0; i < 40000; ++i) {
        verses.push_back({.book = "Psalms", .chapter = i / 100 + 1, .verse_num = i % 100 + 1,
                          .text = std::string(400, 'a')});
    }
    try {
        (void)bibles.import_bible("Long Study Bible", "", verses);
        FAIL("imported");
    } catch (const std::runtime_error& ex) {
        CHECK_THAT(ex.what(), Catch::Matchers::Matches(
                                  R"(This Bible is too large to sync \([0-9]+ MB; the most is 12 MB\)\.)"));
    }
    CHECK(bibles.list_all().empty());
    CHECK(db.query_int("SELECT COUNT(*) FROM verses", -1) == 0);

    // One a little smaller goes in.
    verses.resize(25000);
    CHECK(bibles.import_bible("Shorter Bible", "", verses) > 0);
}

TEST_CASE("A Bible keeps its abbreviation and whether it is offered", "[db][library][bible]")
{
    Database db = open_library_db();
    BibleRepository bibles(db);
    const auto id = bibles.import_bible("King James Version", "", {});

    auto fresh = bibles.get(id);
    REQUIRE(fresh);
    REQUIRE(fresh->abbreviation.empty());
    REQUIRE(fresh->enabled);

    REQUIRE(bibles.set_abbreviation(id, "  KJV\n 1611 extra words "));
    REQUIRE(bibles.set_enabled(id, false));
    const auto all = bibles.list_all();
    REQUIRE(all.size() == 1);
    REQUIRE(all[0].abbreviation == "KJV 1611 ext");
    REQUIRE_FALSE(all[0].enabled);

    REQUIRE_FALSE(bibles.set_abbreviation(id + 1, "NIV"));
    REQUIRE_FALSE(bibles.set_enabled(id + 1, true));
}

// list_books is kept per bible after the first read; these are the ways a kept
// list could go stale.
TEST_CASE("BibleRepository book lists follow imports and removals", "[db][library][bible]")
{
    Database db = open_library_db();
    BibleRepository bibles(db);
    const auto kjv = bibles.import_bible(
        "KJV", "", {{.book = "John", .chapter = 3, .verse_num = 16, .text = "For God"},
                    {.book = "Genesis", .chapter = 1, .verse_num = 1, .text = "In the"}});
    const std::vector<std::string> kjv_books{"John", "Genesis"};
    REQUIRE(bibles.list_books(kjv) == kjv_books);
    REQUIRE(bibles.list_books(kjv) == kjv_books);

    // Asked about before it exists (a remote with a stale id), then imported.
    const auto next = kjv + 1;
    REQUIRE(bibles.list_books(next).empty());
    const auto web = bibles.import_bible(
        "WEB", "", {{.book = "Ruth", .chapter = 1, .verse_num = 1, .text = "In the days"}});
    REQUIRE(web == next);
    REQUIRE(bibles.list_books(web) == std::vector<std::string>{"Ruth"});

    REQUIRE(bibles.remove(kjv));
    REQUIRE(bibles.list_books(kjv).empty());
    REQUIRE(bibles.list_books(web) == std::vector<std::string>{"Ruth"});
}

TEST_CASE("A Bible that numbers a verse twice imports", "[db][library][bible]")
{
    // The schema allows one row per verse; the parser folds a repeated number
    // into one verse, so a real-world file with that quirk still imports.
    Database db = open_library_db();
    BibleRepository bibles(db);
    const auto parsed = seventhslide::parse::parse_bible_file(
        std::string(SEVENTHSLIDE_FIXTURES_DIR) + "/bibles/duplicate_verse.xml");
    const auto id = bibles.import_bible(parsed.name, parsed.copyright, parsed.verses);
    const auto chapter = bibles.get_chapter(id, "1 Corinthians", 10);
    REQUIRE(chapter.size() == 3);
    REQUIRE(chapter[1].verse_num == 29);
    REQUIRE(chapter[1].text.find("Conscience") != std::string::npos);
}

TEST_CASE("FreeformRepository stores documents and deletes folder trees", "[db][library][freeform]")
{
    Database db = open_library_db();
    FreeformRepository repo(db);

    const auto folder = repo.folders().create("General");
    FreeformItem item;
    item.name = "Welcome Slide";
    item.folder_id = folder;
    item.tags = {"service"};
    item.document = make_document("Welcome Slide", {"Main"}).to_json();
    const auto iid = repo.create_item(item);
    REQUIRE(iid > 0);

    auto stored = repo.get_item(iid);
    REQUIRE(stored);
    REQUIRE(document_is_native(stored->document));
    REQUIRE(stored->document == item.document);
    REQUIRE(stored->tags == std::vector<std::string>{"service"});
    REQUIRE(stored->thumb_output_id == "Main");
    REQUIRE_FALSE(stored->thumb_key.empty());
    const auto rows = repo.list_browser_rows(folder);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].id == iid);
    REQUIRE(rows[0].thumb_key == stored->thumb_key);
    REQUIRE(repo.folder_item_counts().at(folder) == 1);

    stored->name = "Renamed";
    repo.update_item(*stored);
    REQUIRE(repo.get_item(iid)->name == "Renamed");

    REQUIRE(repo.delete_folder_tree(folder).size() == 1);
    auto gone = repo.get_item(iid);
    REQUIRE_FALSE(gone);
}

TEST_CASE("FreeformRepository child folders move reorder and list by folder", "[db][library][freeform]")
{
    Database db = open_library_db();
    FreeformRepository repo(db);

    const auto root = repo.folders().create("Shows");
    const auto nested = repo.folders().create("Nested", root);
    REQUIRE(repo.folders().children(std::nullopt).size() == 1);
    REQUIRE(repo.folders().children(root).size() == 1);
    REQUIRE_FALSE(repo.folders().move(root, nested));

    FreeformItem a;
    a.name = "Root item";
    const auto a_id = repo.create_item(a);
    FreeformItem b;
    b.name = "Nested item";
    b.folder_id = nested;
    const auto b_id = repo.create_item(b);

    REQUIRE(repo.list_items_in_folder(std::nullopt).size() == 1);
    REQUIRE(repo.list_items_in_folder(nested).size() == 1);

    REQUIRE(repo.move_item(a_id, nested));
    REQUIRE(repo.list_items_in_folder(std::nullopt).empty());
    REQUIRE(repo.list_items_in_folder(nested).size() == 2);

    repo.reorder_items(nested, {b_id, a_id});
    const auto ordered = repo.list_items_in_folder(nested);
    REQUIRE(ordered.size() == 2);
    REQUIRE(*ordered[0].id == b_id);
    REQUIRE(*ordered[1].id == a_id);

    const auto extra = repo.folders().create("Extra");
    repo.folders().reorder_children(std::nullopt, {extra, root});
    const auto top = repo.folders().children(std::nullopt);
    REQUIRE(top.size() == 2);
    REQUIRE(*top[0].id == extra);
    REQUIRE(*top[1].id == root);
}

TEST_CASE("StyleProfileRepository CRUD", "[db][library][style]")
{
    Database db = open_library_db();
    StyleProfileRepository profiles(db);

    auto data = StyleProfile::empty_data();
    data["songs"]["1"] = {{"Main", {{"song", {{"theme", "t1"}}}}}};
    const auto id = profiles.create("Default", data);
    REQUIRE(id > 0);

    auto loaded = profiles.get(id);
    REQUIRE(loaded);
    REQUIRE(loaded->name == "Default");
    REQUIRE(loaded->data["songs"]["1"]["Main"]["song"]["theme"] == "t1");

    profiles.rename(id, "Sunday");
    auto summaries = profiles.list_summaries();
    REQUIRE(summaries.size() == 1);
    REQUIRE(summaries[0].name == "Sunday");
    REQUIRE(summaries[0].data.is_object());
    REQUIRE(summaries[0].data.empty());

    REQUIRE(profiles.remove(id));
    REQUIRE_FALSE(profiles.get(id));
}
