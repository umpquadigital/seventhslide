// The Bibles SeventhSlide ships, and a library's copy of them.

#include <catch2/catch_test_macros.hpp>

#include "facade_all.hpp"
#include "fixture_files.hpp"
#include "library_controller.hpp"
#include "shipped_bibles.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "test_wait.hpp"

#include <seventhslide/db/bible_repository.hpp>

#include <QString>
#include <QTest>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using seventhslide::app::read_shipped_bibles;
using seventhslide::app::SessionFacade;
using seventhslide::app::shipped_bible_abbreviations;
using seventhslide::db::BibleRepository;
using seventhslide::test::wait_until;

namespace {

/// third_party/bibles in the source tree: what the program is built with.
[[nodiscard]] QString source_bibles_dir()
{
    const auto dir = seventhslide::test::fixtures_dir("..") / "third_party" / "bibles";
    return QString::fromStdString(dir.string());
}

/// A folder of two shipped Bibles: "Test OSIS" as TST and "MS" as MS.
struct ShippedFixtures {
    seventhslide::test::TempDir tmp;

    ShippedFixtures()
    {
        const auto bibles = seventhslide::test::fixtures_dir("bibles");
        std::filesystem::copy_file(bibles / "osis_sample.xml", tmp.path / "TST.xml");
        std::filesystem::copy_file(bibles / "osis_milestones.xml", tmp.path / "MS.xml");
    }

    [[nodiscard]] QString dir() const { return QString::fromStdString(tmp.path.string()); }
};

[[nodiscard]] const seventhslide::model::Bible* named(const std::vector<seventhslide::model::Bible>& all,
                                                     std::string_view name)
{
    const auto it = std::ranges::find(all, name, &seventhslide::model::Bible::name);
    return it == all.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("Every shipped Bible reads in full, named for its abbreviation", "[bible][app]")
{
    const auto dir = source_bibles_dir();
    CHECK(shipped_bible_abbreviations(dir) == std::vector<std::string>{"ASV", "BSB", "KJV"});
    const auto bibles = read_shipped_bibles(dir);
    REQUIRE(bibles.size() == 3);
    const std::vector<std::pair<std::string, std::string>> names{
        {"ASV", "American Standard Version"},
        {"BSB", "Berean Standard Bible"},
        {"KJV", "King James Version"},
    };
    for (std::size_t i = 0; i < bibles.size(); ++i) {
        const auto& shipped = bibles[i];
        INFO(shipped.abbreviation);
        CHECK(shipped.abbreviation == names[i].first);
        CHECK(shipped.bible.name == names[i].second);
        CHECK_FALSE(shipped.bible.copyright.empty());
        std::set<std::string> books;
        for (const auto& verse : shipped.bible.verses) {
            books.insert(verse.book);
        }
        CHECK(books.size() == 66);
        // The BSB leaves out the verses most modern translations do (Matthew
        // 17:21 and the like).
        CHECK(shipped.bible.verses.size() >= 31080);
        const auto john = std::ranges::find_if(shipped.bible.verses, [](const auto& verse) {
            return verse.book == "John" && verse.chapter == 3 && verse.verse_num == 16;
        });
        REQUIRE(john != shipped.bible.verses.end());
        CHECK(john->text.starts_with("For God so loved the world"));
    }
}

TEST_CASE("A library takes a copy of the shipped Bibles once, and its own stay", "[bible][app]")
{
    seventhslide::test::ensure_qt_application();
    const ShippedFixtures shipped;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    BibleRepository bibles(*facade.open_library().database());
    // This program ships none: the library opened takes nothing, and has not
    // had its copy.
    QTest::qWait(50);
    CHECK(bibles.list_all().empty());
    CHECK_FALSE(bibles.bundled_taken());

    // The library's own Bible of a shipped name is not touched.
    seventhslide::model::BibleVerse verse;
    verse.book = "Genesis";
    verse.chapter = 1;
    verse.verse_num = 1;
    verse.text = "Our own.";
    const auto own = bibles.import_bible("MS", "", {verse});

    auto& library = *facade.library();
    library.set_shipped_bibles_dir(shipped.dir());
    library.take_shipped_bibles();
    REQUIRE(wait_until([&] { return bibles.bundled_taken(); }));
    const auto all = bibles.list_all();
    REQUIRE(all.size() == 2);
    const auto* ms = named(all, "MS");
    REQUIRE(ms != nullptr);
    CHECK(ms->id == own);
    CHECK(bibles.get_chapter(own, "Genesis", 1).size() == 1);
    const auto* taken = named(all, "Test OSIS");
    REQUIRE(taken != nullptr);
    CHECK(taken->abbreviation == "TST");
    CHECK(taken->copyright == "Public Domain");
    // The lists follow.
    CHECK(library.bibles()->rowCount() == 2);

    // Removed, it stays removed.
    REQUIRE(taken->id);
    REQUIRE(bibles.remove(*taken->id));
    library.take_shipped_bibles();
    QTest::qWait(100);
    CHECK(bibles.list_all().size() == 1);
}

TEST_CASE("A library only viewed here, or joined here, takes none of the shipped Bibles",
          "[bible][app]")
{
    seventhslide::test::ensure_qt_application();
    const ShippedFixtures shipped;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    BibleRepository bibles(*facade.open_library().database());
    auto& library = *facade.library();
    library.set_shipped_bibles_dir(shipped.dir());

    SECTION("viewed")
    {
        library.set_may_write([] { return false; });
    }
    SECTION("joined")
    {
        library.set_may_take_shipped([] { return false; });
    }
    library.take_shipped_bibles();
    QTest::qWait(200);
    CHECK(bibles.list_all().empty());
    CHECK_FALSE(bibles.bundled_taken());
}

TEST_CASE("Shipped Bibles read for a library that has gone are not put in the next",
          "[bible][app]")
{
    seventhslide::test::ensure_qt_application();
    const ShippedFixtures shipped;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto& library = *facade.library();
    library.set_shipped_bibles_dir(shipped.dir());
    library.take_shipped_bibles();
    // Another library opens before the read is back. It takes its own copy
    // once its open settles, and only one.
    REQUIRE(facade.open_memory_database());
    BibleRepository bibles(*facade.open_library().database());
    REQUIRE(wait_until([&] { return bibles.bundled_taken(); }));
    QTest::qWait(100);
    CHECK(bibles.list_all().size() == 2);
}
