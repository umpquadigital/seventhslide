// Overlay boxes and text lines are wrapped and measured once, when a frame is
// built, with the layout engine lyrics use.

#include <catch2/catch_test_macros.hpp>

#include "font_fixtures.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/fonts/font_registry.hpp>
#include <seventhslide/layout/chords.hpp>
#include <seventhslide/layout/overlay_layout.hpp>
#include <seventhslide/layout/qt_text_measurer.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/layout/word_wrap.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/model/stack.hpp>
#include <seventhslide/scene/chrome_text.hpp>
#include <seventhslide/scene/text_metrics.hpp>
#include <seventhslide/session/session.hpp>
#include <seventhslide/theme/theme_library.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::layout::FontStyle;
using seventhslide::layout::StyledWords;
using seventhslide::layout::OverlayBox;
using seventhslide::layout::OverlayLayout;
using seventhslide::layout::bind_run_measurer;
using seventhslide::layout::lay_out_overlay;
using seventhslide::model::parse_inline_markup;
using seventhslide::scene::SceneTextBoxStyle;
using seventhslide::scene::SceneTextLine;

namespace {

/// A 40 px box: the approx measurer makes every character 20 px wide.
OverlayBox box_of(std::vector<const char*> lines, int width)
{
    OverlayBox box;
    box.x = 0;
    box.y = 0;
    box.w = width;
    box.h = 1000;
    box.font_family = "Sans";
    box.font_size = 40;
    for (const char* line : lines) {
        box.lines.push_back(parse_inline_markup(line));
    }
    return box;
}

std::string row_text(const seventhslide::layout::OverlayRow& row)
{
    std::string out;
    for (const auto& run : row.runs) {
        out += run.text;
    }
    return out;
}

std::vector<std::string> row_texts(const OverlayLayout& layout)
{
    std::vector<std::string> out;
    for (const auto& row : layout.rows) {
        out.push_back(row_text(row));
    }
    return out;
}

FontStyle box_font()
{
    FontStyle font;
    font.family = "Sans";
    font.size_px = 40;
    return font;
}

}  // namespace

TEST_CASE("An overlay line breaks where a lyric line of the same text breaks", "[layout][overlay]")
{
    ApproxTextMeasurer measurer;
    const char* text = "A mighty fortress is our God, a bulwark never failing";
    for (const int width : {200, 333, 500, 900}) {
        INFO("width " << width);
        const auto lyric = seventhslide::layout::wrap_marked_text_to_width(
            text, bind_run_measurer(measurer, box_font()), width);
        REQUIRE(row_texts(lay_out_overlay(box_of({text}, width), measurer)) == lyric);
    }
}

TEST_CASE("Whitespace inside an overlay line is kept, and not at its ends or a break", "[layout][overlay]")
{
    ApproxTextMeasurer measurer;
    REQUIRE(row_texts(lay_out_overlay(box_of({"Name:   Value"}, 1000), measurer))
            == std::vector<std::string>{"Name:   Value"});
    REQUIRE(row_texts(lay_out_overlay(box_of({"   lead"}, 1000), measurer))
            == std::vector<std::string>{"lead"});
    REQUIRE(row_texts(lay_out_overlay(box_of({"trail   "}, 1000), measurer))
            == std::vector<std::string>{"trail"});

    // "aaaa bbbb" in room for one word: the space at the break is in neither
    // row, so it never counts toward where a centred row sits.
    const auto wrapped = lay_out_overlay(box_of({"aaaa bbbb"}, 100), measurer);
    REQUIRE(row_texts(wrapped) == std::vector<std::string>{"aaaa", "bbbb"});
    REQUIRE(wrapped.rows[0].width == 80.0);
    REQUIRE(wrapped.rows[1].width == 80.0);
}

TEST_CASE("A styled word is not split where its style changes", "[layout][overlay]")
{
    ApproxTextMeasurer measurer;
    // Room for "Bulwark" (140 px) but not "Bulwark never".
    const auto layout = lay_out_overlay(box_of({"Bul<b>wark</b> never"}, 150), measurer);
    REQUIRE(row_texts(layout) == std::vector<std::string>{"Bulwark", "never"});
    REQUIRE(layout.rows[0].runs.size() == 2);
    REQUIRE(layout.rows[0].runs[1].style.bold);
    REQUIRE(layout.rows[0].runs[1].x == 60.0);
}

TEST_CASE("A word wider than the box is broken between characters", "[layout][overlay]")
{
    ApproxTextMeasurer measurer;
    // Nine two-byte characters, 180 px, in a 160 px box.
    const std::string word = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9";
    const auto layout = lay_out_overlay(box_of({(word + " ok").c_str()}, 160), measurer);
    std::string joined;
    for (const auto& row : layout.rows) {
        REQUIRE(row.width <= 160.0);
        const auto text = row_text(row);
        // Never cut inside a character.
        REQUIRE((static_cast<unsigned char>(text.front()) & 0xC0) != 0x80);
        joined += text;
    }
    // Eight characters fit a row; the last piece shares its row with the next word.
    REQUIRE(row_texts(layout)
            == std::vector<std::string>{word.substr(0, 16), word.substr(16) + " ok"});
    REQUIRE(joined == word + " ok");

    // A word that fits is left whole.
    auto words = StyledWords::spans(parse_inline_markup("fits fine"));
    words.break_wide_words(bind_run_measurer(measurer, box_font()), 100);
    REQUIRE(words.size() == 2);
}

TEST_CASE("Overlay rows stack with line gaps, and runs sit centred in their row", "[layout][overlay]")
{
    ApproxTextMeasurer measurer;
    auto box = box_of({"one", "", "x<rise=50>2</rise>"}, 1000);
    box.line_height = 1.5;
    box.line_gap = 50;
    const auto layout = lay_out_overlay(box, measurer);

    // 40 px at 1.5 is a 60 px row; logical lines are 20 px (50%) apart, and an
    // empty line keeps its row.
    REQUIRE(layout.rows.size() == 3);
    REQUIRE(layout.rows[0].y == 0.0);
    REQUIRE(layout.rows[1].y == 80.0);
    REQUIRE(layout.rows[1].runs.empty());
    REQUIRE(layout.rows[2].y == 160.0);
    REQUIRE(layout.height == 220.0);
    for (const auto& row : layout.rows) {
        REQUIRE(row.height == 60.0);
    }
    // Approx metrics: ascent 32, height 40, so the baseline is 10 + 32.
    REQUIRE(layout.rows[0].runs[0].baseline == 42.0);
    // Rise is a share of the box size: 50% of 40 lifts the "2" by 20.
    REQUIRE(layout.rows[2].runs.size() == 2);
    REQUIRE(layout.rows[2].runs[1].baseline == 22.0);
    REQUIRE(layout.rows[2].runs[1].x == 20.0);
}

TEST_CASE("An indicator's label gap is that many spaces and never grows the row", "[layout][overlay][indicator]")
{
    using seventhslide::scene::IndicatorItem;
    using seventhslide::scene::indicator_line;
    ApproxTextMeasurer measurer;
    const std::vector<IndicatorItem> items{{"V1", true}, {"C1", false}, {"B1", false}};
    // Approx measurer: a 40 px space is 20 px, a label 40 px.
    const auto lay_out = [&](double gap, int width) {
        auto box = box_of({}, width);
        box.lines.push_back(indicator_line(items, gap));
        return lay_out_overlay(box, measurer);
    };
    const auto row_height = lay_out(1.0, 1000).rows.at(0).height;
    for (const double gap : {0.1, 0.5, 1.0, 2.5, 4.0}) {
        const auto layout = lay_out(gap, 1000);
        REQUIRE(layout.rows.size() == 1);
        const auto& row = layout.rows[0];
        CHECK(row.height == row_height);
        CHECK(std::abs(row.width - (3 * 40 + 2 * gap * 20)) < 1e-9);
        // The second label starts one label and one gap in.
        double second = -1;
        for (const auto& run : row.runs) {
            if (run.text.find("C1") != std::string::npos) {
                second = run.x + 20 * static_cast<double>(run.text.find("C1"));
            }
        }
        CHECK(std::abs(second - (40 + gap * 20)) < 1e-9);
    }
    // The gap is whitespace, so a narrow box still wraps between labels.
    CHECK(row_texts(lay_out(0.5, 60)) == std::vector<std::string>{"V1", "C1", "B1"});
    CHECK(row_texts(lay_out(2.0, 60)) == std::vector<std::string>{"V1", "C1", "B1"});
}

TEST_CASE("Paragraph spaces go between paragraphs, on top of the line gap",
          "[layout][overlay][paragraph]")
{
    ApproxTextMeasurer measurer;
    OverlayBox box = box_of({"one", "two", "three"}, 400);
    box.paragraph.space_before = 10;
    box.paragraph.space_after = 5;
    const auto laid = lay_out_overlay(box, measurer);
    REQUIRE(laid.rows.size() == 3);
    CHECK(laid.rows[0].y == 0.0);
    CHECK(laid.rows[1].y == laid.rows[0].height + 15.0);
    CHECK(laid.rows[2].y == laid.rows[1].y + laid.rows[1].height + 15.0);
    CHECK(laid.height == laid.rows[2].y + laid.rows[2].height);
    CHECK(laid.font_scale == 1.0);
}

TEST_CASE("Bullets mark each paragraph and hang its rows after the mark",
          "[layout][overlay][paragraph][bullets]")
{
    ApproxTextMeasurer measurer;
    // 20 px a character: "aa bb cc" wraps in a 160 px box once the mark hangs.
    OverlayBox box = box_of({"aa bb cc", "dd"}, 160);
    box.paragraph.bullets = seventhslide::canvas::Bullets::Disc;
    const auto laid = lay_out_overlay(box, measurer);
    REQUIRE(laid.rows.size() >= 3);
    REQUIRE_FALSE(laid.rows[0].runs.empty());
    CHECK(laid.rows[0].runs[0].text == "\u2022");
    CHECK(laid.rows[0].runs[0].x == 0.0);
    const double hang = laid.rows[0].runs[1].x;
    CHECK(hang > 20.0);
    // The paragraph's next row starts where the words did, not at the mark.
    CHECK(laid.rows[1].runs[0].x == hang);
    CHECK(laid.rows[1].runs[0].text != "\u2022");
    // Numbers count the paragraphs that get one: a blank line has none.
    box.paragraph.bullets = seventhslide::canvas::Bullets::Number;
    const auto numbered = lay_out_overlay(box, measurer);
    CHECK(numbered.rows[0].runs[0].text == "1.");
    CHECK(numbered.rows.back().runs[0].text == "2.");
    CHECK(numbered.rows.back().runs[1].text == "dd");
    OverlayBox gappy = box_of({"aa", "", "bb"}, 400);
    gappy.paragraph.bullets = seventhslide::canvas::Bullets::Number;
    const auto counted = lay_out_overlay(gappy, measurer);
    REQUIRE(counted.rows.size() == 3);
    CHECK(counted.rows[0].runs[0].text == "1.");
    CHECK(counted.rows[1].runs.empty());
    CHECK(counted.rows[2].runs[0].text == "2.");
}

TEST_CASE("A chord line keeps the margins but takes no bullet", "[layout][overlay][paragraph][chords]")
{
    ApproxTextMeasurer measurer;
    OverlayBox box = box_of({"[G]aa bb [C]cc dd"}, 200);
    box.show_chords = true;
    box.paragraph.margins.left = 40;
    box.paragraph.margins.right = 40;
    box.paragraph.bullets = seventhslide::canvas::Bullets::Disc;
    const auto laid = lay_out_overlay(box, measurer);
    REQUIRE_FALSE(laid.rows.empty());
    for (const auto& row : laid.rows) {
        CHECK(row.width <= 120.0 + 1e-6);
        for (const auto& run : row.runs) {
            CHECK(run.text != "\u2022");
        }
    }
}

TEST_CASE("An indent moves a paragraph's first row only, and margins narrow every row",
          "[layout][overlay][paragraph]")
{
    ApproxTextMeasurer measurer;
    OverlayBox box = box_of({"aa bb cc dd"}, 200);
    box.paragraph.indent = 60;
    const auto indented = lay_out_overlay(box, measurer);
    REQUIRE(indented.rows.size() >= 2);
    CHECK(indented.rows[0].runs[0].x == 60.0);
    CHECK(indented.rows[1].runs[0].x == 0.0);
    // The first row has 140 px: "aa bb cc" is 160, so it takes two words.
    CHECK(row_text(indented.rows[0]) == "aa bb");

    OverlayBox wide = box_of({"aa bb cc dd"}, 200);
    wide.paragraph.margins.left = 40;
    wide.paragraph.margins.right = 40;
    const auto inset = lay_out_overlay(wide, measurer);
    const auto narrow = lay_out_overlay(box_of({"aa bb cc dd"}, 120), measurer);
    REQUIRE(inset.rows.size() == narrow.rows.size());
    for (std::size_t i = 0; i < inset.rows.size(); ++i) {
        CHECK(row_text(inset.rows[i]) == row_text(narrow.rows[i]));
    }
}

TEST_CASE("Shrink to fit scales the type down until the rows fit the box",
          "[layout][overlay][paragraph][autofit]")
{
    ApproxTextMeasurer measurer;
    OverlayBox box = box_of({"aa bb cc dd ee ff gg hh"}, 200);
    box.h = 120;
    const auto overflowing = lay_out_overlay(box, measurer);
    CHECK(overflowing.height > box.h);
    CHECK(overflowing.font_scale == 1.0);

    box.paragraph.autofit = seventhslide::canvas::Autofit::Shrink;
    box.paragraph.margins.top = 10;
    const auto fitted = lay_out_overlay(box, measurer);
    CHECK(fitted.font_scale < 1.0);
    CHECK(fitted.height <= box.h - 10);
    CHECK(fitted.rows.size() < overflowing.rows.size());
    // The type never goes below 8 px: a box too small for that stays overfull.
    box.h = 4;
    const auto tiny = lay_out_overlay(box, measurer);
    CHECK(tiny.font_scale * box.font_size >= 8.0);
}

TEST_CASE("Shrink to fit takes the step the 5% steps tried in turn would",
          "[layout][overlay][paragraph][autofit]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_fixture_fonts(fonts);
    seventhslide::layout::QtTextMeasurer qt_measurer(fonts);
    ApproxTextMeasurer approx_measurer;

    // Each step tried in turn, 5% smaller each time and rounded, until the
    // rows fit or the type is 8 px or under.
    const auto stepwise = [](OverlayBox box, seventhslide::layout::ITextMeasurer& measurer) {
        box.paragraph.autofit = seventhslide::canvas::Autofit::None;
        OverlayLayout out = lay_out_overlay(box, measurer);
        const double avail_h = box.h - box.paragraph.margins.top - box.paragraph.margins.bottom;
        double scale = 1.0;
        int size = box.font_size;
        while (!out.rows.empty() && out.height > avail_h && size > 8) {
            scale *= 0.95;
            OverlayBox smaller = box;
            smaller.font_size = std::max(1, static_cast<int>(std::lround(box.font_size * scale)));
            if (smaller.font_size == size) {
                continue;
            }
            size = smaller.font_size;
            smaller.letter_spacing = box.letter_spacing * scale;
            for (auto& line : smaller.lines) {
                for (auto& span : line) {
                    if (span.style.size_px > 0.0) {
                        span.style.size_px *= scale;
                    }
                }
            }
            out = lay_out_overlay(smaller, measurer);
            out.font_scale = static_cast<double>(size) / box.font_size;
        }
        return out;
    };

    const std::vector<std::vector<const char*>> texts{
        {"Amazing grace how sweet the sound"},
        {"Amazing grace how sweet the sound", "That saved a wretch like me",
         "I once was lost but now am found", "Was blind but now I see"},
        {"<size=150>Big</size> and small words <b>bold</b> mixed in a line that wraps",
         "", "after a blank line"},
        {"Averyveryverylongwordthatwillnotwrapanywhere at all"},
    };
    int compared = 0;
    int shrunk = 0;
    for (auto* measurer : std::vector<seventhslide::layout::ITextMeasurer*>{&approx_measurer,
                                                                           &qt_measurer}) {
        for (const auto& text : texts) {
            for (const int width : {150, 320, 700, 1600}) {
                for (const int height : {20, 60, 130, 260, 500, 900}) {
                    for (const int font_size : {9, 30, 48, 120, 300}) {
                        for (const double tracking : {0.0, 3.0}) {
                            OverlayBox box = box_of(text, width);
                            box.font_family = std::string(seventhslide::test::k_fixture_sans);
                            box.h = height;
                            box.font_size = font_size;
                            box.letter_spacing = tracking;
                            box.paragraph.margins.top = 5;
                            box.paragraph.autofit = seventhslide::canvas::Autofit::Shrink;
                            const auto want = stepwise(box, *measurer);
                            const auto got = lay_out_overlay(box, *measurer);
                            INFO("width " << width << " height " << height << " size "
                                          << font_size << " tracking " << tracking);
                            CHECK(got.font_scale == want.font_scale);
                            CHECK(got == want);
                            ++compared;
                            shrunk += want.font_scale < 1.0 ? 1 : 0;
                        }
                    }
                }
            }
        }
    }
    // Most of them shrink, some as far as they go.
    CHECK(shrunk > compared / 2);
}

namespace {

// Measures as ApproxTextMeasurer does, keeping each type size it was asked
// about: one per size the box was laid out at.
class SizeCountingMeasurer final : public seventhslide::layout::ITextMeasurer {
public:
    [[nodiscard]] double measure_width(std::string_view text, const FontStyle& style) override
    {
        sizes.insert(style.size_px);
        return inner_.measure_width(text, style);
    }
    [[nodiscard]] seventhslide::layout::FontMetrics font_metrics(const FontStyle& style) override
    {
        sizes.insert(style.size_px);
        return inner_.font_metrics(style);
    }

    std::set<double> sizes;

private:
    ApproxTextMeasurer inner_;
};

}  // namespace

TEST_CASE("Shrink to fit lays a box a step or two too full out once or twice more",
          "[layout][overlay][paragraph][autofit]")
{
    ApproxTextMeasurer approx;
    OverlayBox box = box_of({"aa bb cc dd ee ff gg hh ii jj kk ll"}, 300);
    box.font_size = 48;
    const auto height_at = [&](int size) {
        OverlayBox at = box;
        at.font_size = size;
        return lay_out_overlay(at, approx).height;
    };
    // The first two steps down from 48 px.
    const int step0 = static_cast<int>(std::lround(48 * 0.95));
    const int step1 = static_cast<int>(std::lround(48 * 0.95 * 0.95));
    REQUIRE(height_at(48) > height_at(step0));
    REQUIRE(height_at(step0) > height_at(step1));
    box.paragraph.autofit = seventhslide::canvas::Autofit::Shrink;

    for (const auto& [size, layouts] : {std::pair{step0, 2U}, std::pair{step1, 3U}}) {
        box.h = height_at(size);
        SizeCountingMeasurer counting;
        const auto fitted = lay_out_overlay(box, counting);
        INFO("fits at " << size << " px");
        CHECK(fitted.font_scale == static_cast<double>(size) / 48);
        CHECK(counting.sizes.size() == layouts);
    }
}

TEST_CASE("A box with no lines or no area lays out to nothing", "[layout][overlay]")
{
    ApproxTextMeasurer measurer;
    REQUIRE(lay_out_overlay(box_of({}, 500), measurer).rows.empty());
    REQUIRE(lay_out_overlay(box_of({"text"}, 0), measurer).rows.empty());
}

TEST_CASE("A text line's metrics place each run where the one before it ends", "[scene][metrics]")
{
    ApproxTextMeasurer measurer;
    SceneTextBoxStyle box;
    box.font_size = 40;
    box.line_height = 1.5;
    SceneTextLine line;
    seventhslide::model::TextStyle number;
    number.size_pct = 50;
    number.margin_right_pct = 50;
    line.runs = {{"16", number}, {"For God", {}}};

    const auto m = seventhslide::scene::measure_text_line(line, box, {}, measurer);
    // "16" at 20 px is 2 x 10 px, plus a margin of half of 20 px.
    REQUIRE(m.x == std::vector<double>{0.0, 30.0});
    REQUIRE(m.width == 30.0 + 7 * 20.0);
    REQUIRE(m.height == 60.0);
    REQUIRE(m.baseline == 10.0 + 32.0);

    // The active line's own size drives its box.
    line.type.size = 80;
    REQUIRE(seventhslide::scene::measure_text_line(line, box, {}, measurer).height == 120.0);
}

TEST_CASE("A chord line's metrics place each cell and both baselines", "[scene][metrics]")
{
    // The approx measurer's fonts: ascent 0.8, descent 0.2, capitals 0.7 of the size.
    ApproxTextMeasurer measurer;
    SceneTextBoxStyle box;
    box.font_size = 40;
    box.line_height = 1.2;
    SceneTextLine line;
    line.has_chords = true;
    line.chords = seventhslide::layout::parse_chord_line("[G]Amazing [D/F#]it");
    const seventhslide::layout::ChordStyle chords;

    const auto m = seventhslide::scene::measure_text_line(line, box, chords, measurer);
    REQUIRE(m.x.size() == line.chords.items.size());
    REQUIRE(m.x.front() == 0.0);
    for (std::size_t i = 1; i < m.x.size(); ++i) {
        REQUIRE(m.x[i] > m.x[i - 1]);
    }
    // The lyric's own 48 px line box (4 px leading each side) opened up for a
    // 27 px chord row: 30 px chords' 21 px capitals and a 6 px (15%) gap.
    // Above the chords: the leading and the 4 px the lyric font leaves above
    // its capitals.
    REQUIRE(m.height == 48.0 + 27.0);
    REQUIRE(m.chord_baseline == 4.0 + 4.0 + 21.0);
    REQUIRE(m.baseline == 4.0 + 27.0 + 32.0);
}

TEST_CASE("Line spacing goes between chord lines, not between a chord and its words",
          "[scene][metrics][chords]")
{
    ApproxTextMeasurer measurer;
    SceneTextBoxStyle box;
    box.font_size = 40;
    SceneTextLine line;
    line.has_chords = true;
    line.chords = seventhslide::layout::parse_chord_line("[G]Amazing grace");
    // From the chord baseline to the top of the lyric's capitals.
    auto chord_to_lyric = [](const seventhslide::scene::SceneLineMetrics& m) {
        return m.baseline - 28.0 - m.chord_baseline;
    };

    box.line_height = 1.2;
    const auto tight = seventhslide::scene::measure_text_line(line, box, {}, measurer);
    box.line_height = 2.0;
    const auto loose = seventhslide::scene::measure_text_line(line, box, {}, measurer);
    REQUIRE(loose.height == tight.height + 32.0);
    REQUIRE(chord_to_lyric(loose) == chord_to_lyric(tight));
    REQUIRE(chord_to_lyric(tight) == 6.0);

    // Full-size chords with no gap sit right on the lyric's capitals.
    box.line_height = 1.2;
    const auto big = seventhslide::scene::measure_text_line(
        line, box, {.size_pct = 100, .gap_pct = 0, .color = "#ffffff"}, measurer);
    REQUIRE(big.height == 48.0 + 28.0);
    REQUIRE(chord_to_lyric(big) == 0.0);
}

// The approx measurer gives a 40 px lyric 20 px a character and its 30 px
// chords 15 px, so a chord font space (the gap between chords) is 15 px.
TEST_CASE("Chords on a line never run into one another", "[scene][metrics][chords]")
{
    ApproxTextMeasurer measurer;
    SceneTextBoxStyle box;
    box.font_size = 40;
    auto measure = [&](std::string_view markup) {
        SceneTextLine line;
        line.has_chords = true;
        line.chords = seventhslide::layout::parse_chord_line(markup);
        return seventhslide::scene::measure_text_line(line, box, {}, measurer);
    };

    SECTION("chords with room keep their syllables' places")
    {
        const auto m = measure("[G]Amazing [D]grace");
        REQUIRE(m.x == std::vector<double>{0.0, 140.0, 160.0});
        REQUIRE(m.width == 260.0);
    }

    SECTION("a chord with no syllable takes its own width")
    {
        // Am7 is 45 px: D waits for it and the gap.
        const auto m = measure("[Am7][D]Lord");
        REQUIRE(m.x == std::vector<double>{0.0, 60.0});
        REQUIRE(m.width == 140.0);
        REQUIRE(m.hyphens.empty());
    }

    SECTION("a line of chords alone spaces them out")
    {
        const auto m = measure("[G] [Em7] [D]");
        REQUIRE(m.x == std::vector<double>{0.0, 0.0, 30.0, 30.0, 90.0});
        REQUIRE(m.width == 105.0);
        REQUIRE(m.hyphens.empty());
    }

    SECTION("a chord wider than its syllable pushes the next one along")
    {
        // Gsus4 (75 px) over "i" (20 px): "n" and its G move to 90, and a
        // hyphen (20 px) sits in the middle of the 70 px the word opened.
        const auto m = measure("[Gsus4]i[G]n");
        REQUIRE(m.x == std::vector<double>{0.0, 90.0});
        REQUIRE(m.width == 110.0);
        REQUIRE(m.hyphens == std::vector<double>{45.0});
    }

    SECTION("a word opened less than a space gets no hyphen")
    {
        // Am (30 px) is free at 45; "ii" ends at 40.
        const auto m = measure("[Am]ii[G]n");
        REQUIRE(m.x == std::vector<double>{0.0, 45.0});
        REQUIRE(m.hyphens.empty());
    }

    SECTION("a last chord past the lyric counts in the width")
    {
        const auto m = measure("end[Gsus4]");
        REQUIRE(m.x == std::vector<double>{0.0, 60.0});
        REQUIRE(m.width == 135.0);
    }
}

TEST_CASE("Laying out a frame measures its text, overlay and objects once", "[scene][metrics]")
{
    ApproxTextMeasurer measurer;
    seventhslide::scene::SceneFrame frame;
    frame.text.emplace();
    frame.text->box.font_size = 40;
    SceneTextLine line;
    line.runs = parse_inline_markup("one line");
    frame.text->lines.push_back(line);
    frame.overlay.boxes.push_back(box_of({"Title"}, 500));
    seventhslide::scene::SceneObject object;
    object.text = box_of({"Copyright"}, 500);
    frame.objects.push_back(object);

    seventhslide::scene::lay_out_frame_text(frame, measurer);
    REQUIRE(frame.text->lines[0].metrics.has_value());
    REQUIRE(frame.overlay.boxes[0].layout.has_value());
    REQUIRE(frame.objects[0].text->layout.has_value());

    // What is already measured is left as it is.
    frame.overlay.boxes[0].layout->height = 12345.0;
    seventhslide::scene::lay_out_frame_text(frame, measurer);
    REQUIRE(frame.overlay.boxes[0].layout->height == 12345.0);
}

TEST_CASE("A live frame arrives measured", "[scene][metrics]")
{
    ApproxTextMeasurer measurer;
    seventhslide::session::Session session(measurer);
    seventhslide::model::OutputConfig main;
    main.name = "Main";
    (void)seventhslide::theme::seed_default_themes(main);
    session.set_outputs({main});
    seventhslide::model::Song song;
    song.title = "Measured";
    song.lyrics = "[V1]\nAmazing grace how sweet the sound\nThat saved a wretch like me";
    session.play_song(song, std::string(seventhslide::model::k_default_stack_id));

    bool saw_text = false;
    for (int i = 0; i < 3; ++i) {
        const auto frame = session.scene_frame(session.outputs()[0].id);
        REQUIRE(frame.has_value());
        if (frame->text) {
            for (const auto& line : frame->text->lines) {
                REQUIRE(line.metrics.has_value());
                saw_text = true;
            }
        }
        for (const auto& box : frame->overlay.boxes) {
            REQUIRE(box.layout.has_value());
        }
        for (const auto& object : frame->objects) {
            if (object.text) {
                REQUIRE(object.text->layout.has_value());
            }
        }
        (void)session.focused_stack().program.next_slide();
    }
    REQUIRE(saw_text);
}

TEST_CASE("A chord word wider than a box is cut to fit it", "[scene][metrics][chords]")
{
    // 40 px characters are 20 px wide: five to a 100 px row.
    ApproxTextMeasurer measurer;
    auto box = box_of({"[G]Hallelujah [D]amen"}, 100);
    box.show_chords = true;
    const auto layout = lay_out_overlay(box, measurer);
    REQUIRE(layout.rows.size() == 3);
    CHECK(row_text(layout.rows[0]) == "Halle");
    CHECK(row_text(layout.rows[1]) == "lujah");
    CHECK(row_text(layout.rows[2]) == "amen");
    REQUIRE(layout.rows[0].chords.size() == 1);
    CHECK(layout.rows[0].chords[0].text == "G");
    CHECK(layout.rows[1].chords.empty());
    REQUIRE(layout.rows[2].chords.size() == 1);
    CHECK(layout.rows[2].chords[0].text == "D");
    for (const auto& row : layout.rows) {
        CHECK(row.width <= 100);
    }
}
