#include <catch2/catch_test_macros.hpp>

#include "test_temp_dir.hpp"

#include <seventhslide/library/library.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

using seventhslide::library::LibraryRegistry;
using seventhslide::library::database_file;
using seventhslide::library::k_default_library_id;
using seventhslide::library::media_root;
using seventhslide::library::slugify;

using seventhslide::test::TempDir;


TEST_CASE("Library layout has one spelling", "[library]")
{
    const fs::path root{"/srv/worship"};
    REQUIRE(database_file(root) == root / "seventhslide.db");
    REQUIRE(media_root(root) == root / "media");
}

TEST_CASE("slugify makes filesystem-safe ids", "[library]")
{
    REQUIRE(slugify("Second Campus") == "second-campus");
    REQUIRE(slugify("  Spanish / Español!  ") == "spanish-espa-ol");
    REQUIRE(slugify("---") .empty());
    REQUIRE(slugify("Kids").size() == 4);
    // Long names are truncated rather than rejected.
    REQUIRE(slugify(std::string(200, 'a')).size() == 48);
}

TEST_CASE("ensure_default creates the first library in its own folder", "[library]")
{
    TempDir tmp;
    LibraryRegistry registry(tmp.path);
    registry.load();
    REQUIRE(registry.entries().empty());
    // No file is a first run, not a refusal.
    REQUIRE(registry.load_error().empty());

    registry.ensure_default();
    REQUIRE(registry.entries().size() == 1);
    REQUIRE(registry.active_id() == std::string{k_default_library_id});
    REQUIRE(registry.active() != nullptr);
    // Never the data dir itself: that folder is shared with the retired
    // Electron app, whose media must not show up in this library.
    REQUIRE(registry.active()->root == tmp.path / "libraries" / "default");
    REQUIRE(fs::exists(registry.registry_file()));

    LibraryRegistry reloaded(tmp.path);
    reloaded.load();
    REQUIRE(reloaded.entries().size() == 1);
    REQUIRE(reloaded.active_id() == std::string{k_default_library_id});
    REQUIRE(reloaded.active()->root == tmp.path / "libraries" / "default");
}

TEST_CASE("A library owns only a folder the app made for it", "[library]")
{
    TempDir tmp;
    LibraryRegistry registry(tmp.path);
    registry.load();
    registry.ensure_default();

    const auto made = registry.create("Kids");
    REQUIRE(made.ok());
    CHECK(registry.owns_folder(made.id));
    CHECK_FALSE(registry.owns_folder("nobody"));

    // Added from elsewhere: whatever else is in the folder is not the app's.
    const auto elsewhere = registry.attach(tmp.path / "Downloads" / "Songs", "Songs");
    REQUIRE(elsewhere.ok());
    CHECK_FALSE(registry.owns_folder(elsewhere.id));

    // Deeper in the libraries folder, or the data folder itself (as an older
    // install's library is), with the computer's key and every other library.
    const auto deeper = registry.attach(tmp.path / "libraries" / "kids" / "older", "Older");
    REQUIRE(deeper.ok());
    CHECK_FALSE(registry.owns_folder(deeper.id));
    const auto data = registry.attach(tmp.path, "Data");
    REQUIRE(data.ok());
    CHECK_FALSE(registry.owns_folder(data.id));

    // Another library inside it: deleting it would take that one too.
    CHECK_FALSE(registry.owns_folder(made.id));
    REQUIRE(registry.remove(deeper.id).ok());
    CHECK(registry.owns_folder(made.id));

    // A link in the libraries folder, to a folder elsewhere.
    // (Windows makes none without Developer Mode or an administrator.)
    std::error_code ec;
    fs::create_directories(tmp.path / "Elsewhere");
    fs::create_directory_symlink(tmp.path / "Elsewhere", tmp.path / "libraries" / "linked", ec);
    if (ec) {
        return;
    }
    const auto linked = registry.attach(tmp.path / "libraries" / "linked", "Linked");
    REQUIRE(linked.ok());
    CHECK_FALSE(registry.owns_folder(linked.id));
}

TEST_CASE("create registers a directory under the data dir", "[library]")
{
    TempDir tmp;
    LibraryRegistry registry(tmp.path);
    registry.load();
    registry.ensure_default();

    const auto made = registry.create("Second Campus");
    REQUIRE(made.ok());
    REQUIRE(made.id == "second-campus");
    REQUIRE(registry.entries().size() == 2);
    // Switching is a separate decision from creating.
    REQUIRE(registry.active_id() == std::string{k_default_library_id});

    const auto* entry = registry.find("second-campus");
    REQUIRE(entry != nullptr);
    REQUIRE(entry->root == tmp.path / "libraries" / "second-campus");
    REQUIRE(fs::is_directory(media_root(entry->root)));

    // A duplicate display name gets its own id, not a collision.
    const auto again = registry.create("Second Campus");
    REQUIRE(again.ok());
    REQUIRE(again.id == "second-campus-2");

    REQUIRE_FALSE(registry.create("   ").ok());
}

TEST_CASE("set_active and remove keep one usable library", "[library]")
{
    TempDir tmp;
    LibraryRegistry registry(tmp.path);
    registry.load();
    registry.ensure_default();
    const auto made = registry.create("Kids");
    REQUIRE(made.ok());

    REQUIRE_FALSE(registry.set_active("nope").ok());
    REQUIRE(registry.set_active("kids").ok());
    REQUIRE(registry.active_id() == "kids");

    // The active library cannot be pulled out from under the session.
    REQUIRE_FALSE(registry.remove("kids").ok());
    REQUIRE(registry.remove(std::string{k_default_library_id}).ok());
    REQUIRE(registry.entries().size() == 1);
    // Removing only forgets the entry - content stays on disk.
    REQUIRE(fs::exists(tmp.path));
    REQUIRE_FALSE(registry.remove("kids").ok());

    LibraryRegistry reloaded(tmp.path);
    reloaded.load();
    REQUIRE(reloaded.entries().size() == 1);
    REQUIRE(reloaded.active_id() == "kids");
}

TEST_CASE("rename persists and rejects empty names", "[library]")
{
    TempDir tmp;
    LibraryRegistry registry(tmp.path);
    registry.load();
    registry.ensure_default();

    REQUIRE(registry.rename(std::string{k_default_library_id}, "Main Campus").ok());
    REQUIRE_FALSE(registry.rename(std::string{k_default_library_id}, "  ").ok());
    REQUIRE_FALSE(registry.rename("ghost", "Whatever").ok());

    LibraryRegistry reloaded(tmp.path);
    reloaded.load();
    REQUIRE(reloaded.active()->name == "Main Campus");
    // The id is stable across renames so the active pointer survives.
    REQUIRE(reloaded.active()->id == std::string{k_default_library_id});
}

TEST_CASE("attach adopts an existing folder once", "[library]")
{
    TempDir tmp;
    TempDir other;
    LibraryRegistry registry(tmp.path);
    registry.load();
    registry.ensure_default();

    const auto attached = registry.attach(other.path, "Archive");
    REQUIRE(attached.ok());
    REQUIRE(fs::is_directory(media_root(other.path)));
    // The same folder cannot be registered twice, and a refused attach makes
    // no folders (it used to create <root>/media before checking).
    fs::remove(media_root(other.path));
    REQUIRE_FALSE(registry.attach(other.path, "Archive Again").ok());
    REQUIRE_FALSE(fs::exists(media_root(other.path)));
    REQUIRE_FALSE(registry.attach({}, "Nowhere").ok());
}

namespace {

void write_bytes(const fs::path& file, const std::string& bytes)
{
    std::ofstream(file, std::ios::binary) << bytes;
}

std::string read_bytes(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// ``bytes`` in the registry file are refused: no list, no default, nothing
/// written or made on disk, and the file just as it was. Returns why.
std::string require_refused(const std::string& bytes)
{
    TempDir tmp;
    const auto file = tmp.path / "libraries.json";
    write_bytes(file, bytes);

    LibraryRegistry registry(tmp.path);
    registry.load();
    REQUIRE(registry.entries().empty());
    REQUIRE(registry.load_error().find(file.string()) != std::string::npos);

    registry.ensure_default();
    REQUIRE(registry.entries().empty());
    REQUIRE(registry.active() == nullptr);
    REQUIRE_FALSE(registry.save());
    // Creating or attaching says why, and makes no folder first.
    const auto created = registry.create("Kids");
    REQUIRE_FALSE(created.ok());
    REQUIRE(created.error == registry.load_error());
    REQUIRE_FALSE(fs::exists(tmp.path / "libraries"));
    const auto own = tmp.path / "Own";
    fs::create_directories(own);
    REQUIRE(registry.attach(own, "Own").error == registry.load_error());
    REQUIRE(fs::is_empty(own));
    REQUIRE(read_bytes(file) == bytes);
    return registry.load_error();
}

}  // namespace

TEST_CASE("A registry that cannot be read is kept, not replaced", "[library]")
{
    SECTION("not JSON") { require_refused("{ this is not json"); }
    SECTION("not an object") { require_refused("[1, 2, 3]\n"); }
    SECTION("no version")
    {
        require_refused(R"({"active":"a","libraries":[{"id":"a","name":"A","path":"/tmp/a"}]})");
    }
    SECTION("no list")
    {
        require_refused(R"({"version":1,"active":"a","libraries":{"id":"a","path":"/tmp/a"}})");
    }
    // One entry dropped would be lost at the next save; all of them would
    // leave an empty list that the default library then replaced.
    SECTION("an entry without its folder")
    {
        const auto why = require_refused(R"({"version":1,"active":"a","libraries":[
            {"id":"a","name":"A","path":"/tmp/a"},
            {"id":"c","name":"No Path"}]})");
        REQUIRE(why.find("without its id or folder") != std::string::npos);
    }
    SECTION("every entry unreadable")
    {
        require_refused(R"({"version":1,"active":"a","libraries":[
            {"id":"a","name":"A","folder":"/tmp/a"},
            {"id":"b","name":"B","folder":"/tmp/b"}]})");
    }
    SECTION("an id twice")
    {
        const auto why = require_refused(R"({"version":1,"active":"a","libraries":[
            {"id":"a","name":"A","path":"/tmp/a"},
            {"id":"a","name":"Again","path":"/tmp/b"}]})");
        REQUIRE(why.find("\"a\" twice") != std::string::npos);
    }
}

TEST_CASE("A registry version this build does not know is refused", "[library]")
{
    const auto why = require_refused(
        R"({"version":2,"active":"a","libraries":[{"id":"a","name":"A","path":"/tmp/a"}]})");
    REQUIRE(why.find("version 2") != std::string::npos);
    // Moving it aside would hide it from the build that reads it.
    REQUIRE(why.find("newer SeventhSlide") != std::string::npos);
}

TEST_CASE("A registry read after a refused one is written again", "[library]")
{
    TempDir tmp;
    const auto file = tmp.path / "libraries.json";
    write_bytes(file, "garbage");
    LibraryRegistry registry(tmp.path);
    registry.load();
    REQUIRE_FALSE(registry.load_error().empty());

    write_bytes(file, R"({"version":1,"active":"a","libraries":[{"id":"a","name":"A","path":"/tmp/a"}]})");
    registry.load();
    REQUIRE(registry.load_error().empty());
    REQUIRE(registry.rename("a", "Archive").ok());
    LibraryRegistry reloaded(tmp.path);
    reloaded.load();
    REQUIRE(reloaded.find("a")->name == "Archive");
}

TEST_CASE("unique_id survives names that slugify to nothing", "[library]")
{
    // "create" only rejects a *blank* name, so a name made entirely of
    // punctuation reaches unique_id with an empty slug. Without the fallback the
    // library would be registered under an empty id and its folder would be the
    // libraries/ directory itself.
    TempDir tmp;
    LibraryRegistry registry(tmp.path);
    registry.load();
    registry.ensure_default();

    const auto first = registry.create("***");
    REQUIRE(first.ok());
    REQUIRE(first.id == "library");

    // Two more, to walk the suffix chain past the "-2" the create test reaches.
    const auto second = registry.create("!!!");
    REQUIRE(second.ok());
    REQUIRE(second.id == "library-2");

    const auto third = registry.create("///");
    REQUIRE(third.ok());
    REQUIRE(third.id == "library-3");

    // Every id is distinct and has its own directory, which is the property the
    // suffix exists to guarantee.
    REQUIRE(registry.find("library") != nullptr);
    REQUIRE(registry.find("library-2") != nullptr);
    REQUIRE(registry.find("library-3") != nullptr);
    REQUIRE(registry.find("library")->root != registry.find("library-2")->root);
    REQUIRE(registry.find("library-2")->root != registry.find("library-3")->root);
}

namespace {

/// Make the registry's saves fail: its file is a folder with a file in it,
/// which no rename can replace.
void block_registry_writes(const LibraryRegistry& registry)
{
    const auto file = registry.registry_file();
    fs::remove(file);
    fs::create_directories(file);
    std::ofstream(file / "blocked") << "x";
}

}  // namespace

// A failed save put the removed entry back at the end of the list,
// and create made its folders before refusing a duplicate, or left them
// behind when the registry could not be written.
TEST_CASE("A failed registry write leaves libraries as they were", "[library]")
{
    TempDir tmp;
    LibraryRegistry registry(tmp.path);
    registry.load();
    registry.ensure_default();
    REQUIRE(registry.create("Second").ok());
    REQUIRE(registry.create("Third").ok());
    const auto before = registry.entries();
    REQUIRE(before.size() == 3);

    block_registry_writes(registry);

    REQUIRE_FALSE(registry.remove("second").ok());
    REQUIRE(registry.entries().size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        REQUIRE(registry.entries()[i].id == before[i].id);  // same order
    }

    REQUIRE_FALSE(registry.create("Fourth").ok());
    REQUIRE(registry.entries().size() == before.size());
    REQUIRE_FALSE(fs::exists(tmp.path / "libraries" / "fourth"));

    // An attached folder stays; the media folder made for it goes.
    TempDir other;
    REQUIRE_FALSE(registry.attach(other.path, "Archive").ok());
    REQUIRE(registry.entries().size() == before.size());
    REQUIRE(fs::is_directory(other.path));
    REQUIRE_FALSE(fs::exists(media_root(other.path)));
}

TEST_CASE("is_local_only names what stays on this computer", "[library][sync]")
{
    using seventhslide::library::is_local_only;

    // SQLite's working files.
    CHECK(is_local_only("seventhslide.db-wal"));
    CHECK(is_local_only("seventhslide.db-shm"));
    CHECK(is_local_only("seventhslide.db-journal"));
    // Thumbnails, at any depth under media/thumbs.
    CHECK(is_local_only("media/thumbs/160/3f2a.jpg"));
    CHECK(is_local_only("media/thumbs/freeform/3.stamp"));
    // A file an import is still copying, and a write's temp file, anywhere.
    CHECK(is_local_only("media/videos/.import-0123abcd.part"));
    CHECK(is_local_only("media/theme_backgrounds/.import-89abcdef.part"));
    CHECK(is_local_only("media/thumbs/160/a.jpg.4242-0.tmp"));
    CHECK(is_local_only("seventhslide.db.tmp"));
    // The sync folder, whose files the link trades on their own.
    CHECK(is_local_only("sync"));
    CHECK(is_local_only("sync/changes/0123456789abcdef/0000000001.json"));
    CHECK(is_local_only("sync/snapshots/0123456789abcdef/0000000040.json"));

    // The library itself.
    CHECK_FALSE(is_local_only("seventhslide.db"));
    CHECK_FALSE(is_local_only("media/images/3f2a.png"));
    CHECK_FALSE(is_local_only("media/videos/Welcome Loop.mp4"));
    CHECK_FALSE(is_local_only("media/theme_backgrounds/1a2b_bg_c3d4e5f6.png"));
    // Near misses: the names only count where the app puts them.
    CHECK_FALSE(is_local_only("media/images/thumbs"));
    CHECK_FALSE(is_local_only("thumbs/160/a.jpg"));
    CHECK_FALSE(is_local_only("media/sync/a.png"));
    CHECK_FALSE(is_local_only("media/videos/.import-notes.txt"));
    CHECK_FALSE(is_local_only("media/videos/.import-.part"));
    CHECK_FALSE(is_local_only("other.db-wal"));
    CHECK_FALSE(is_local_only(""));
}

