// A song's lines in a text theme's Lyrics box are planned with the rows paint
// draws, at every size it draws them: no line runs past the box's edge or out
// of its height where it can be cut to fit, and a line cut keeps its chords.
//
// The approx measurer makes a 40 px lyric character 20 px wide and a 30 px
// (75%) chord character 15 px wide.

#include <catch2/catch_test_macros.hpp>

#include "theme_test_helpers.hpp"

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/build/song_layout.hpp>
#include <seventhslide/devsupport/flat_style.hpp>
#include <seventhslide/layout/chords.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/scene/text_metrics.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

using seventhslide::build::LayoutLine;
using seventhslide::build::LayoutResult;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::layout::ChordItemKind;
using seventhslide::layout::ChordLine;
using seventhslide::scene::SceneLineRole;
using seventhslide::scene::SceneTextContent;
using seventhslide::test::resolved_from_flat;

namespace {

constexpr const char* k_long_line =
    "---[Verse:1]---\n"
    "[G]Amazing [D]grace how [C]sweet the [G]sound that [D]saved a wretch like [G]me\n";

[[nodiscard]] nlohmann::json chord_look(int width, int height)
{
    return {{"font_family", "X"}, {"font_size", 40},  {"width_px", width},
            {"height_px", height}, {"show_chords", true}, {"balance_wrapped_lines", false}};
}

/// The first slide that is not a title, as the audience frame builds and
/// measures it.
[[nodiscard]] SceneTextContent first_slide(const LayoutResult& layout,
                                           const seventhslide::theme::ResolvedTheme& theme,
                                           ApproxTextMeasurer& measurer,
                                           int skip = 0)
{
    int group = 0;
    while (layout.groups[static_cast<std::size_t>(group)].is_title) {
        ++group;
    }
    auto text = seventhslide::scene::build_song_slide_text(layout, theme, group + skip, &measurer);
    seventhslide::scene::measure_text_content(text, measurer);
    return text;
}

[[nodiscard]] double painted_height(const SceneTextContent& text)
{
    double total = 0.0;
    for (const auto& line : text.lines) {
        total += line.margin_top_px + line.metrics->height;
    }
    return total;
}

/// The chords of ``line`` in order, accidentals as written.
[[nodiscard]] std::vector<std::string> chord_names(const ChordLine& line)
{
    std::vector<std::string> out;
    for (const auto& item : line.items) {
        if (item.kind == ChordItemKind::Cell && item.cell.chord && !item.cell.chord->empty()) {
            out.push_back(*item.cell.chord);
        }
    }
    return out;
}

[[nodiscard]] std::vector<std::string> chord_names(const SceneTextContent& text)
{
    std::vector<std::string> out;
    for (const auto& line : text.lines) {
        for (auto& name : chord_names(line.chords)) {
            out.push_back(std::move(name));
        }
    }
    return out;
}

}  // namespace

TEST_CASE("A chord line wider than the box wraps, as tall as layout planned it",
          "[song][chords][layout]")
{
    ApproxTextMeasurer measurer;
    const auto theme = resolved_from_flat(chord_look(400, 1000));
    const auto layout = seventhslide::build::compute_song_layout(theme, measurer, k_long_line, "v1");
    REQUIRE(layout.lines.size() == 1);
    const LayoutLine& ln = layout.lines[0];
    REQUIRE(ln.has_chord_layout);

    const auto text = first_slide(layout, theme, measurer);
    // "Amazing grace how" / "sweet the sound that" / "saved a wretch like" / "me"
    REQUIRE(text.lines.size() == 4);
    for (std::size_t i = 0; i < text.lines.size(); ++i) {
        const auto& row = text.lines[i];
        INFO("row " << i);
        CHECK(row.has_chords);
        CHECK(row.wrap_continue == (i > 0));
        CHECK(row.metrics->width <= theme.song.width);
    }
    CHECK(painted_height(text) == ln.base_height_px);
    CHECK(ln.base_height_px == 4 * ln.line_height_px);

    // Every chord is drawn, each over its own words.
    CHECK(chord_names(text) == std::vector<std::string>{"G", "D", "C", "G", "D", "G"});
    CHECK(seventhslide::model::plain_text(text.lines[1].runs) == "sweet the sound that");
    CHECK(text.lines[1].chords.items.front().cell.chord == "C");
}

TEST_CASE("Chords that widen a line wrap it, though its words alone would fit",
          "[song][chords][layout]")
{
    // "a b" is 60 px. Over it, Cmaj7 is 75 px and a chord space 15 px, so b
    // moves to 90 px and its Dm7 ends the row at 135 px: past a 100 px box.
    ApproxTextMeasurer measurer;
    const auto theme = resolved_from_flat(chord_look(100, 1000));
    const auto layout = seventhslide::build::compute_song_layout(
        theme, measurer, "---[Verse:1]---\n[Cmaj7]a [Dm7]b\n", "v1");
    REQUIRE(layout.lines.size() == 1);
    const auto text = first_slide(layout, theme, measurer);
    REQUIRE(text.lines.size() == 2);
    for (const auto& row : text.lines) {
        CHECK(row.metrics->width <= theme.song.width);
    }
    CHECK(painted_height(text) == layout.lines[0].base_height_px);
}

TEST_CASE("The active chord line wraps at its highlight size, as tall as planned",
          "[song][chords][layout][follow]")
{
    ApproxTextMeasurer measurer;
    auto look = chord_look(400, 2000);
    look["follow_lines"] = 1;
    look["active_line"] = {{"size", 60}};
    const auto theme = resolved_from_flat(look);
    const auto layout = seventhslide::build::compute_song_layout(
        theme, measurer,
        "---[Verse:1]---\n"
        "[G]Amazing [D]grace how [C]sweet the [G]sound\n"
        "That [D]saved a [G]wretch like [C]me, I [G]once was lost\n",
        "v1");
    REQUIRE(layout.lines.size() == 2);
    const auto text = first_slide(layout, theme, measurer);

    double active = 0.0;
    double follow = 0.0;
    int active_rows = 0;
    for (const auto& row : text.lines) {
        CHECK(row.metrics->width <= theme.song.width);
        if (row.role == SceneLineRole::Active) {
            active += row.margin_top_px + row.metrics->height;
            ++active_rows;
        } else {
            follow += row.margin_top_px + row.metrics->height;
        }
    }
    // 60 px characters: "Amazing grace how" is 510 px, so the active line
    // takes more rows than the 40 px follow line does.
    CHECK(active_rows > 1);
    CHECK(active == layout.lines[0].active_height_px);
    CHECK(follow == layout.lines[1].base_height_px);
}

TEST_CASE("A chord line too tall for the box is split with its chords",
          "[song][chords][layout][split]")
{
    // Four rows of 75 px (48 px lyric, 27 px chord row) in a box 160 px high:
    // two parts of two rows.
    ApproxTextMeasurer measurer;
    const auto theme = resolved_from_flat(chord_look(400, 160));
    const auto layout = seventhslide::build::compute_song_layout(theme, measurer, k_long_line, "v1");
    REQUIRE(layout.lines.size() == 2);

    std::vector<std::string> chords;
    for (std::size_t i = 0; i < layout.lines.size(); ++i) {
        const auto& part = layout.lines[i];
        INFO("part " << i);
        REQUIRE(part.has_chord_layout);
        CHECK_FALSE(part.evenable);
        CHECK(part.base_height_px <= theme.song.height);
        // The part's chords are on its own words, for the line list too.
        CHECK(seventhslide::layout::strip_chords(part.chord_text) == part.text);

        const auto text = first_slide(layout, theme, measurer, static_cast<int>(i));
        CHECK(painted_height(text) == part.base_height_px);
        for (const auto& row : text.lines) {
            CHECK(row.metrics->width <= theme.song.width);
        }
        for (auto& name : chord_names(text)) {
            chords.push_back(std::move(name));
        }
    }
    CHECK(layout.lines[0].text == "Amazing grace how sweet the sound that");
    CHECK(layout.lines[0].chord_text == "[G]Amazing [D]grace how [C]sweet the [G]sound that");
    CHECK(layout.lines[1].chord_text == "[D]saved a wretch like [G]me");
    CHECK(chords == std::vector<std::string>{"G", "D", "C", "G", "D", "G"});
}

TEST_CASE("Outputs that differ on chords split a line where both fit",
          "[song][chords][layout][split]")
{
    // The chord output needs the line in parts; the other shows it whole and
    // holds it across the parts' rows.
    ApproxTextMeasurer measurer;
    const auto chords = resolved_from_flat(chord_look(400, 160));
    auto plain_look = chord_look(400, 1000);
    plain_look["show_chords"] = false;
    const auto plain = resolved_from_flat(plain_look);
    const auto results =
        seventhslide::build::compute_song_layouts({&chords, &plain}, measurer, k_long_line, "v1");
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].row_lines.size() == results[1].row_lines.size());
    REQUIRE(results[0].lines.size() == 2);
    for (const auto& part : results[0].lines) {
        CHECK(part.has_chord_layout);
        CHECK(part.base_height_px <= chords.song.height);
    }
    REQUIRE(results[1].lines.size() == 1);
    CHECK_FALSE(results[1].lines[0].has_chord_layout);
}

TEST_CASE("A chord before a word goes with that word when a line is split",
          "[song][chords][layout][split]")
{
    // "[F] once" is a chord with no syllable of its own; it stays with
    // "once", the word it comes before. One 75 px row a part, 200 px wide.
    ApproxTextMeasurer measurer;
    const auto theme = resolved_from_flat(chord_look(200, 80));
    const auto layout = seventhslide::build::compute_song_layout(
        theme, measurer, "---[Verse:1]---\n[G]Twas grace that [D]taught [F] once more\n", "v1");
    REQUIRE(layout.lines.size() >= 2);
    std::vector<std::string> all;
    for (const auto& part : layout.lines) {
        // A part with no chords of its own is words like any other line.
        const auto names = chord_names(part.chords);
        CHECK(part.has_chord_layout == !names.empty());
        all.insert(all.end(), names.begin(), names.end());
        if (std::find(names.begin(), names.end(), "F") != names.end()) {
            CHECK(part.text.starts_with("once"));
        }
    }
    CHECK(all == std::vector<std::string>{"G", "D", "F"});
}

TEST_CASE("Evening counts a chord line's rows, not its words'", "[song][chords][layout][even]")
{
    // The chords take "a b c" to two rows (150 px), though its words are one.
    // Evened to the first line's 300 px the last line would take a second row,
    // 342 px in all: past the box, though words alone would count 267.
    ApproxTextMeasurer measurer;
    auto look = chord_look(400, 294);
    look["balance_wrapped_lines"] = true;
    look["balance_wrapped_strength"] = 100;
    const auto theme = resolved_from_flat(look);
    const auto layout = seventhslide::build::compute_song_layout(
        theme, measurer,
        "---[Verse:1]---\n"
        "[Cmaj7sus4]a [Cmaj7sus4]b [Cmaj7sus4]c\n"
        "aaaaaaa bbbbbbb ccccc ddddd\n"
        "eeeeeeeeeeee ffffff\n",
        "v1");
    REQUIRE(layout.groups.size() == 1);
    REQUIRE(layout.groups[0].indices.size() == 3);
    REQUIRE(layout.lines[0].base_height_px == 150);
    const auto text = first_slide(layout, theme, measurer);
    CHECK(painted_height(text) <= theme.song.height);
    // There is room to even the first plain line, and none for another row.
    CHECK(layout.lines[2].wrap_fragments.empty());
}

TEST_CASE("Follow lines past a theme chip count a chord line's rows",
          "[song][chords][theme_chips][follow]")
{
    // The chip's one active line is a 75 px row. The next chip's line is two.
    auto slide_lines = [](int height) {
        seventhslide::model::OutputConfig oc;
        oc.name = "Main";
        oc.id = "out-main";
        oc.canvas_width = 1920;
        oc.canvas_height = 1080;
        auto look = chord_look(400, height);
        look["follow_lines"] = 1;
        look["fluid_slides"] = true;
        look["elements"] = seventhslide::devsupport::elements_from_flat_style(look).to_json();
        oc.song_themes = {
            seventhslide::model::ThemeEntry{"t-a", "A", look},
            seventhslide::model::ThemeEntry{"t-b", "B", look},
        };
        const nlohmann::json pack = {
            {"active", "a1"},
            {"arrangements",
             nlohmann::json::array(
                 {nlohmann::json{{"id", "a1"},
                                 {"name", "Default"},
                                 {"order", "TH1 v1 TH2 v2"},
                                 {"chips",
                                  {{"TH1", {{"out-main", {{"song", {{"theme", "t-a"}}}}}}},
                                   {"TH2", {{"out-main", {{"song", {{"theme", "t-b"}}}}}}}}}}})}};
        seventhslide::build::SlideBuildInput in;
        in.kind = seventhslide::build::BuildKind::Song;
        in.selection.kind = seventhslide::model::ContentKind::Song;
        in.song.theme_map = pack;
        in.song.lyrics = "---[Verse:1]---\n[G]One\n"
                         "---[Verse:2]---\n[G]Amazing [D]grace how [C]sweet\n";
        in.song.verse_order = "TH1 v1 TH2 v2";
        in.song.title = "Test";
        in.outputs = {oc};
        ApproxTextMeasurer measurer;
        const auto built = seventhslide::build::build_slides(in, measurer);
        REQUIRE(built.outputs.size() == 1);
        const auto& rt = built.outputs.front();
        const auto& first = rt.layout.groups.front();
        REQUIRE_FALSE(first.is_title);
        auto text = seventhslide::scene::build_song_slide_text(rt.layout, rt.theme, 0, &measurer);
        seventhslide::scene::measure_text_content(text, measurer);
        CHECK(painted_height(text) <= height);
        return first.indices.size();
    };
    // 75 + 150 is past 200 px: the follow line waits for its own slide.
    CHECK(slide_lines(200) == 1);
    CHECK(slide_lines(230) == 2);
}

TEST_CASE("A line must fit at its highlight size too", "[song][layout][follow]")
{
    // 40 px text in 400 x 200: "aaaaaaaa bbbbbbbb cccccccc" takes two 48 px rows.
    // At 80 px it takes three of 96 px: 288 px, so it is split, chords or not.
    ApproxTextMeasurer measurer;
    auto look = chord_look(400, 200);
    look["follow_lines"] = 1;
    look["active_line"] = {{"size", 80}};

    for (const bool chords : {false, true}) {
        INFO("chords " << chords);
        look["show_chords"] = chords;
        const auto theme = resolved_from_flat(look);
        const auto layout = seventhslide::build::compute_song_layout(
            theme, measurer, "---[Verse:1]---\n[G]aaaaaaaa [D]bbbbbbbb [C]cccccccc\n", "v1");
        REQUIRE(layout.lines.size() > 1);
        REQUIRE(layout.row_lines.size() == layout.lines.size());
        for (std::size_t i = 0; i < layout.lines.size(); ++i) {
            INFO("line " << i << ": " << layout.lines[i].text);
            CHECK(layout.lines[i].active_height_px <= theme.song.height);
            const auto text = first_slide(layout, theme, measurer, static_cast<int>(i));
            double active = 0.0;
            for (const auto& row : text.lines) {
                if (row.role == SceneLineRole::Active) {
                    active += row.metrics->height;
                }
            }
            CHECK(active == layout.lines[i].active_height_px);
        }
    }

    // At the highlight size of the box's own text nothing changes.
    look["show_chords"] = false;
    look["active_line"] = {{"size", 40}};
    const auto same = resolved_from_flat(look);
    CHECK(seventhslide::build::compute_song_layout(
              same, measurer, "---[Verse:1]---\naaaaaaaa bbbbbbbb cccccccc\n", "v1")
              .lines.size()
          == 1);
}

TEST_CASE("A line of chords too tall for the box is cut between chord rows",
          "[song][chords][layout][split]")
{
    // Seven chords, 3 characters and a chord space a piece (60 px), in 200 px:
    // three to a row, three rows of 75 px in a box that holds two.
    ApproxTextMeasurer measurer;
    const auto chords = resolved_from_flat(chord_look(200, 160));
    auto hidden_look = chord_look(200, 160);
    hidden_look["show_chords"] = false;
    const auto hidden = resolved_from_flat(hidden_look);
    const auto results = seventhslide::build::compute_song_layouts(
        {&chords, &hidden}, measurer,
        "---[Verse:1]---\nBefore\n[Am7] [Bm7] [Cm7] [Dm7] [Em7] [Fm7] [Gm7]\nAfter\n", "v1");
    REQUIRE(results.size() == 2);

    std::vector<std::string> names;
    for (std::size_t i = 0; i < results[0].lines.size(); ++i) {
        const auto& ln = results[0].lines[i];
        INFO("line " << i << " " << ln.label);
        CHECK(ln.base_height_px <= chords.song.height);
        for (auto& name : chord_names(ln.chords)) {
            names.push_back(std::move(name));
        }
    }
    CHECK(names == std::vector<std::string>{"Am7", "Bm7", "Cm7", "Dm7", "Em7", "Fm7", "Gm7"});
    CHECK(results[0].lines.size() == 4);  // Before, two parts of chords, After

    // The output that hides chords has every row, and holds "Before" over them.
    REQUIRE(results[1].row_lines.size() == results[0].row_lines.size());
    REQUIRE(results[1].lines.size() == 2);
    CHECK(results[1].row_lines == std::vector<int>{0, -1, -1, 1});
    const auto held = seventhslide::build::row_to_slide(results[1]);
    CHECK(held[1] == held[0]);
    CHECK(held[2] == held[0]);
}

TEST_CASE("One word with more chords than fit is cut between chord rows",
          "[song][chords][layout][split]")
{
    // "[Am7] word [Bm7]": no word to cut at, and the word in a row of its own
    // once the chords will not fit beside it. 100 px wide, one row high.
    ApproxTextMeasurer measurer;
    const auto theme = resolved_from_flat(chord_look(100, 80));
    const auto layout = seventhslide::build::compute_song_layout(
        theme, measurer, "---[Verse:1]---\nwords [Am7sus4] [Bm7sus4]\n", "v1");
    std::vector<std::string> names;
    int with_word = 0;
    for (const auto& ln : layout.lines) {
        CHECK(ln.base_height_px <= theme.song.height);
        with_word += ln.text == "words" ? 1 : 0;
        for (auto& name : chord_names(ln.chords)) {
            names.push_back(std::move(name));
        }
    }
    CHECK(with_word == 1);
    CHECK(names == std::vector<std::string>{"Am7sus4", "Bm7sus4"});
    CHECK(layout.lines.size() == 3);
}

TEST_CASE("A word wider than the box is cut to fit, chords or not",
          "[song][layout][wrap]")
{
    // 40 px characters are 20 px wide: "Hallelujah" is 200 px in a 100 px box.
    ApproxTextMeasurer measurer;
    for (const bool chords : {false, true}) {
        INFO("chords " << chords);
        auto look = chord_look(100, 1000);
        look["show_chords"] = chords;
        const auto theme = resolved_from_flat(look);
        const auto layout = seventhslide::build::compute_song_layout(
            theme, measurer, "---[Verse:1]---\n[G]Hallelujah [D]amen\n", "v1");
        REQUIRE(layout.lines.size() == 1);
        CHECK(layout.lines[0].text == "Hallelujah amen");
        CHECK_FALSE(layout.lines[0].evenable);
        const auto text = first_slide(layout, theme, measurer);
        REQUIRE(text.lines.size() == 3);
        std::vector<std::string> rows;
        for (const auto& row : text.lines) {
            CHECK(row.metrics->width <= theme.song.width);
            rows.push_back(seventhslide::model::plain_text(row.runs));
        }
        CHECK(rows == std::vector<std::string>{"Halle", "lujah", "amen"});
        CHECK_FALSE(text.lines[0].wrap_in_word);
        CHECK(text.lines[1].wrap_in_word);
        CHECK_FALSE(text.lines[2].wrap_in_word);
        CHECK(painted_height(text) == layout.lines[0].base_height_px);
    }
}

TEST_CASE("A row opens up for its largest text, as layout plans it", "[song][layout]")
{
    // 40 px text in 48 px rows; the 200% word needs 96 px. Only its row does.
    ApproxTextMeasurer measurer;
    auto look = chord_look(400, 1000);
    look["show_chords"] = false;
    const auto theme = resolved_from_flat(look);
    const auto layout = seventhslide::build::compute_song_layout(
        theme, measurer, "---[Verse:1]---\naaaaaaaa bbbbbbbb <size=200>cccccc</size>\n", "v1");
    REQUIRE(layout.lines.size() == 1);
    CHECK(layout.lines[0].base_height_px == 48 + 96);
    CHECK_FALSE(layout.lines[0].evenable);
    const auto text = first_slide(layout, theme, measurer);
    REQUIRE(text.lines.size() == 2);
    CHECK(text.lines[0].metrics->height == 48);
    CHECK(text.lines[1].metrics->height == 96);
    CHECK(painted_height(text) == layout.lines[0].base_height_px);
    // The large text fits its row: its top is at or below the row's.
    CHECK(text.lines[1].metrics->baseline >= measurer.font_metrics({.size_px = 80}).ascent);
}

TEST_CASE("Nothing of a word is lost when it is cut at its chords",
          "[song][chords][layout][split]")
{
    // One 75 px row a part, 100 px wide. "fearful," is too wide and too tall to
    // stay whole, and its comma has no chord over it.
    ApproxTextMeasurer measurer;
    const auto theme = resolved_from_flat(chord_look(100, 80));
    const auto layout = seventhslide::build::compute_song_layout(
        theme, measurer, "---[Verse:1]---\n<i>[C]fearful</i>, and\n", "v1");
    std::string read;
    for (const auto& ln : layout.lines) {
        read += seventhslide::model::plain_text(seventhslide::model::parse_inline_markup(ln.text));
    }
    CHECK(read == "fearful,and");
    REQUIRE(layout.row_lines.size() == layout.lines.size());
    CHECK(std::ranges::none_of(layout.row_lines, [](int line) { return line < 0; }));
}

TEST_CASE("A part with no chords of its own has no chord row", "[song][chords][layout][split]")
{
    // "[G]aaaa bbbb cccc dddd" in a box one row wide and high: only the first
    // part has a chord, and the rest are words like any other line's.
    ApproxTextMeasurer measurer;
    const auto theme = resolved_from_flat(chord_look(100, 80));
    const auto layout = seventhslide::build::compute_song_layout(
        theme, measurer, "---[Verse:1]---\n[G]aaaa bbbb cccc\n", "v1");
    REQUIRE(layout.lines.size() == 3);
    CHECK(layout.lines[0].has_chord_layout);
    CHECK(layout.lines[0].base_height_px == 75);
    CHECK_FALSE(layout.lines[1].has_chord_layout);
    CHECK(layout.lines[1].base_height_px == 48);
    CHECK(layout.lines[1].chord_text.empty());
}

TEST_CASE("A quiet row closes up as it does in a Freeform box", "[song][layout]")
{
    ApproxTextMeasurer measurer;
    auto look = chord_look(400, 1000);
    look["show_chords"] = false;
    const auto theme = resolved_from_flat(look);
    const auto layout = seventhslide::build::compute_song_layout(
        theme, measurer, "---[Verse:1]---\n<size=50>quiet</size>\n", "v1");
    REQUIRE(layout.lines.size() == 1);
    CHECK(layout.lines[0].base_height_px == 24);
    const auto text = first_slide(layout, theme, measurer);
    CHECK(painted_height(text) == 24);
}
