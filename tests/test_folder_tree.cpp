#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/folder_tree.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/model/library.hpp>

#include "test_qt_app.hpp"

using seventhslide::db::Database;
using seventhslide::db::FolderTree;
using seventhslide::model::Folder;
using seventhslide::model::folder_paths;

namespace {

Database library_db()
{
    seventhslide::test::ensure_qt_application();
    Database db = Database::open_memory();
    seventhslide::db::ensure_schema(db);
    return db;
}

}  // namespace

TEST_CASE("A folder cannot move into itself or its own subtree", "[db][folders]")
{
    Database db = library_db();
    FolderTree tree(db, "freeform_folders");
    const auto top = tree.create("Top", std::nullopt);
    const auto mid = tree.create("Mid", top);
    const auto leaf = tree.create("Leaf", mid);

    REQUIRE_FALSE(tree.move(top, top));
    REQUIRE_FALSE(tree.move(top, leaf));
    REQUIRE(tree.children(std::nullopt).size() == 1);

    // A legal move lands at the end of its new parent's children.
    const auto other = tree.create("Other", std::nullopt);
    REQUIRE(tree.move(leaf, other));
    REQUIRE(tree.children(other).front().id == leaf);
    REQUIRE(tree.subtree_ids(top).size() == 2);
}

TEST_CASE("Folder changes report a folder that does not exist", "[db][folders]")
{
    Database db = library_db();
    FolderTree tree(db, "image_folders");
    REQUIRE_FALSE(tree.rename(999, "Nope"));
    REQUIRE_FALSE(tree.move(999, std::nullopt));
    REQUIRE_FALSE(tree.remove(999));
    // A missing parent is refused rather than tripping the foreign key.
    const auto real = tree.create("Real", std::nullopt);
    REQUIRE_FALSE(tree.move(real, 999));
    REQUIRE_FALSE(tree.get(real)->parent_id);
}

TEST_CASE("Reordering folders touches only that parent's children", "[db][folders]")
{
    Database db = library_db();
    FolderTree tree(db, "video_folders");
    const auto a = tree.create("A", std::nullopt);
    const auto b = tree.create("B", std::nullopt);
    const auto inner = tree.create("Inner", a);

    tree.reorder_children(std::nullopt, {b, a, inner});
    const auto roots = tree.children(std::nullopt);
    REQUIRE(roots.size() == 2);
    REQUIRE(roots[0].id == b);
    REQUIRE(roots[1].id == a);
    // ``inner`` is not a root folder, so its place under ``a`` is unchanged.
    REQUIRE(tree.children(a).front().sort_order == 0);

    REQUIRE(tree.remove(a));
    REQUIRE(tree.list().size() == 1);  // ``inner`` went with its parent
}

TEST_CASE("Folder paths read from the root and sort by path", "[model][folders]")
{
    const std::vector<Folder> folders{
        {.id = 1, .name = "Songs", .sort_order = 0, .parent_id = std::nullopt},
        {.id = 2, .name = "Easter", .sort_order = 0, .parent_id = 1},
        {.id = 3, .name = "Announcements", .sort_order = 1, .parent_id = std::nullopt},
        // A hand-edited cycle must not hang the walk.
        {.id = 4, .name = "Loop", .sort_order = 0, .parent_id = 5},
        {.id = 5, .name = "Back", .sort_order = 0, .parent_id = 4},
    };
    const auto paths = folder_paths(folders);
    REQUIRE(paths.size() == 5);
    REQUIRE(paths[0].path == "Announcements");
    REQUIRE(paths[3].path == "Songs");
    REQUIRE(paths[4].path == "Songs / Easter");
    REQUIRE(paths[4].id == 2);
}
