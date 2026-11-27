#include <catch2/catch_test_macros.hpp>

#include "fixture_files.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/parse/bible.hpp>
#include <seventhslide/parse/xml_safe.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using seventhslide::parse::BibleXmlError;
using seventhslide::parse::parse_bible_file;
using seventhslide::parse::parse_bible_reference;
using seventhslide::parse::resolve_bible_book;

namespace {

std::filesystem::path fixtures_dir()
{
    return seventhslide::test::fixtures_dir("bibles");
}

using seventhslide::test::load_json;

void require_bible_matches_golden(const std::string& stem)
{
    const auto dir = fixtures_dir();
    const auto xml = dir / (stem + ".xml");
    const auto golden = dir / (stem + ".expected.json");
    REQUIRE(std::filesystem::exists(xml));
    REQUIRE(std::filesystem::exists(golden));

    const auto parsed = parse_bible_file(xml);
    const auto expected = load_json(golden);
    REQUIRE(parsed.name == expected.at("name").get<std::string>());
    REQUIRE(parsed.copyright == expected.at("copyright").get<std::string>());
    REQUIRE(parsed.verses.size() == expected.at("verses").size());
    for (std::size_t i = 0; i < parsed.verses.size(); ++i) {
        const auto& got = parsed.verses[i];
        const auto& exp = expected.at("verses").at(i);
        REQUIRE(got.book == exp.at("book").get<std::string>());
        REQUIRE(got.chapter == exp.at("chapter").get<int>());
        REQUIRE(got.verse_num == exp.at("verse").get<int>());
        REQUIRE(got.text == exp.at("text").get<std::string>());
    }
}

} // namespace

TEST_CASE("parse_bible_file Zefania sample matches golden", "[parse][bible]")
{
    require_bible_matches_golden("zefania_sample");
}

TEST_CASE("parse_bible_file OpenSong sample matches golden", "[parse][bible]")
{
    require_bible_matches_golden("opensong_sample");
}

TEST_CASE("parse_bible_file OSIS sample matches golden", "[parse][bible]")
{
    // Container verses: word markup kept as text, notes and headings left
    // out, the work the text names, a joined verse under its first number.
    require_bible_matches_golden("osis_sample");
}

TEST_CASE("parse_bible_file OSIS milestone verses match golden", "[parse][bible]")
{
    // Milestone verses across poetry lines and paragraphs, under a namespace
    // prefix; text between verses belongs to none.
    require_bible_matches_golden("osis_milestones");
}

TEST_CASE("parse_bible_file OSIS edge cases match golden", "[parse][bible]")
{
    // Under an osisCorpus: a work prefix and a sub-verse mark, an id that is
    // not a verse's, a note's space before punctuation (ASCII and CJK), verse
    // markers in a Psalm's canonical title and in a heading, an end with no
    // verse open, a verse left open at a book's end, quotation marks given as
    // markers, a speaker, and a book with no English name.
    require_bible_matches_golden("osis_edge_cases");
}

TEST_CASE("parse_bible_file OSIS with no osisText has no verses", "[parse][bible]")
{
    const seventhslide::test::TempDir tmp;
    const auto path = tmp.path / "empty_osis.xml";
    {
        std::ofstream out(path, std::ios::binary);
        out << R"(<osis><header><work><title>Lost</title></work></header></osis>)";
    }
    const auto parsed = parse_bible_file(path);
    REQUIRE(parsed.name == "Unknown Bible");
    REQUIRE(parsed.copyright.empty());
    REQUIRE(parsed.verses.empty());
}

TEST_CASE("A verse numbered twice keeps both texts under one number", "[parse][bible]")
{
    require_bible_matches_golden("duplicate_verse");
}

TEST_CASE("parse_bible_file unsupported root yields empty verses", "[parse][bible]")
{
    require_bible_matches_golden("unsupported_root");
}

TEST_CASE("parse_bible_file rejects DOCTYPE", "[parse][bible]")
{
    const auto xml = fixtures_dir() / "doctype_rejected.xml";
    REQUIRE(std::filesystem::exists(xml));
    REQUIRE_THROWS_AS(parse_bible_file(xml), BibleXmlError);
}

TEST_CASE("parse_bible_file refuses a file nested too deep", "[parse][bible]")
{
    // The Bible reader does not recurse, but holds to the shared limit: the
    // verse is four deep, and what is in it takes it past.
    const seventhslide::test::TempDir tmp;
    const auto path = tmp.path / "deep.xml";
    const int extra = seventhslide::parse::k_max_xml_depth - 3;
    {
        std::ofstream out(path, std::ios::binary);
        out << R"(<bible><b n="Genesis"><c n="1"><v n="1">In the beginning)";
        for (int i = 0; i < extra; ++i) {
            out << "<x>";
        }
        for (int i = 0; i < extra; ++i) {
            out << "</x>";
        }
        out << "</v></c></b></bible>";
    }
    REQUIRE_THROWS_AS(parse_bible_file(path), BibleXmlError);
}

TEST_CASE("parse_bible_file missing file throws", "[parse][bible]")
{
    REQUIRE_THROWS_AS(
        parse_bible_file(fixtures_dir() / "no_such_bible.xml"), BibleXmlError);
}

TEST_CASE("resolve_bible_book aliases and prefixes", "[parse][bible]")
{
    const auto golden = load_json(fixtures_dir() / "references.expected.json");
    const auto books = golden.at("books").get<std::vector<std::string>>();

    REQUIRE(resolve_bible_book(books, "rom") == "Romans");
    REQUIRE(resolve_bible_book(books, "jn") == "John");
    REQUIRE(resolve_bible_book(books, "1cor") == "1 Corinthians");
    REQUIRE(resolve_bible_book(books, "song") == "Song of Solomon");
    REQUIRE(resolve_bible_book(books, "gen") == "Genesis");
    REQUIRE_FALSE(resolve_bible_book(books, "xyz").has_value());
    REQUIRE_FALSE(resolve_bible_book(books, "j").has_value()); // ambiguous
}

TEST_CASE("suggest_bible_books prefixes and aliases", "[parse][bible]")
{
    const auto golden = load_json(fixtures_dir() / "references.expected.json");
    const auto books = golden.at("books").get<std::vector<std::string>>();

    using seventhslide::parse::suggest_bible_books;

    REQUIRE(suggest_bible_books(books, "").empty());
    REQUIRE(suggest_bible_books(books, "xyz").empty());

    const auto reve = suggest_bible_books(books, "Reve");
    REQUIRE(reve.size() == 1);
    REQUIRE(reve.front() == "Revelation");

    const auto jn = suggest_bible_books(books, "jn");
    REQUIRE(std::find(jn.begin(), jn.end(), "John") != jn.end());

    const auto jo = suggest_bible_books(books, "Jo");
    REQUIRE(std::find(jo.begin(), jo.end(), "John") != jo.end());
    REQUIRE(std::find(jo.begin(), jo.end(), "Job") != jo.end());
    REQUIRE(std::find(jo.begin(), jo.end(), "Joel") != jo.end());
    REQUIRE(std::find(jo.begin(), jo.end(), "Jonah") != jo.end());
    REQUIRE(std::find(jo.begin(), jo.end(), "Joshua") != jo.end());
    REQUIRE(std::find(jo.begin(), jo.end(), "Judges") == jo.end());
}

TEST_CASE("parse_bible_reference refuses numbers too large for an int", "[parse][bible]")
{
    // Typed into the search field one keystroke at a time, so these arrive
    // constantly while an operator leans on a key. They used to throw
    // std::out_of_range out of a QML call.
    const std::vector<std::string> books{"John"};
    for (const auto* input : {"John 99999999999", "John 3:99999999999",
                              "John 3:16-99999999999", "John 99999999999:1"}) {
        INFO("input=" << input);
        REQUIRE_NOTHROW(parse_bible_reference(input, books));
        REQUIRE_FALSE(parse_bible_reference(input, books).has_value());
    }
    // The largest int still parses; whether it exists is the lookup's call.
    const auto max = parse_bible_reference("John 2147483647", books);
    REQUIRE(max.has_value());
    REQUIRE(max->chapter == 2147483647);
}

TEST_CASE("parse_bible_reference matches the golden fixture", "[parse][bible]")
{
    const auto golden = load_json(fixtures_dir() / "references.expected.json");
    const auto books = golden.at("books").get<std::vector<std::string>>();

    for (const auto& c : golden.at("cases")) {
        const auto input = c.at("input").get<std::string>();
        const auto result = parse_bible_reference(input, books);
        INFO("input=" << input);
        if (c.at("result").is_null()) {
            REQUIRE_FALSE(result.has_value());
            continue;
        }
        REQUIRE(result.has_value());
        const auto& exp = c.at("result");
        REQUIRE(result->book == exp.at("book").get<std::string>());
        REQUIRE(result->chapter == exp.at("chapter").get<int>());
        if (exp.at("verse_start").is_null()) {
            REQUIRE_FALSE(result->verse_start.has_value());
            REQUIRE_FALSE(result->verse_end.has_value());
        } else {
            REQUIRE(result->verse_start == exp.at("verse_start").get<int>());
            REQUIRE(result->verse_end == exp.at("verse_end").get<int>());
        }
    }
}
