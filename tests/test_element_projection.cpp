// The element model is what a resolved theme's body boxes come from.

#include <catch2/catch_test_macros.hpp>

#include "theme_test_helpers.hpp"

#include <seventhslide/model/content_kind.hpp>
#include <seventhslide/theme/theme_elements.hpp>

#include <nlohmann/json.hpp>

using seventhslide::model::ContentKind;
using seventhslide::model::OutputConfig;
using seventhslide::theme::ElementBinding;
using seventhslide::theme::ThemeSelection;
namespace theme = seventhslide::theme;
namespace th = seventhslide::test;

namespace {

[[nodiscard]] OutputConfig with_design(const theme::ElementSet& set)
{
    OutputConfig oc;
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    th::use_design(oc, set);
    return oc;
}

/// ``oc``'s look for content of ``kind``: its kind of theme's design.
[[nodiscard]] theme::ResolvedTheme resolved(const OutputConfig& oc,
                                            ContentKind kind = ContentKind::Song)
{
    ThemeSelection sel;
    sel.kind = kind;
    return theme::resolve_theme(oc, sel);
}

[[nodiscard]] theme::Element* lyrics_of(theme::ElementSet& set)
{
    for (auto& el : set.song) {
        if (el.binding == ElementBinding::Lyrics) {
            return &el;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("the element model places the song body", "[theme][elements][projection]")
{
    auto set = theme::default_elements();
    auto* lyrics = lyrics_of(set);
    REQUIRE(lyrics != nullptr);
    lyrics->object.x = 400.4;   // rounded, not truncated
    lyrics->object.y = 250.6;
    lyrics->object.w = 1000.0;
    lyrics->object.h = 600.0;
    lyrics->object.font_size = 60;
    lyrics->object.bold = true;
    lyrics->object.text_align = "left";
    lyrics->object.vertical_align = "top";
    lyrics->object.line_height = 1.45;
    lyrics->object.font_color = "#12ab34";
    lyrics->body.other_lines.color = "#404040";
    lyrics->body.active_line.size = 52;
    lyrics->body.active_line.bold = false;
    lyrics->body.show_chords = true;
    lyrics->body.chords = {.size_pct = 90, .gap_pct = 20, .color = "#00ccff"};

    const auto theme = resolved(with_design(set));
    const auto& song = theme.song;

    REQUIRE(song.x == 400);
    REQUIRE(song.y == 251);
    REQUIRE(song.width == 1000);
    REQUIRE(song.height == 600);
    REQUIRE(song.font_size == 60);
    REQUIRE(song.bold);
    REQUIRE(song.align == "left");
    REQUIRE(song.valign == "top");
    REQUIRE(song.line_height == 1.45);
    REQUIRE(song.color == "#12ab34");
    REQUIRE(song.other_lines.color == "#404040");
    REQUIRE(song.active_line.size == 52);
    REQUIRE(song.active_line.bold == false);
    // Over the box: the active line keeps the box's family and colour.
    const auto active = seventhslide::canvas::painted_type(song, song.active_line);
    REQUIRE(active.size == 52);
    REQUIRE_FALSE(active.bold);
    REQUIRE(active.color == "#12ab34");
    const auto others = seventhslide::canvas::painted_type(song, song.other_lines);
    REQUIRE(others.size == 60);
    REQUIRE(others.bold);
    REQUIRE(others.color == "#404040");
    REQUIRE(song.show_chords);
    REQUIRE(song.chords == lyrics->body.chords);
    REQUIRE(song.shown);
}

TEST_CASE("a body element that is not there turns its body off", "[theme][elements][projection]")
{
    theme::ElementSet empty;
    empty.canvas = {1920, 1080};
    const auto theme = resolved(with_design(empty));
    REQUIRE_FALSE(theme.song.shown);
    REQUIRE_FALSE(theme.bible.shown);
    REQUIRE_FALSE(theme.video.enabled);
    REQUIRE_FALSE(theme.image.enabled);
    REQUIRE_FALSE(theme.web.enabled);
}

TEST_CASE("media areas come from their elements", "[theme][elements][projection]")
{
    theme::ElementSet set;
    set.canvas = {1920, 1080};
    auto video = theme::make_element(seventhslide::freeform::ObjectType::Image,
                                     ElementBinding::VideoArea, "te_video", {1920, 1080});
    video.object.x = 10;
    video.object.y = 20;
    video.object.w = 800;
    video.object.h = 450;
    video.object.object_fit = "cover";
    auto image = theme::make_element(seventhslide::freeform::ObjectType::Image,
                                     ElementBinding::ImageArea, "te_image", {1920, 1080});
    image.object.x = 100.4;
    image.object.y = 199.6;
    image.object.w = 640;
    image.object.h = 360;
    image.object.object_fit = "cover";
    set.video.push_back(video);
    set.image.push_back(image);

    const auto theme = resolved(with_design(set), ContentKind::Image);
    REQUIRE(theme.video.enabled);
    REQUIRE(theme.video.x == 10);
    REQUIRE(theme.video.y == 20);
    REQUIRE(theme.video.width == 800);
    REQUIRE(theme.video.height == 450);
    // Video fit is the painter's; the element's object_fit is not read.
    REQUIRE(theme.video.fit == "contain");
    REQUIRE(theme.image.enabled);
    // Designer positions land on whole pixels.
    REQUIRE(theme.image.x == 100);
    REQUIRE(theme.image.y == 200);
    REQUIRE(theme.image.width == 640);
    REQUIRE(theme.image.height == 360);
    REQUIRE(theme.image.fit == "cover");
}

TEST_CASE("a web area comes from its element", "[theme][elements][projection][web]")
{
    theme::ElementSet set;
    set.canvas = {1920, 1080};
    auto web = theme::make_element(seventhslide::freeform::ObjectType::Image,
                                   ElementBinding::WebArea, "te_web", {1920, 1080});
    REQUIRE(web.object.w == 1920);
    web.object.x = 960;
    web.object.y = 0;
    web.object.w = 960;
    web.object.h = 1080;
    web.object.object_fit = "cover";
    set.web.push_back(web);

    const auto theme = resolved(with_design(set), ContentKind::Web);
    REQUIRE(theme.web.enabled);
    REQUIRE(theme.web.x == 960);
    REQUIRE(theme.web.width == 960);
    REQUIRE(theme.web.height == 1080);
    REQUIRE(theme.web.fit == "cover");
    // The other areas are their own.
    REQUIRE_FALSE(theme.image.enabled);
}
