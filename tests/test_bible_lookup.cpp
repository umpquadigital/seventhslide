// Resolving what an operator typed into an actual passage, for the console and
// the remote alike: the single-chapter-book convention, clamping a range, the
// wording of each refusal.

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/db/bible_lookup.hpp>
#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>

#include <string>
#include <vector>

using seventhslide::db::BibleRepository;
using seventhslide::db::Database;
using seventhslide::db::format_reference;
using seventhslide::db::resolve_reference;
using seventhslide::model::BibleVerse;

namespace {

struct Fixture {
    Database db;
    BibleRepository bibles;
    std::int64_t id = 0;

    Fixture() : db(Database::open_memory()), bibles(db)
    {
        seventhslide::db::ensure_schema(db);
        std::vector<BibleVerse> verses;
        for (int v = 1; v <= 36; ++v) {
            verses.push_back({.book = "John", .chapter = 3, .verse_num = v,
                              .text = "John 3:" + std::to_string(v)});
        }
        for (int v = 1; v <= 6; ++v) {
            verses.push_back({.book = "John", .chapter = 4, .verse_num = v,
                              .text = "John 4:" + std::to_string(v)});
        }
        // Jude has exactly one chapter, which is what makes "Jude 4" a verse.
        for (int v = 1; v <= 25; ++v) {
            verses.push_back({.book = "Jude", .chapter = 1, .verse_num = v,
                              .text = "Jude " + std::to_string(v)});
        }
        id = bibles.import_bible("KJV", "Public domain", verses);
        REQUIRE(id > 0);
    }
};

} // namespace

TEST_CASE("format_reference covers chapter, verse and range", "[bible][lookup]")
{
    REQUIRE(format_reference("John", 3, 16, 16) == "John 3:16");
    REQUIRE(format_reference("John", 3, 16, 18) == "John 3:16-18");
    REQUIRE(format_reference("John", 3, 0, 0) == "John 3");
    // An end below the start is not a range.
    REQUIRE(format_reference("John", 3, 16, 2) == "John 3:16");
}

TEST_CASE("a verse, a range and a whole chapter each resolve", "[bible][lookup]")
{
    seventhslide::test::ensure_qt_application();
    Fixture f;

    const auto one = resolve_reference(f.bibles, f.id, "John 3:16");
    REQUIRE(one.ok);
    REQUIRE(one.book == "John");
    REQUIRE(one.chapter == 3);
    REQUIRE(one.verse_start == 16);
    REQUIRE(one.verse_end == 16);
    REQUIRE(one.ref == "John 3:16");
    REQUIRE(one.verses.size() == 1);
    REQUIRE(one.verses.front().text == "John 3:16");

    const auto range = resolve_reference(f.bibles, f.id, "John 3:16-18");
    REQUIRE(range.ok);
    REQUIRE(range.ref == "John 3:16-18");
    REQUIRE(range.verses.size() == 3);

    // No verse means the whole chapter, and the reference shows no verse.
    const auto chapter = resolve_reference(f.bibles, f.id, "John 3");
    REQUIRE(chapter.ok);
    REQUIRE(chapter.ref == "John 3");
    REQUIRE(chapter.verse_start == 1);
    REQUIRE(chapter.verse_end == 36);
    REQUIRE(chapter.verses.size() == 36);
}

TEST_CASE("half-typed references still resolve", "[bible][lookup]")
{
    seventhslide::test::ensure_qt_application();
    Fixture f;

    // Trailing punctuation is what a reference looks like mid-keystroke.
    for (const auto* typed : {"John 3:16", "  John 3:16  ", "John 3:16:", "John 3:16-",
                              "John 3:16."}) {
        const auto got = resolve_reference(f.bibles, f.id, typed);
        INFO(typed);
        REQUIRE(got.ok);
        REQUIRE(got.ref == "John 3:16");
    }

    const auto bare = resolve_reference(f.bibles, f.id, "John 3:");
    REQUIRE(bare.ok);
    REQUIRE(bare.ref == "John 3");
}

TEST_CASE("in a single-chapter book a bare number is the verse", "[bible][lookup]")
{
    seventhslide::test::ensure_qt_application();
    Fixture f;

    const auto jude = resolve_reference(f.bibles, f.id, "Jude 4");
    REQUIRE(jude.ok);
    REQUIRE(jude.chapter == 1);
    REQUIRE(jude.verse_start == 4);
    REQUIRE(jude.verse_end == 4);
    REQUIRE(jude.ref == "Jude 1:4");

    // A book with more than one chapter keeps reading it as a chapter.
    const auto john = resolve_reference(f.bibles, f.id, "John 4");
    REQUIRE(john.ok);
    REQUIRE(john.chapter == 4);
    REQUIRE(john.ref == "John 4");
}

TEST_CASE("a range past the end of the chapter is clamped, not refused", "[bible][lookup]")
{
    seventhslide::test::ensure_qt_application();
    Fixture f;

    const auto got = resolve_reference(f.bibles, f.id, "John 4:3-99");
    REQUIRE(got.ok);
    REQUIRE(got.verse_start == 3);
    REQUIRE(got.verse_end == 6);
    REQUIRE(got.ref == "John 4:3-6");
    REQUIRE(got.verses.size() == 4);
}

TEST_CASE("refusals say what is wrong", "[bible][lookup]")
{
    seventhslide::test::ensure_qt_application();
    Fixture f;

    REQUIRE(resolve_reference(f.bibles, 99999, "John 3:16").message
            == "Select a bible first.");
    REQUIRE(resolve_reference(f.bibles, f.id, "   ").message
            == "Enter a scripture reference such as John 3:16.");
    REQUIRE(resolve_reference(f.bibles, f.id, "Habakkuk 1:1").message
            == "Could not find \"Habakkuk 1:1\" in this bible.");
    REQUIRE(resolve_reference(f.bibles, f.id, "John 9").message == "John has no chapter 9.");
    REQUIRE(resolve_reference(f.bibles, f.id, "John 3:99").message
            == "John 3 has no verse 99.");

    REQUIRE(resolve_reference(f.bibles, f.id, "John 3:99999999999").message
            == "Could not find \"John 3:99999999999\" in this bible.");

    for (const auto* bad : {"   ", "Habakkuk 1:1", "John 9", "John 3:99", "John 99999999999"}) {
        INFO(bad);
        const auto got = resolve_reference(f.bibles, f.id, bad);
        REQUIRE_FALSE(got.ok);
        REQUIRE(got.verses.empty());
    }
}

TEST_CASE("A bible's books written by another connection are seen, however they were asked before",
          "[bible][lookup][sync]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir dir;
    auto window = Database::open_file(dir.path / "library.db");
    seventhslide::db::ensure_schema(window);
    BibleRepository bibles(window);
    // Asked about before it is here (a remote holding an id), and seen empty.
    CHECK(bibles.list_books(1).empty());

    // The sync writes on a connection of its own.
    auto sync = Database::open_file(dir.path / "library.db");
    sync.exec("INSERT INTO bibles (id, name, copyright) VALUES (1, 'KJV', '')");
    sync.exec("INSERT INTO verses (bible_id, book, chapter, verse_num, text) VALUES (1, 'Ruth', 1, 1, 'r')");
    sync.exec("INSERT INTO verses (bible_id, book, chapter, verse_num, text) VALUES (1, 'Jude', 1, 1, 'j')");
    CHECK(bibles.list_books(1) == std::vector<std::string>{"Ruth", "Jude"});
    CHECK(resolve_reference(bibles, 1, "Jude 1").ok);
}
