#include <catch2/catch_test_macros.hpp>

#include "font_fixtures.hpp"
#include "test_qt_app.hpp"
#include "scene_test_helpers.hpp"
#include "theme_test_helpers.hpp"

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/build/song_layout.hpp>
#include <seventhslide/fonts/font_registry.hpp>
#include <seventhslide/layout/qt_text_measurer.hpp>
#include <seventhslide/layout/slide_grouper.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/layout/word_wrap.hpp>
#include <seventhslide/scene/scene_builder.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::layout::ITextMeasurer;
using seventhslide::layout::FontStyle;
using seventhslide::layout::SlideGrouper;
using seventhslide::layout::build_line_to_slide;
using seventhslide::build::compute_song_layout;
using seventhslide::test::resolved_from_flat;
using seventhslide::test::load_json;

namespace {

std::filesystem::path fixtures_dir()
{
    return seventhslide::test::fixtures_dir("layout");
}

using seventhslide::test::LenTextMeasurer;

void require_groups_match(const nlohmann::json& exp, const auto& groups)
{
    seventhslide::test::require_slide_groups(exp, groups);
}

}  // namespace

TEST_CASE("SlideGrouper matches the golden packing fixture", "[layout][grouper]")
{
    const auto doc = load_json(fixtures_dir() / "slide_grouper.expected.json");
    for (const auto& case_j : doc) {
        INFO(case_j.at("name").get<std::string>());
        SlideGrouper g(
            case_j.at("max_visual_px").get<int>(),
            case_j.at("follow_lines").get<int>(),
            case_j.at("prevent_mixed_active").get<bool>());
        int i = 0;
        for (const auto& line : case_j.at("lines")) {
            g.add_line(
                i++,
                line.at("base").get<int>(),
                line.at("active").get<int>(),
                line.at("verse").get<int>());
        }
        g.flush_buffer();
        require_groups_match(case_j.at("groups"), g.groups());
    }
}

TEST_CASE("Active lines that do not fit wait for the next slide", "[layout][grouper][follow]")
{
    // Two active lines a slide, 100 px high: the second cannot join the first.
    SlideGrouper g(100, 2, false);
    g.add_line(0, 80, 80, 0);
    g.add_line(1, 80, 80, 0);
    g.add_line(2, 10, 10, 0);
    g.add_line(3, 10, 10, 0);
    g.flush_buffer();

    const auto& groups = g.groups();
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].indices == std::vector<int>{0});
    CHECK(groups[0].active_count == 1);
    CHECK(groups[1].indices == std::vector<int>{1, 2, 3});
    CHECK(groups[1].active_count == 2);
    CHECK(groups[2].indices == std::vector<int>{3});
    CHECK(groups[2].active_count == 1);

    // Too big as an active line, it can still show following in the room left.
    SlideGrouper bigger(100, 2, false);
    bigger.add_line(0, 50, 60, 0);
    bigger.add_line(1, 30, 60, 0);
    bigger.add_line(2, 30, 60, 0);
    bigger.flush_buffer();
    REQUIRE(bigger.groups().size() == 3);
    CHECK(bigger.groups()[0].indices == std::vector<int>{0, 1});
    CHECK(bigger.groups()[0].active_count == 1);
    CHECK(bigger.groups()[1].indices == std::vector<int>{1, 2});
    CHECK(bigger.groups()[1].active_count == 1);
    CHECK(bigger.groups()[2].indices == std::vector<int>{2});
}

TEST_CASE("build_line_to_slide uses last group containing line", "[layout][grouper]")
{
    std::vector<seventhslide::layout::SlideGroup> groups{
        {.indices = {0, 1}, .active_count = 1},
        {.indices = {1, 2}, .active_count = 1},
        {.indices = {2}, .active_count = 1},
    };
    REQUIRE(build_line_to_slide(groups, 3) == std::vector<int>{0, 1, 2});
}

TEST_CASE("compute_song_layout matches the golden fixture", "[layout][grouper]")
{
    const auto doc = load_json(fixtures_dir() / "song_layout.expected.json");
    LenTextMeasurer measurer;

    for (const auto& case_j : doc) {
        INFO(case_j.at("id").get<std::string>());
        const auto& opts = case_j.at("opts");
        nlohmann::json look = {
            {"font_family", "X"},
            {"balance_wrapped_lines", false},
            {"width_px", opts.contains("width_px") ? opts.at("width_px").get<int>() : 200},
            {"height_px", opts.contains("height_px") ? opts.at("height_px").get<int>() : 200},
            {"font_size", opts.contains("font_size") ? opts.at("font_size").get<int>() : 10},
        };
        for (const char* key : {"follow_lines", "fluid_slides", "verse_gap",
                                "prevent_mixed_active", "show_chords"}) {
            if (opts.contains(key)) {
                look[key] = opts.at(key);
            }
        }
        const auto theme = resolved_from_flat(look);

        const auto result = compute_song_layout(
            theme,
            measurer,
            case_j.at("lyrics").get<std::string>(),
            case_j.at("verse_order").get<std::string>(),
            case_j.at("suppress_titles").get<bool>());

        const auto& exp = case_j.at("result");
        REQUIRE(result.verse_codes == exp.at("verse_codes").get<std::vector<std::string>>());

        std::vector<std::string> texts;
        std::vector<int> verse_indices;
        std::vector<std::string> labels;
        for (const auto& line : result.lines) {
            texts.push_back(line.text);
            verse_indices.push_back(line.verse_index);
            labels.push_back(line.label);
        }
        REQUIRE(texts == exp.at("all_lines").get<std::vector<std::string>>());
        REQUIRE(verse_indices == exp.at("verse_indices").get<std::vector<int>>());
        REQUIRE(labels == exp.at("line_labels").get<std::vector<std::string>>());
        require_groups_match(exp.at("groups"), result.groups);
        REQUIRE(result.line_to_slide.size() == result.lines.size());
    }
}

TEST_CASE("compute_song_layout works with ApproxTextMeasurer", "[layout][grouper]")
{
    ApproxTextMeasurer approx;
    const auto theme = resolved_from_flat({
        {"width_px", 400},
        {"height_px", 300},
        {"font_size", 20},
        {"font_family", "X"},
    });
    const auto result = compute_song_layout(
        theme, approx, "---[Verse:1]---\nHello\nWorld\n", "v1");
    REQUIRE(result.lines.size() == 2);
    REQUIRE_FALSE(result.groups.empty());
}

TEST_CASE("Main theme Holy line wraps at EB Garamond 110", "[layout][wrap][regression]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);

    const auto theme = resolved_from_flat({
        {"width_px", 1690},
        {"height_px", 600},
        {"font_family", "EB Garamond"},
        {"font_size", 110},
        {"follow_lines", 0},
        {"balance_wrapped_lines", false},
        {"box_x", 115},
        {"box_y", 226},
        {"highlight_color", "#ffffff"},
        {"align", "center"},
        {"valign", "center"},
    });

    const std::string line =
        "Early in the morning our song shall rise to Thee;";
    const std::string lyrics = "---[Verse:1]---\n" + line + "\n";

    const auto result = compute_song_layout(theme, measurer, lyrics, "v1");
    REQUIRE_FALSE(result.lines.empty());

    const auto& ln = result.lines[0];
    REQUIRE(ln.text.find("Early in the morning") != std::string::npos);

    // avail_w = full box width (padding removed). At 110px this lyric must wrap.
    const auto measure = seventhslide::layout::bind_measurer(
        measurer,
        seventhslide::layout::FontStyle{.family = "EB Garamond", .size_px = 110});
    const double full_w = measure(line);
    INFO("full_w=" << full_w << " avail=1690 fragments=" << ln.wrap_fragments.size());
    REQUIRE(full_w > 1690.0);
    REQUIRE(ln.wrap_fragments.size() > 1);

    // Scene must emit multiple visual lines.
    seventhslide::build::OutputRuntime rt;
    rt.name = "Main";
    rt.layout = result;
    rt.theme = theme;
    // One group covering the line.
    REQUIRE_FALSE(result.groups.empty());

    seventhslide::scene::SceneFrameRequest req{
        rt, seventhslide::scene::SceneMode::Song, 1920, 1080, 0, {}, &measurer};
    auto frame = seventhslide::scene::build_scene_frame(req);
    REQUIRE(frame.text.has_value());
    REQUIRE(frame.text->lines.size() > 1);
}

TEST_CASE("needs_verse_gap separates verses only when fluid slides ask for it", "[layout][song]")
{
    using seventhslide::build::needs_verse_gap;
    seventhslide::build::LayoutResult layout;
    layout.verse_codes = {"v1a", "v1b", "c1"};
    for (int verse : {0, 1, 2}) {
        seventhslide::build::LayoutLine line;
        line.verse_index = verse;
        layout.lines.push_back(line);
    }

    REQUIRE(needs_verse_gap(layout, true, 20, 2, 1));         // v1b -> c1
    REQUIRE_FALSE(needs_verse_gap(layout, true, 20, 1, 0));   // sections of one verse
    REQUIRE_FALSE(needs_verse_gap(layout, false, 20, 2, 1));  // fluid slides off
    REQUIRE_FALSE(needs_verse_gap(layout, true, 0, 2, 1));    // no gap configured
    REQUIRE_FALSE(needs_verse_gap(layout, true, 20, 3, 1));   // out of range
}
