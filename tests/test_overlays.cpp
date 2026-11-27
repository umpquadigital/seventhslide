#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "fixture_files.hpp"

#include <seventhslide/layout/overlays.hpp>
#include <seventhslide/model/content.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using nlohmann::json;
using seventhslide::layout::OverlayBox;
using seventhslide::layout::OverlayScene;
using seventhslide::model::RichLine;
using seventhslide::model::TextSpan;
using seventhslide::model::plain_text;
using seventhslide::layout::SlideGroup;
using seventhslide::layout::song_token_values;
using seventhslide::layout::assign_overlay_to_title_slides;
using seventhslide::layout::build_overlay_scene;
using seventhslide::layout::build_title_overlay_scene;
using seventhslide::layout::substitute_variables;
using seventhslide::layout::token_key;

namespace {

std::filesystem::path fixtures_dir()
{
    return seventhslide::test::fixtures_dir("layout");
}

using seventhslide::test::load_json;

TextSpan span_from_json(const json& src)
{
    TextSpan sp;
    sp.text = src.value("text", "");
    sp.style.size_pct = src.value("scale", 100);
    sp.style.bold = src.value("bold", false);
    sp.style.italic = src.value("italic", false);
    sp.style.underline = src.value("underline", false);
    sp.style.color = src.value("color", "");
    return sp;
}

/// A template box in the fixture's JSON shape.
OverlayBox box_from_json(const json& j)
{
    OverlayBox b;
    b.x = j.value("x", 0.0);
    b.y = j.value("y", 0.0);
    b.w = j.value("w", 100.0);
    b.h = j.value("h", 100.0);
    b.font_family = j.value("font_family", "Helvetica");
    b.font_size = j.value("font_size", 48);
    b.font_color = j.value("font_color", "#ffffff");
    b.text_align = j.value("text_align", "center");
    b.vertical_align = j.value("vertical_align", "middle");
    b.line_height = j.value("line_height", 1.15);
    b.line_gap = j.value("line_gap", 0);
    b.letter_spacing = j.value("letter_spacing", 0.0);
    for (const auto& lj : j.value("lines", json::array())) {
        RichLine ln;
        if (lj.is_string()) {
            ln.push_back(span_from_json(json{{"text", lj.get<std::string>()}}));
        } else if (lj.value("spans", json::array()).empty()) {
            ln.push_back(span_from_json(lj));
        } else {
            for (const auto& sj : lj.at("spans")) {
                ln.push_back(span_from_json(sj));
            }
        }
        b.lines.push_back(std::move(ln));
    }
    return b;
}

void require_scene_eq(const OverlayScene& scene, const json& expected)
{
    REQUIRE(scene.boxes.size() == expected.at("boxes").size());
    for (std::size_t i = 0; i < scene.boxes.size(); ++i) {
        const auto& b = scene.boxes[i];
        const auto& ej = expected.at("boxes")[i];
        REQUIRE(b.x == Catch::Approx(ej.at("x").get<double>()));
        REQUIRE(b.y == Catch::Approx(ej.at("y").get<double>()));
        REQUIRE(b.w == Catch::Approx(ej.at("w").get<double>()));
        REQUIRE(b.h == Catch::Approx(ej.at("h").get<double>()));
        REQUIRE(b.font_family == ej.at("font_family").get<std::string>());
        REQUIRE(b.font_size == ej.at("font_size").get<int>());
        REQUIRE(b.font_color == ej.at("font_color").get<std::string>());
        REQUIRE(b.text_align == ej.at("text_align").get<std::string>());
        REQUIRE(b.vertical_align == ej.at("vertical_align").get<std::string>());
        REQUIRE(b.line_height == Catch::Approx(ej.at("line_height").get<double>()));
        REQUIRE(b.line_gap == ej.at("line_gap").get<int>());
        REQUIRE(b.lines.size() == ej.at("lines").size());
        for (std::size_t li = 0; li < b.lines.size(); ++li) {
            const auto& ln = b.lines[li];
            const auto& el = ej.at("lines")[li];
            REQUIRE(plain_text(ln) == el.at("text").get<std::string>());
            REQUIRE_FALSE(ln.empty());
            const auto& st = ln.front().style;
            REQUIRE(st.size_pct == el.at("scale").get<int>());
            REQUIRE(st.bold == el.at("bold").get<bool>());
            REQUIRE(st.italic == el.at("italic").get<bool>());
            REQUIRE(st.underline == el.value("underline", false));
            REQUIRE(st.color == el.at("color").get<std::string>());
        }
    }
}

}  // namespace

TEST_CASE("token_key matches the golden fixture", "[layout][overlay]")
{
    const auto doc = load_json(fixtures_dir() / "overlays.expected.json");
    for (const auto& c : doc.at("token_key")) {
        REQUIRE(token_key(c.at("in").get<std::string>())
                == c.at("out").get<std::string>());
    }
}

TEST_CASE("substitute_variables matches the golden fixture", "[layout][overlay]")
{
    const auto doc = load_json(fixtures_dir() / "overlays.expected.json");
    for (const auto& c : doc.at("substitute")) {
        INFO(c.at("text").get<std::string>());
        std::map<std::string, std::string> song_vars;
        for (auto it = c.at("song_vars").begin(); it != c.at("song_vars").end(); ++it) {
            song_vars[it.key()] = it.value().get<std::string>();
        }
        const auto ctx = song_token_values(song_vars);
        REQUIRE(substitute_variables(c.at("text").get<std::string>(), ctx)
                == c.at("out").get<std::string>());
    }
}

TEST_CASE("A stray brace does not hide a required token", "[layout][overlay]")
{
    const std::map<std::string, std::string> values{{"key", ""}, {"songbook", "Hymns"}};
    // Substitution already saw past the stray '{'; the drop check must too.
    REQUIRE(substitute_variables("Key {a {-key}\n{songbook}", values) == "Hymns");
    REQUIRE(substitute_variables("x {a {songbook}", values) == "x {a Hymns");
    REQUIRE(substitute_variables("{unknown} and {", values) == "{unknown} and {");
}

TEST_CASE("build_title_overlay_scene matches the golden fixture", "[layout][overlay]")
{
    const auto doc = load_json(fixtures_dir() / "overlays.expected.json");
    const auto& c = doc.at("title_scene");
    std::vector<OverlayBox> boxes;
    for (const auto& b : c.at("boxes")) {
        boxes.push_back(box_from_json(b));
    }
    std::map<std::string, std::string> song_vars;
    for (auto it = c.at("song_vars").begin(); it != c.at("song_vars").end(); ++it) {
        song_vars[it.key()] = it.value().get<std::string>();
    }
    const auto scene = build_title_overlay_scene(boxes, song_vars);
    require_scene_eq(scene, c.at("scene"));
}

TEST_CASE("empty overlay when tokens missing", "[layout][overlay]")
{
    const auto doc = load_json(fixtures_dir() / "overlays.expected.json");
    const auto& c = doc.at("empty_scene");
    std::vector<OverlayBox> boxes;
    for (const auto& b : c.at("boxes")) {
        boxes.push_back(box_from_json(b));
    }
    const auto scene = build_title_overlay_scene(boxes, {});
    require_scene_eq(scene, c.at("scene"));
    REQUIRE(scene.empty());
}

TEST_CASE("assign_overlay_to_title_slides", "[layout][overlay]")
{
    const auto doc = load_json(fixtures_dir() / "overlays.expected.json");
    const auto& c = doc.at("assign_title_slides");
    OverlayScene overlay;
    OverlayBox box;
    box.lines.push_back(RichLine{span_from_json(json{{"text", "T"}, {"scale", 100}})});
    overlay.boxes.push_back(box);

    std::vector<SlideGroup> groups;
    for (bool is_title : c.at("groups_is_title").get<std::vector<bool>>()) {
        SlideGroup g;
        g.is_title = is_title;
        groups.push_back(g);
    }
    const auto assigned = assign_overlay_to_title_slides(groups, overlay);
    REQUIRE(assigned.size() == c.at("overlay_nonempty").size());
    for (std::size_t i = 0; i < assigned.size(); ++i) {
        REQUIRE((!assigned[i].empty())
                == c.at("overlay_nonempty")[i].get<bool>());
    }
}

TEST_CASE("overlay scene copies underline from lines and spans", "[layout][overlay]")
{
    OverlayBox box;
    box.w = 200;
    box.h = 80;
    RichLine whole{span_from_json(json{{"text", "All"}, {"underline", true}})};
    RichLine mixed{span_from_json(json{{"text", "Hi "}}),
                   span_from_json(json{{"text", "there"}, {"underline", true}})};
    box.lines = {std::move(whole), std::move(mixed)};

    const auto scene = build_overlay_scene({box}, {});
    REQUIRE(scene.boxes.size() == 1);
    REQUIRE(scene.boxes[0].lines.size() == 2);
    REQUIRE(scene.boxes[0].lines[0].size() == 1);
    REQUIRE(scene.boxes[0].lines[0][0].style.underline);
    REQUIRE(scene.boxes[0].lines[1].size() == 2);
    REQUIRE_FALSE(scene.boxes[0].lines[1][0].style.underline);
    REQUIRE(scene.boxes[0].lines[1][1].style.underline);
}
