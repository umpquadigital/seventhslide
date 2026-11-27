#include <catch2/catch_test_macros.hpp>

#include "theme_test_helpers.hpp"

#include <seventhslide/model/output_config.hpp>
#include <seventhslide/theme/resolved_theme.hpp>
#include <seventhslide/theme/theme_resolver.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>
#include <vector>

using seventhslide::model::OutputConfig;
using seventhslide::model::ThemeEntry;
using seventhslide::model::ContentKind;
using seventhslide::theme::ThemeSelection;
using seventhslide::test::flat_style;

namespace {

nlohmann::json load_fixture()
{
    const std::string path =
        std::string(SEVENTHSLIDE_FIXTURES_DIR) + "/theme/theme_resolver.expected.json";
    std::ifstream in(path);
    REQUIRE(in.good());
    nlohmann::json j;
    in >> j;
    return j;
}

OutputConfig make_fixture_output(const nlohmann::json& doc)
{
    OutputConfig oc;
    oc.id = doc.at("output").at("id").get<std::string>();
    oc.name = doc.at("output").at("name").get<std::string>();
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    oc.category_defaults = doc.at("output").at("category_defaults");

    // The same three looks in every kind's library, so a case reads the same
    // whichever category it resolves; ``t_scripture`` is a Bible theme only.
    const std::vector<ThemeEntry> looks = {
        ThemeEntry{"t_default", "Default Text",
                   flat_style({{"font_size", 48}, {"align", "center"}, {"highlight_color", "#ffffff"}})},
        ThemeEntry{"t_big", "Big",
                   flat_style({{"font_size", 72}, {"align", "left"}, {"highlight_color", "#ffcc00"}})},
        ThemeEntry{"t_ann", "Ann Text", flat_style({{"font_size", 36}, {"align", "center"}})},
    };
    oc.song_themes = looks;
    oc.bible_themes = looks;
    oc.bible_themes.push_back(
        ThemeEntry{"t_scripture", "Scripture", flat_style({{"font_size", 40}})});
    oc.media_themes = looks;
    oc.bg_themes = {
        ThemeEntry{"b_default", "Default BG",
                   {{"background_type", "color"},
                    {"background_color", "#000000"}}},
        ThemeEntry{"b_blue", "Blue",
                   {{"background_type", "color"},
                    {"background_color", "#003366"}}},
        ThemeEntry{"b_img", "Image",
                   {{"background_type", "image"},
                    {"background_image", "/media/bg.jpg"}}},
    };
    return oc;
}

ThemeSelection selection_from_json(const nlohmann::json& s)
{
    ThemeSelection sel;
    const auto mode = s.at("mode").get<std::string>();
    sel.from_service = mode == "service";
    if (sel.from_service) {
        const auto item = s.value("service_item_type", std::string{});
        sel.kind = seventhslide::model::content_kind_from_string(item);
    } else {
        sel.kind = seventhslide::model::content_kind_from_string(mode);
    }
    sel.service_themes = seventhslide::theme::service_themes_from_json(
        s.value("service_theme_map", nlohmann::json::object()));
    // As the app reads a content's own map: only what that content may hold.
    sel.content_themes = seventhslide::theme::OutputThemeMap::from_json(
        s.value("content_theme_map", nlohmann::json::object()),
        seventhslide::theme::categories_for(sel.kind));
    if (s.contains("theme_priority") && s["theme_priority"].is_array()) {
        sel.theme_priority = s["theme_priority"].get<std::vector<std::string>>();
    }
    return sel;
}

} // namespace

TEST_CASE("ThemeResolver matches the golden fixture", "[theme]")
{
    const auto doc = load_fixture();
    REQUIRE(doc.at("schema") == "seventhslide.theme_resolver.v2");
    const auto oc = make_fixture_output(doc);

    for (const auto& c : doc.at("cases")) {
        INFO(c.at("name").get<std::string>() << ": " << c.at("why").get<std::string>());
        const auto sel = selection_from_json(c.at("selection"));

        const auto ids = seventhslide::theme::resolve_theme_ids(oc, sel);
        REQUIRE(ids.theme_id == c.at("theme_ids").at("theme").get<std::string>());
        REQUIRE(ids.bg_id == c.at("theme_ids").at("bg").get<std::string>());

        const auto* text_theme = seventhslide::theme::resolve_text_theme(oc, sel);
        if (c.at("text_theme_id").is_null()) {
            REQUIRE(text_theme == nullptr);
        } else {
            REQUIRE(text_theme != nullptr);
            REQUIRE(text_theme->id == c.at("text_theme_id").get<std::string>());
        }

        const auto theme = seventhslide::theme::resolve_theme(oc, sel);
        const auto spot = c.at("style_spotlight");
        if (spot.at("font_size").is_number()) {
            REQUIRE(theme.song.font_size == spot.at("font_size").get<int>());
        }
        if (spot.at("align").is_string()) {
            REQUIRE(theme.song.align == spot.at("align").get<std::string>());
        }
        if (spot.at("highlight_color").is_string()) {
            REQUIRE(theme.song.color == spot.at("highlight_color").get<std::string>());
        }
        if (spot.at("background_type").is_string()) {
            REQUIRE(seventhslide::layout::to_string(theme.background.type)
                    == spot.at("background_type").get<std::string>());
        }
        if (spot.at("background_color").is_string()) {
            REQUIRE(theme.background.color == spot.at("background_color").get<std::string>());
        }
    }
}

TEST_CASE("a text theme's title-slide switch wins over the output's", "[theme][title]")
{
    // The switch lives on the text theme - that is where the operator sets it -
    // so the cascade has to let the theme layer override the output's default.
    // The output value still applies to a theme that says nothing.
    OutputConfig oc;
    oc.name = "Main";
    oc.song_themes = {ThemeEntry{"t_off", "Off", {{"show_title_slides", false}}},
                      ThemeEntry{"t_quiet", "Quiet", {{"font_size", 48}}}};
    oc.bg_themes = {ThemeEntry{"b", "BG", {{"background_color", "#000000"}}}};

    ThemeSelection sel;
    sel.kind = ContentKind::Song;

    oc.category_defaults = {{"song", {{"theme", "t_off"}, {"bg", "b"}}}};
    REQUIRE_FALSE(seventhslide::theme::resolve_theme(oc, sel).behavior.show_title_slides);

    // A theme that does not mention the switch keeps the default, which is on.
    oc.category_defaults = {{"song", {{"theme", "t_quiet"}, {"bg", "b"}}}};
    REQUIRE(seventhslide::theme::resolve_theme(oc, sel).behavior.show_title_slides);
}

TEST_CASE("ThemeResolver find_theme and empty id", "[theme]")
{
    OutputConfig oc;
    oc.song_themes = {ThemeEntry{"a", "A", {{"font_size", 10}}}};
    REQUIRE(seventhslide::theme::find_theme(oc.song_themes, "") == nullptr);
    REQUIRE(seventhslide::theme::find_theme(oc.song_themes, "missing") == nullptr);
    REQUIRE(seventhslide::theme::find_theme(oc.song_themes, "a")->name == "A");
}

