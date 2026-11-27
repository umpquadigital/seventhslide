#include <catch2/catch_test_macros.hpp>

#include "fixture_files.hpp"
#include <catch2/catch_approx.hpp>

#include "layout_test_helpers.hpp"
#include "theme_test_helpers.hpp"

#include <seventhslide/build/even_wrap.hpp>
#include <seventhslide/build/song_layout.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/layout/word_wrap.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/theme/resolved_theme.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

using nlohmann::json;
using seventhslide::build::LayoutLine;
using seventhslide::build::LayoutResult;
using seventhslide::layout::SlideGroup;
using seventhslide::layout::WidthMeasurer;
using seventhslide::build::balance_strength_from_percent;
using seventhslide::build::even_one_slide;
using seventhslide::build::even_slide_lines;
using seventhslide::build::min_even_width;
using seventhslide::build::search_even_target;
using seventhslide::build::topheavy_even_width;
using seventhslide::layout::wrap_marked_text_to_width;
using seventhslide::build::apply_even_wrap_if_enabled;
using seventhslide::build::compute_song_layout;

namespace {

/// One unit per character (plain words).
double len_measure(std::string_view s)
{
    return static_cast<double>(s.size());
}

/// One unit per character of each run: how the Python goldens measure marked
/// lines (tags take no width).
double run_len(std::string_view s, const seventhslide::model::TextStyle& /*style*/)
{
    return static_cast<double>(s.size());
}

std::filesystem::path fixtures_dir()
{
    return seventhslide::test::fixtures_dir("layout");
}

using seventhslide::test::load_json;

/// Fragments joined with ``<br/>``, the form the golden files record.
std::string join_wrap_fragments_html(const std::vector<std::string>& fragments)
{
    std::string out;
    for (std::size_t i = 0; i < fragments.size(); ++i) {
        if (i > 0) {
            out += "<br/>";
        }
        out += fragments[i];
    }
    return out;
}

std::string display_line(const LayoutLine& ln)
{
    if (!ln.wrap_fragments.empty()) {
        return join_wrap_fragments_html(ln.wrap_fragments);
    }
    return ln.text;
}

LayoutLine meta_line(const json& m)
{
    LayoutLine ln;
    ln.text = m.at("plain").get<std::string>();
    ln.line_height_px = m.at("lh").get<int>();
    ln.extra_px = m.at("extra").get<int>();
    ln.evenable = m.at("ev").get<bool>();
    return ln;
}

}  // namespace

TEST_CASE("wrap_word_runs and wrap_marked_text match the golden fixture", "[layout][even]")
{
    const auto doc = load_json(fixtures_dir() / "even_wrap.expected.json");

    const auto& gr = doc.at("greedy_runs");
    std::string text;
    for (const auto& w : gr.at("words")) {
        text += (text.empty() ? "" : " ") + w.get<std::string>();
    }
    const auto words = seventhslide::layout::PlainWords(text);
    REQUIRE(words.size() == gr.at("words").size());
    const auto runs = seventhslide::layout::wrap_word_runs(words, 0, words.size(), len_measure,
                                                           gr.at("box_w").get<double>());
    REQUIRE(runs.size() == gr.at("runs").size());
    for (std::size_t i = 0; i < runs.size(); ++i) {
        REQUIRE(runs[i].start == gr.at("runs")[i].at("start").get<std::size_t>());
        REQUIRE(runs[i].end == gr.at("runs")[i].at("end").get<std::size_t>());
    }

    for (const auto& c : doc.at("wrap_html")) {
        INFO(c.at("text").get<std::string>());
        const auto fr = wrap_marked_text_to_width(
            c.at("text").get<std::string>(), run_len, c.at("w").get<double>());
        REQUIRE(static_cast<int>(fr.size()) == c.at("n").get<int>());
        REQUIRE(join_wrap_fragments_html(fr) == c.at("html").get<std::string>());
    }
}

TEST_CASE("min_even / topheavy widths match the golden fixture", "[layout][even]")
{
    const auto doc = load_json(fixtures_dir() / "even_wrap.expected.json");
    for (const auto& c : doc.at("widths")) {
        INFO(c.at("plain").get<std::string>());
        const auto plain = c.at("plain").get<std::string>();
        const double box_w = c.at("box_w").get<double>();
        const auto words = seventhslide::layout::StyledWords::marked(plain);
        REQUIRE(min_even_width(words, run_len, box_w)
                == Catch::Approx(c.at("min_even").get<double>()).margin(1e-9));
        REQUIRE(topheavy_even_width(words, run_len, box_w)
                == Catch::Approx(c.at("topheavy").get<double>()).margin(1e-9));
    }
}

TEST_CASE("search_even_target matches the golden fixture", "[layout][even]")
{
    const auto doc = load_json(fixtures_dir() / "even_wrap.expected.json");
    const auto& s = doc.at("search");
    auto h = [](double t) { return 50.0 + (20.0 - t) * 5.0; };
    REQUIRE(search_even_target(
                s.at("ideal").get<double>(),
                s.at("box_w").get<double>(),
                s.at("cap").get<double>(),
                h)
            == Catch::Approx(s.at("result").get<double>()).margin(1e-9));
    REQUIRE(search_even_target(
                s.at("ideal").get<double>(),
                s.at("box_w").get<double>(),
                200.0,
                h)
            == Catch::Approx(s.at("result_when_ideal_fits").get<double>()).margin(1e-9));
}

TEST_CASE("even_one_slide matches the golden fixture", "[layout][even]")
{
    const auto doc = load_json(fixtures_dir() / "even_wrap.expected.json");
    for (const auto& c : doc.at("even_one")) {
        INFO(c.at("name").get<std::string>());
        std::vector<LayoutLine> lines;
        for (const auto& m : c.at("input")) {
            lines.push_back(meta_line(m));
        }
        std::vector<int> idxs;
        for (const auto& i : c.at("indices")) {
            idxs.push_back(i.get<int>());
        }
        even_one_slide(
            idxs,
            lines,
            run_len,
            c.at("box_w").get<double>(),
            c.at("cap").get<double>(),
            c.at("strength").get<double>());
        REQUIRE(lines.size() == c.at("output").size());
        for (std::size_t i = 0; i < lines.size(); ++i) {
            REQUIRE(display_line(lines[i]) == c.at("output")[i].get<std::string>());
        }
    }
}

TEST_CASE("even_slide_lines matches the golden fixture", "[layout][even]")
{
    const auto doc = load_json(fixtures_dir() / "even_wrap.expected.json");
    const auto& c = doc.at("even_slides");
    LayoutResult result;
    for (const auto& t : c.at("input")) {
        LayoutLine ln;
        ln.text = t.get<std::string>();
        ln.line_height_px = c.at("line_meta_lh").get<int>();
        ln.evenable = true;
        result.lines.push_back(std::move(ln));
    }
    for (const auto& g : c.at("groups")) {
        SlideGroup grp;
        for (const auto& i : g.at("indices")) {
            grp.indices.push_back(i.get<int>());
        }
        result.groups.push_back(std::move(grp));
    }
    even_slide_lines(
        result,
        run_len,
        c.at("box_w").get<double>(),
        c.at("cap").get<double>(),
        c.at("strength").get<double>());
    REQUIRE(result.lines.size() == c.at("output").size());
    for (std::size_t i = 0; i < result.lines.size(); ++i) {
        REQUIRE(display_line(result.lines[i]) == c.at("output")[i].get<std::string>());
    }
}

TEST_CASE("balance_strength_from_percent and strength scale", "[layout][even]")
{
    REQUIRE(balance_strength_from_percent(100) == Catch::Approx(1.0));
    REQUIRE(balance_strength_from_percent(0) == Catch::Approx(0.0));
    REQUIRE(balance_strength_from_percent(50) == Catch::Approx(0.5));
    REQUIRE(balance_strength_from_percent(150) == Catch::Approx(1.0));
    REQUIRE(balance_strength_from_percent(-10) == Catch::Approx(0.0));

    const auto doc = load_json(fixtures_dir() / "even_wrap.expected.json");
    for (const auto& c : doc.at("strength_scale")) {
        const double box_w = c.at("box_w").get<double>();
        const double target = c.at("target").get<double>();
        const double strength = c.at("strength").get<double>();
        REQUIRE(box_w - strength * (box_w - target)
                == Catch::Approx(c.at("scaled").get<double>()).margin(1e-12));
    }
}

TEST_CASE("apply_even_wrap_if_enabled respects gates", "[layout][even]")
{
    LayoutResult result;
    LayoutLine ln;
    ln.text = "aaaaa bbbbb ccccc ddddd eeeee fffff";
    ln.line_height_px = 10;
    ln.evenable = true;
    result.lines.push_back(ln);
    SlideGroup g;
    g.indices = {0};
    result.groups.push_back(g);

    seventhslide::theme::ResolvedTheme theme;
    theme.behavior.balance_wrapped_lines = false;
    theme.behavior.balance_wrapped_strength = 100;
    theme.behavior.follow_lines = 0;
    apply_even_wrap_if_enabled(result, theme, run_len, 20, 100);
    REQUIRE(result.lines[0].wrap_fragments.empty());

    theme.behavior.balance_wrapped_lines = true;
    theme.behavior.follow_lines = 2;
    apply_even_wrap_if_enabled(result, theme, run_len, 20, 100);
    REQUIRE(result.lines[0].wrap_fragments.empty());

    theme.behavior.follow_lines = 0;
    theme.behavior.balance_wrapped_strength = 0;
    apply_even_wrap_if_enabled(result, theme, run_len, 20, 100);
    REQUIRE(result.lines[0].wrap_fragments.empty());

    theme.behavior.balance_wrapped_strength = 100;
    apply_even_wrap_if_enabled(result, theme, run_len, 20, 100);
    REQUIRE_FALSE(result.lines[0].wrap_fragments.empty());
}

TEST_CASE("compute_song_layout applies even wrap when enabled", "[layout][even]")
{
    // Len-based measurer: font_size drives css line height; widths = char count.
    struct LenTextMeasurer : seventhslide::layout::ITextMeasurer {
        [[nodiscard]] double measure_width(
            std::string_view text, const seventhslide::layout::FontStyle&) override
        {
            return static_cast<double>(text.size());
        }
    } measurer;

    auto theme = seventhslide::test::resolved_from_flat({
        {"width_px", 20},
        {"height_px", 200},
        {"font_size", 10},
        {"follow_lines", 0},
        {"balance_wrapped_lines", true},
        {"balance_wrapped_strength", 100},
    });

    const std::string lyrics = "---[V1]---\naaaaa bbbbb ccccc ddddd eeeee fffff\n";
    auto result = compute_song_layout(theme, measurer, lyrics, "v1");
    REQUIRE_FALSE(result.lines.empty());
    bool any_baked = false;
    for (const auto& ln : result.lines) {
        if (!ln.wrap_fragments.empty()) {
            any_baked = true;
            REQUIRE(ln.wrap_fragments.size() > 1);
        }
    }
    REQUIRE(any_baked);

    theme.behavior.balance_wrapped_lines = false;
    result = compute_song_layout(theme, measurer, lyrics, "v1");
    // Balance off -> evening skipped, but full-box wrap still bakes fragments so
    // paint gets visual lines (native has no CSS word-wrap).
    bool any_box = false;
    for (const auto& ln : result.lines) {
        if (!ln.wrap_fragments.empty()) {
            any_box = true;
            REQUIRE(ln.wrap_fragments.size() > 1);
        }
    }
    REQUIRE(any_box);
}

// A wrapped line used to re-open only <b>/<i>/<u> on its next row, and
// cut "<font=Open Sans>" at the space into literal text.
TEST_CASE("Wrapped lines keep every inline style", "[layout][wrap]")
{
    seventhslide::layout::ApproxTextMeasurer m;
    seventhslide::layout::FontStyle style;
    style.size_px = 10;
    const auto measure = seventhslide::layout::bind_run_measurer(m, style);

    for (const std::string marked : {"<color=#ff0000>one two three four five six</color>",
                                     "<font=Open Sans>one two three four five six</font>"}) {
        INFO(marked);
        const auto fragments = seventhslide::layout::wrap_marked_text_to_width(marked, measure, 50);
        REQUIRE(fragments.size() > 1);
        for (const auto& fragment : fragments) {
            INFO(fragment);
            const auto spans = seventhslide::model::parse_inline_markup(fragment);
            REQUIRE_FALSE(spans.empty());
            for (const auto& span : spans) {
                CHECK(span.text.find('<') == std::string::npos);
                CHECK((span.style.color == "#ff0000" || span.style.font_family == "Open Sans"));
            }
        }
    }
}

// Words were measured in the box font whatever their own size, so a
// larger word could run past the box edge.
TEST_CASE("Styled words are measured in their own font", "[layout][wrap]")
{
    seventhslide::layout::ApproxTextMeasurer m;  // 0.5 x size px per character
    seventhslide::layout::FontStyle style;
    style.size_px = 10;
    const auto measure = seventhslide::layout::bind_run_measurer(m, style);

    // "aa" 10 + " " 5 + "bb" at 200% 20 = 35: two rows in 30, though the
    // same words in the box font (25) would fit on one.
    const auto words = seventhslide::layout::StyledWords::marked("aa <size=200>bb</size>");
    REQUIRE(words.width(0, words.size(), measure) == Catch::Approx(35.0));
    const auto rows =
        seventhslide::layout::wrap_marked_text_to_width("aa <size=200>bb</size>", measure, 30);
    REQUIRE(rows == std::vector<std::string>{"aa", "<size=200>bb</size>"});
}

// The overflow split measured tags as glyphs and could cut inside one.
TEST_CASE("A line split to fit keeps its markup whole", "[layout][wrap]")
{
    seventhslide::layout::ApproxTextMeasurer m;
    seventhslide::layout::FontStyle style;
    style.size_px = 10;
    const auto measure = seventhslide::layout::bind_run_measurer(m, style);

    const auto pieces = seventhslide::test::split_marked_line_to_fit(
        "<font=Open Sans>one two three four</font>, <b>five six seven eight</b>", measure,
        12, 60, 12);
    REQUIRE(pieces.size() > 1);
    for (const auto& piece : pieces) {
        INFO(piece);
        for (const auto& span : seventhslide::model::parse_inline_markup(piece)) {
            CHECK(span.text.find('<') == std::string::npos);
            CHECK((span.style.font_family == "Open Sans" || span.style.bold
                   || span.text == ","));
        }
    }
}
