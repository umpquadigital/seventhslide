#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "fixture_files.hpp"

#include <seventhslide/layout/chords.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using seventhslide::layout::ChordItemKind;
using seventhslide::layout::ChordLine;
using seventhslide::layout::format_chord_accidentals;
using seventhslide::layout::has_chords;
using seventhslide::layout::parse_chord_line;
using seventhslide::layout::lyric_without_chords;
using seventhslide::layout::strip_chords;

namespace {

std::filesystem::path fixtures_dir()
{
    return seventhslide::test::fixtures_dir("chords");
}

using seventhslide::test::load_json;

void require_line_matches(const ChordLine& line, const nlohmann::json& items)
{
    REQUIRE(line.items.size() == items.size());
    for (std::size_t i = 0; i < line.items.size(); ++i) {
        const auto& got = line.items[i];
        const auto& exp = items[i];
        const std::string kind = exp.at("kind").get<std::string>();
        if (kind == "space") {
            REQUIRE(got.kind == ChordItemKind::Space);
            continue;
        }
        REQUIRE(got.kind == ChordItemKind::Cell);
        REQUIRE(got.cell.syllable == exp.at("syllable").get<std::string>());
        REQUIRE(got.cell.style.bold == exp.at("style").at("bold").get<bool>());
        REQUIRE(got.cell.style.italic == exp.at("style").at("italic").get<bool>());
        REQUIRE(got.cell.style.underline == exp.at("style").at("underline").get<bool>());
        if (exp.at("chord").is_null()) {
            REQUIRE_FALSE(got.cell.chord.has_value());
        } else {
            REQUIRE(got.cell.chord.has_value());
            REQUIRE(*got.cell.chord == exp.at("chord").get<std::string>());
        }
    }
}

}  // namespace

TEST_CASE("has_chords / strip_chords / accidentals", "[layout][chords]")
{
    REQUIRE_FALSE(has_chords("Hello"));
    REQUIRE(has_chords("[C]Hello"));
    REQUIRE(has_chords("[]"));
    REQUIRE_FALSE(has_chords("[unclosed"));

    REQUIRE(strip_chords("[C]Hello [G]world") == "Hello world");
    REQUIRE(strip_chords("no chords") == "no chords");

    REQUIRE(format_chord_accidentals("F#") == "F♯");
    REQUIRE(format_chord_accidentals("Bb") == "B♭");
    REQUIRE(format_chord_accidentals("B") == "B");  // uppercase B is a note
    REQUIRE(lyric_without_chords("  [A]word  ") == "word");
    REQUIRE(lyric_without_chords("[C]  Hi   there") == "Hi there");
}

TEST_CASE("parse_chord_line matches the golden fixture", "[layout][chords]")
{
    const auto doc = load_json(fixtures_dir() / "chord_lines.expected.json");
    REQUIRE(doc.is_array());
    for (const auto& case_j : doc) {
        INFO("input=" << case_j.at("input").get<std::string>());
        const auto input = case_j.at("input").get<std::string>();
        REQUIRE(has_chords(input) == case_j.at("has_chords").get<bool>());
        REQUIRE(strip_chords(input) == case_j.at("strip_chords").get<std::string>());
        REQUIRE(lyric_without_chords(input)
                == case_j.at("plain_measure").get<std::string>());
        require_line_matches(parse_chord_line(input), case_j.at("items"));
    }
}

TEST_CASE("chord cells show typographic accidentals", "[layout][chords]")
{
    const auto line = parse_chord_line("[F#]x");
    REQUIRE(line.items.size() == 1);
    REQUIRE(*line.items[0].cell.chord == "F♯");
    REQUIRE(line.items[0].cell.syllable == "x");

    const auto day = parse_chord_line("[Bb]Day");
    REQUIRE(*day.items[0].cell.chord == "B♭");
}

// Chord mode only understood <b>/<i>/<u>; any other tag became part
// of a syllable and was painted as text.
TEST_CASE("chord cells take every inline style and show no tags", "[layout][chords]")
{
    const ChordLine line = parse_chord_line(
        "<color=#ff0000>[G]Amazing</color> <font=Open Sans>[D]grace</font> <b>how</b>");
    std::vector<const seventhslide::layout::ChordCell*> cells;
    for (const auto& it : line.items) {
        if (it.kind == ChordItemKind::Cell) {
            cells.push_back(&it.cell);
        }
    }
    REQUIRE(cells.size() == 3);
    for (const auto* cell : cells) {
        INFO(cell->syllable);
        CHECK(cell->syllable.find('<') == std::string::npos);
    }
    CHECK(cells[0]->syllable == "Amazing");
    CHECK(cells[0]->chord == std::optional<std::string>("G"));
    CHECK(cells[0]->style.color == "#ff0000");
    CHECK(cells[1]->syllable == "grace");
    CHECK(cells[1]->style.font_family == "Open Sans");
    CHECK(cells[2]->syllable == "how");
    CHECK(cells[2]->style.bold);
}

TEST_CASE("chord marks come out of markup and go back in", "[layout][chords]")
{
    using seventhslide::layout::ChordMark;
    using seventhslide::layout::put_chord_marks;
    using seventhslide::layout::take_chord_marks;

    SECTION("plain text")
    {
        const auto taken = take_chord_marks("Hal[C]le[D]lu[G]");
        CHECK(taken.markup == "Hallelu");
        CHECK(taken.marks == std::vector<ChordMark>{{3, "C"}, {5, "D"}, {7, "G"}});
        CHECK(put_chord_marks(taken.markup, taken.marks) == "Hal[C]le[D]lu[G]");
    }

    SECTION("markup without chords is left as it is")
    {
        const auto taken = take_chord_marks("<b>Amazing</b>  grace");
        CHECK(taken.markup == "<b>Amazing</b>  grace");
        CHECK(taken.marks.empty());
        CHECK(put_chord_marks("<b>Amazing</b>  grace", {}) == "<b>Amazing</b>  grace");
    }

    SECTION("styled runs keep their style and offsets skip the tags")
    {
        const auto taken = take_chord_marks("[G]<b>Amazing</b> [D]grace");
        CHECK(taken.markup == "<b>Amazing</b> grace");
        CHECK(taken.marks == std::vector<ChordMark>{{0, "G"}, {8, "D"}});
    }

    SECTION("line breaks do not count")
    {
        const auto taken = take_chord_marks("A[G]b\nc[D]");
        CHECK(taken.markup == "Ab\nc");
        CHECK(taken.marks == std::vector<ChordMark>{{1, "G"}, {3, "D"}});
        CHECK(put_chord_marks("Ab\n\nc", taken.marks) == "A[G]b\n\nc[D]");
    }

    SECTION("marks go in sorted, and past the end after the text")
    {
        CHECK(put_chord_marks("Lord", {{9, "G"}, {0, "Am7"}, {0, "D"}}) == "[Am7][D]Lord[G]");
        CHECK(put_chord_marks("", {{0, "G"}}) == "[G]");
    }
}

TEST_CASE("lyric_with_chords trims and collapses like lyric_without_chords",
          "[layout][chords]")
{
    using seventhslide::layout::lyric_with_chords;
    for (const char* raw : {"  [G] Amazing   [C]grace  ", "love[E/G#] for [A2] us,",
                            "I can[E]not give an a[Bsus]nswer.[E]", "[G] [D] [Em7]",
                            "<b>Bold</b>  [G]word", "no chords  here "}) {
        const std::string kept = lyric_with_chords(raw);
        CHECK(strip_chords(kept) == lyric_without_chords(raw));
        CHECK(seventhslide::layout::take_chord_marks(kept).marks.size()
              == seventhslide::layout::take_chord_marks(raw).marks.size());
    }
    CHECK(lyric_with_chords("  [G] Amazing   [C]grace  ") == "[G]Amazing [C]grace");
    CHECK(lyric_with_chords("love[E/G#] for [A2] us,") == "love[E/G#] for [A2]us,");
}

TEST_CASE("wrap_chord_items wraps whole words with their chords", "[layout][chords]")
{
    using seventhslide::layout::WrapRun;
    using seventhslide::layout::wrap_chord_items;
    // 40 px lyric characters are 20 px wide, 30 px (75%) chords' 15 px.
    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::layout::FontStyle lyric;
    lyric.size_px = 40;
    const auto chord = seventhslide::layout::chord_font(lyric, {});
    auto rows = [&](std::string_view line, double width) {
        const auto parsed = parse_chord_line(line);
        return wrap_chord_items(parsed.items, lyric, chord, measurer, width);
    };

    SECTION("a line that fits is one row")
    {
        CHECK(rows("[G]Amazing [D]grace", 400) == std::vector<WrapRun>{{0, 3}});
    }

    SECTION("rows break between words and leave the spaces out")
    {
        // Amazing grace how | sweet
        CHECK(rows("[G]Amazing [D]grace how [C]sweet", 400)
              == std::vector<WrapRun>{{0, 5}, {6, 7}});
    }

    SECTION("chords that push the words along count in a row's width")
    {
        // "a b" is 60 px; Cmaj7 over a moves b to 90 px and Dm7 ends at 135.
        CHECK(rows("a b", 100) == std::vector<WrapRun>{{0, 3}});
        CHECK(rows("[Cmaj7]a [Dm7]b", 100) == std::vector<WrapRun>{{0, 1}, {2, 3}});
    }

    SECTION("a word split by chords stays whole where it fits")
    {
        // Hal 60, le 40, lu 40, jah 60: 200 px.
        CHECK(rows("Hal[C]le[G]lu[D]jah amen", 400) == std::vector<WrapRun>{{0, 6}});
        CHECK(rows("Hal[C]le[G]lu[D]jah amen", 200) == std::vector<WrapRun>{{0, 4}, {5, 6}});
    }

    SECTION("a word wider than the box breaks where its chords do")
    {
        CHECK(rows("Hal[C]le[G]lu[D]jah amen", 100)
              == std::vector<WrapRun>{{0, 2}, {2, 4}, {5, 6}});
    }

    SECTION("a chord alone before a word goes to the word's row")
    {
        // "one two" and F fit 200 px, "three" does not: F starts its row.
        CHECK(rows("[G]one two [F] three", 200) == std::vector<WrapRun>{{0, 3}, {4, 7}});
        // Chords alone at the end of a line still wrap among themselves.
        CHECK(rows("[G] [D] [Em] [C]", 60) == std::vector<WrapRun>{{0, 3}, {4, 7}});
    }

    SECTION("no items, no rows")
    {
        CHECK(rows("", 100).empty());
    }
}

TEST_CASE("chord_items_lyric reads a row's words back", "[layout][chords]")
{
    using seventhslide::layout::chord_items_lyric;
    const auto line = parse_chord_line("<b>[G]Amazing [D]grace</b> how [C]swe[G]et");
    const auto lyric = chord_items_lyric(line.items);
    CHECK(seventhslide::model::plain_text(lyric) == "Amazing grace how sweet");
    REQUIRE(lyric.size() == 2);
    CHECK(lyric[0].text == "Amazing grace");
    CHECK(lyric[0].style.bold);
    CHECK_FALSE(lyric[1].style.bold);
}

TEST_CASE("wrap_chord_line cuts a syllable wider than the box", "[layout][chords]")
{
    // 40 px lyric characters are 20 px wide: five to a 100 px row.
    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::layout::FontStyle lyric;
    lyric.size_px = 40;
    const auto wrapped = seventhslide::layout::wrap_chord_line(
        parse_chord_line("[G]abcdefghijkl [D]mn"), lyric, {}, measurer, 100);
    std::vector<std::string> rows;
    std::vector<std::string> chords;
    for (const auto& row : wrapped.rows) {
        const auto part = seventhslide::layout::chord_line_part(wrapped.line, row);
        rows.push_back(seventhslide::model::plain_text(
            seventhslide::layout::chord_items_lyric(part.items)));
        for (const auto& item : part.items) {
            if (item.kind == ChordItemKind::Cell && item.cell.chord) {
                chords.push_back(*item.cell.chord);
            }
        }
    }
    CHECK(rows == std::vector<std::string>{"abcde", "fghij", "kl mn"});
    CHECK(chords == std::vector<std::string>{"G", "D"});
    CHECK_FALSE(wrapped.continues_word(0));
    CHECK(wrapped.continues_word(1));
    CHECK(wrapped.continues_word(2));
}

TEST_CASE("chord_items_lyric takes one space between words, chords alone or not",
          "[layout][chords]")
{
    const auto line = parse_chord_line("a [G] [D] b");
    CHECK(seventhslide::model::plain_text(seventhslide::layout::chord_items_lyric(line.items))
          == "a b");
}
