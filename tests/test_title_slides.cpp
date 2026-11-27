#include <catch2/catch_test_macros.hpp>

#include "scene_test_helpers.hpp"
#include "theme_test_helpers.hpp"

#include <seventhslide/build/song_layout.hpp>
#include <seventhslide/build/title_slides.hpp>
#include <seventhslide/layout/text_measurer.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using seventhslide::layout::FontStyle;
using seventhslide::layout::ITextMeasurer;
using seventhslide::build::LayoutResult;
using seventhslide::layout::SlideGroup;
using seventhslide::build::apply_title_slide_layout;
using seventhslide::build::apply_title_upcoming;
using seventhslide::build::compute_song_layout;
using seventhslide::build::prepend_title_line;
using seventhslide::build::should_prepend_title_line;
using seventhslide::build::should_show_title_upcoming;
using seventhslide::build::title_slide_mode_active;
using seventhslide::build::verse_order_allows_title_slide;
using seventhslide::model::OutputConfig;
using seventhslide::test::load_json;

namespace {

std::filesystem::path fixtures_dir()
{
    return seventhslide::test::fixtures_dir("layout");
}

using seventhslide::test::LenTextMeasurer;

void require_groups(const nlohmann::json& exp, const std::vector<SlideGroup>& groups)
{
    seventhslide::test::require_slide_groups(exp, groups);
}

LayoutResult result_from_base_json(const nlohmann::json& base)
{
    LayoutResult r;
    r.verse_codes = base.at("verse_codes").get<std::vector<std::string>>();
    const auto texts = base.at("all_lines").get<std::vector<std::string>>();
    const auto labels = base.at("line_labels").get<std::vector<std::string>>();
    const auto vidx = base.at("verse_indices").get<std::vector<int>>();
    REQUIRE(texts.size() == labels.size());
    REQUIRE(texts.size() == vidx.size());
    for (std::size_t i = 0; i < texts.size(); ++i) {
        seventhslide::build::LayoutLine line;
        line.text = texts[i];
        line.label = labels[i];
        line.verse_index = vidx[i];
        r.lines.push_back(std::move(line));
    }
    for (const auto& g : base.at("groups")) {
        SlideGroup sg;
        sg.indices = g.at("indices").get<std::vector<int>>();
        sg.active_count = g.at("active_count").get<int>();
        sg.is_title = g.value("is_title", false);
        r.groups.push_back(std::move(sg));
    }
    return r;
}

}  // namespace

TEST_CASE("title slide policy helpers", "[layout][title]")
{
    REQUIRE(verse_order_allows_title_slide(""));
    REQUIRE(verse_order_allows_title_slide("   "));
    REQUIRE(verse_order_allows_title_slide("t1 v1"));
    REQUIRE(verse_order_allows_title_slide("T1 V1"));
    REQUIRE_FALSE(verse_order_allows_title_slide("v1"));
    REQUIRE_FALSE(verse_order_allows_title_slide("v1 c1"));

    REQUIRE(title_slide_mode_active(""));
    REQUIRE_FALSE(title_slide_mode_active("v1"));
    REQUIRE_FALSE(title_slide_mode_active("v1 c1"));
    REQUIRE(title_slide_mode_active("t1 v1"));
    REQUIRE(title_slide_mode_active("T1 V1"));

    REQUIRE(should_prepend_title_line(true, ""));
    REQUIRE_FALSE(should_prepend_title_line(true, "t1 v1"));
    REQUIRE_FALSE(should_prepend_title_line(false, ""));

    seventhslide::theme::ResolvedTheme theme;
    theme.behavior.follow_lines = 2;
    theme.behavior.title_slide_show_lines = true;
    REQUIRE(should_show_title_upcoming(true, theme, false));
    REQUIRE_FALSE(should_show_title_upcoming(true, theme, true));
    theme.behavior.follow_lines = 0;
    REQUIRE_FALSE(should_show_title_upcoming(true, theme, false));
    theme.behavior.follow_lines = 2;
    theme.behavior.title_slide_show_lines = false;
    REQUIRE_FALSE(should_show_title_upcoming(true, theme, false));
}

TEST_CASE("prepend_title_line matches the golden fixture", "[layout][title]")
{
    const auto doc = load_json(fixtures_dir() / "title_slides.expected.json");
    for (const auto& c : doc) {
        const auto id = c.at("id").get<std::string>();
        if (id != "prepend_blank" && id != "prepend_upcoming") {
            continue;
        }
        INFO(id);
        auto result = result_from_base_json(c.at("base"));
        prepend_title_line(result, c.at("show_upcoming").get<bool>());
        const auto& exp = c.at("result");
        std::vector<std::string> texts;
        std::vector<std::string> labels;
        std::vector<int> vidx;
        for (const auto& line : result.lines) {
            texts.push_back(line.text);
            labels.push_back(line.label);
            vidx.push_back(line.verse_index);
        }
        REQUIRE(texts == exp.at("all_lines").get<std::vector<std::string>>());
        REQUIRE(labels == exp.at("line_labels").get<std::vector<std::string>>());
        REQUIRE(vidx == exp.at("verse_indices").get<std::vector<int>>());
        REQUIRE(result.verse_codes == exp.at("verse_codes").get<std::vector<std::string>>());
        require_groups(exp.at("groups"), result.groups);
        REQUIRE(result.lines.front().is_title);
    }
}

TEST_CASE("apply_title_upcoming matches the golden fixture", "[layout][title]")
{
    const auto doc = load_json(fixtures_dir() / "title_slides.expected.json");
    for (const auto& c : doc) {
        const auto id = c.at("id").get<std::string>();
        if (id != "apply_upcoming_explicit_t1" && id != "apply_upcoming_title_only") {
            continue;
        }
        INFO(id);
        LayoutResult result;
        if (c.contains("all_lines")) {
            for (std::size_t i = 0; i < c.at("all_lines").size(); ++i) {
                seventhslide::build::LayoutLine line;
                line.text = c.at("all_lines")[i].get<std::string>();
                line.label = c.at("line_labels")[i].get<std::string>();
                result.lines.push_back(std::move(line));
            }
        } else {
            seventhslide::build::LayoutLine line;
            line.label = "t1";
            line.is_title = true;
            result.lines.push_back(std::move(line));
        }
        for (const auto& g : c.at("base_groups")) {
            SlideGroup sg;
            sg.indices = g.at("indices").get<std::vector<int>>();
            sg.active_count = g.at("active_count").get<int>();
            sg.is_title = g.value("is_title", false);
            result.groups.push_back(std::move(sg));
        }
        apply_title_upcoming(result);
        require_groups(c.at("result_groups"), result.groups);
    }
}

TEST_CASE("apply_title_slide_layout end-to-end prepend", "[layout][title]")
{
    LenTextMeasurer measurer;
    const auto theme = seventhslide::test::resolved_from_flat({
        {"width_px", 200},
        {"height_px", 200},
        {"font_size", 10},
        {"font_family", "X"},
        {"follow_lines", 0},
        {"balance_wrapped_lines", false},
    });

    // Empty verse_order + title mode -> suppress titles + prepend
    auto result = compute_song_layout(
        theme, measurer, "---[Verse:1]---\nHello\nWorld\n", "", true);
    apply_title_slide_layout(result, theme, "", true, true);
    REQUIRE(result.lines.size() == 3);
    REQUIRE(result.lines[0].label == "t1");
    REQUIRE(result.groups.front().is_title);
    REQUIRE(result.groups.front().indices == std::vector<int>{0});
}
