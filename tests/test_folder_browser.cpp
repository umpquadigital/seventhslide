// FolderBrowser: the folder navigation, search and folder edits the image,
// video and Freeform browsers share. The controllers' own tests cover what a
// folder holds; these pin the shared half once.

#include "test_qt_app.hpp"
#include "refusals.hpp"

#include <catch2/catch_test_macros.hpp>

#include "folder_browser.hpp"

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/schema.hpp>

#include <QVariantList>
#include <QVariantMap>

#include <optional>

using seventhslide::app::FolderBrowser;
using seventhslide::app::MediaBrowserEntry;
using seventhslide::db::Database;
using seventhslide::db::FreeformRepository;
namespace refusal = seventhslide::app::refusal;

namespace {

Database open_library_db()
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

}  // namespace

TEST_CASE("FolderBrowser walks into folders and back up", "[app][folder-browser]")
{
    Database db = open_library_db();
    FreeformRepository repo(db);
    const auto outer = repo.folders().create("Outer");
    const auto inner = repo.folders().create("Inner", outer);

    FolderBrowser browser;
    REQUIRE_FALSE(browser.enter(outer));  // nothing to browse yet
    browser.attach(repo.folders());
    REQUIRE_FALSE(browser.folder());
    REQUIRE(browser.parent_id() == -1);
    REQUIRE_FALSE(browser.up());

    REQUIRE(browser.enter(inner));
    REQUIRE(browser.folder() == inner);
    REQUIRE(browser.parent_id() == outer);
    REQUIRE_FALSE(browser.enter(inner + 100));
    REQUIRE(browser.folder() == inner);

    REQUIRE(browser.up());
    REQUIRE(browser.folder() == outer);
    REQUIRE(browser.up());
    REQUIRE_FALSE(browser.folder());

    REQUIRE(browser.enter(outer));
    REQUIRE(browser.enter(-1));
    REQUIRE_FALSE(browser.folder());
}

TEST_CASE("FolderBrowser falls back to the root when its folder goes", "[app][folder-browser]")
{
    Database db = open_library_db();
    FreeformRepository repo(db);
    const auto outer = repo.folders().create("Outer");
    const auto inner = repo.folders().create("Inner", outer);

    FolderBrowser browser;
    browser.attach(repo.folders());
    REQUIRE(browser.enter(inner));
    browser.drop_missing();
    REQUIRE(browser.folder() == inner);

    // Deleting an ancestor takes the folder shown with it.
    (void)repo.delete_folder_tree(outer);
    browser.drop_missing();
    REQUIRE_FALSE(browser.folder());
}

TEST_CASE("FolderBrowser search and the browser's leading rows", "[app][folder-browser]")
{
    Database db = open_library_db();
    FreeformRepository repo(db);
    const auto outer = repo.folders().create("Outer");
    const auto inner = repo.folders().create("Inner", outer);

    FolderBrowser browser;
    browser.attach(repo.folders());
    REQUIRE(browser.set_search(QStringLiteral("  inn ")));
    REQUIRE_FALSE(browser.set_search(QStringLiteral("  inn ")));
    REQUIRE(browser.query() == QStringLiteral("inn"));

    REQUIRE(browser.enter(outer));
    const auto children = browser.children();
    REQUIRE(children.size() == 1);

    // Searching: no "..", it is not a match.
    auto rows = browser.folder_entries(children, {{inner, 3}});
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].kind == MediaBrowserEntry::Kind::Folder);
    REQUIRE(rows[0].id == inner);
    REQUIRE(rows[0].name == "Inner");
    REQUIRE(rows[0].count == 3);

    REQUIRE(browser.set_search(QString()));
    rows = browser.folder_entries(children, {});
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].kind == MediaBrowserEntry::Kind::Parent);
    REQUIRE(rows[0].name == "..");
    REQUIRE(rows[1].count == 0);

    // A new tree (another library) starts over.
    browser.attach(repo.folders());
    REQUIRE_FALSE(browser.folder());
    REQUIRE(browser.search().isEmpty());
}

TEST_CASE("FolderBrowser folder edits", "[app][folder-browser]")
{
    Database db = open_library_db();
    FreeformRepository repo(db);

    FolderBrowser browser;
    REQUIRE(browser.create(QStringLiteral("Early")) == -1);
    REQUIRE(browser.rename(1, QStringLiteral("Early")) == refusal::no_library());
    REQUIRE(browser.rows().isEmpty());
    browser.attach(repo.folders());

    // New folders go in the folder shown.
    REQUIRE(browser.create(QStringLiteral("   ")) == -1);
    const auto songs = browser.create(QStringLiteral(" Songs "));
    REQUIRE(songs >= 0);
    REQUIRE(browser.enter(songs));
    const auto hymns = browser.create(QStringLiteral("Hymns"));
    REQUIRE(repo.folders().get(hymns)->parent_id == songs);
    REQUIRE(repo.folders().get(songs)->name == "Songs");

    REQUIRE(browser.rename(hymns, QStringLiteral(" ")) == refusal::name_required());
    REQUIRE(browser.rename(-1, QStringLiteral("X")) == refusal::folder_gone());
    REQUIRE(browser.rename(hymns, QStringLiteral(" Old Hymns ")).isEmpty());
    REQUIRE(repo.folders().get(hymns)->name == "Old Hymns");

    REQUIRE_FALSE(browser.move(songs, hymns));  // into itself
    REQUIRE_FALSE(browser.move(-1, songs));
    REQUIRE(browser.move(hymns, -1));
    REQUIRE_FALSE(repo.folders().get(hymns)->parent_id);

    REQUIRE_FALSE(browser.reorder(-1, QVariantList{QStringLiteral("not an id")}));
    REQUIRE(browser.reorder(-1, QVariantList{hymns, songs}));
    const auto top = repo.folders().children(std::nullopt);
    REQUIRE(top.size() == 2);
    REQUIRE(top[0].id == hymns);

    const auto rows = browser.rows();
    REQUIRE(rows.size() == 2);
    const auto first = rows[0].toMap();
    REQUIRE(first.value(QStringLiteral("id")).toLongLong() == hymns);
    REQUIRE(first.value(QStringLiteral("path")).toString() == QStringLiteral("Old Hymns"));

    browser.detach();
    REQUIRE_FALSE(browser.folder());
    REQUIRE_FALSE(browser.move(hymns, songs));
}
