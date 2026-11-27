#include <catch2/catch_test_macros.hpp>

#include <seventhslide/freeform/background_copy.hpp>
#include <seventhslide/layout/background_fill.hpp>
#include <seventhslide/model/transition.hpp>
#include <seventhslide/theme/resolved_theme.hpp>

#include <nlohmann/json.hpp>

using seventhslide::layout::BackgroundType;
using seventhslide::layout::background_fill_from_style;

TEST_CASE("background_fill_from_style reads theme fill keys", "[layout][bg]")
{
    const nlohmann::json style = {
        {"background_type", "image"},
        {"background_color", "#111111"},
        {"background_image", "/static/theme_backgrounds/a.png"},
        {"background_video", "/static/theme_backgrounds/loop.mp4"},
    };
    const auto got = background_fill_from_style(style);
    REQUIRE(got.has_value());
    REQUIRE(got->type == BackgroundType::Image);
    REQUIRE(got->image == "/static/theme_backgrounds/a.png");
    REQUIRE(got->video.empty());

    const nlohmann::json video = {
        {"background_type", "video"},
        {"background_video", "/static/theme_backgrounds/loop.mp4"},
    };
    const auto v = background_fill_from_style(video);
    REQUIRE(v.has_value());
    REQUIRE(v->type == BackgroundType::Video);
    REQUIRE(v->video == "/static/theme_backgrounds/loop.mp4");
}

TEST_CASE("an empty background theme is transparent in both readers", "[layout][bg]")
{
    // The seeded Base background theme is {}. The design panel shows
    // Transparent for a missing background_type. A slide that selects that
    // theme and a song that inherits it have to paint the same fill.
    seventhslide::theme::ThemeBackground theme_default;
    seventhslide::layout::BackgroundFill fill_default;
    REQUIRE(theme_default.type == fill_default.type);
    REQUIRE(theme_default.color == fill_default.color);

    const nlohmann::json empty = nlohmann::json::object();
    const auto fill = background_fill_from_style(empty);
    REQUIRE(fill.has_value());
    REQUIRE(fill->type == BackgroundType::Transparent);
    REQUIRE(fill->image.empty());
    REQUIRE(fill->video.empty());

    const auto theme = seventhslide::theme::resolve_styles(empty, empty, {1920, 1080});
    REQUIRE(theme.background.type == fill->type);
    REQUIRE(theme.background.color == fill->color);
}

TEST_CASE("background_fill_from_style defaults a blank or mistyped fill", "[layout][bg]")
{
    const auto blank = background_fill_from_style(
        nlohmann::json{{"background_type", ""}, {"background_color", 5}});
    REQUIRE(blank.has_value());
    REQUIRE(blank->type == BackgroundType::Transparent);
    REQUIRE(blank->color == "#000000");

    const auto color = background_fill_from_style(nlohmann::json{{"background_type", "color"}});
    REQUIRE(color.has_value());
    REQUIRE(color->type == BackgroundType::Color);
    REQUIRE(color->color == "#000000");

    const nlohmann::json blank_color{{"background_type", "color"}, {"background_color", ""}};
    const auto color_fill = background_fill_from_style(blank_color);
    REQUIRE(color_fill.has_value());
    REQUIRE(color_fill->type == BackgroundType::Color);
    REQUIRE(color_fill->color == "#000000");
    const auto color_theme =
        seventhslide::theme::resolve_styles({}, blank_color, {1920, 1080});
    REQUIRE(color_theme.background.type == color_fill->type);
    REQUIRE(color_theme.background.color == color_fill->color);

    // A blank poster is not a black plate. The stack underneath shows until
    // a frame is decoded.
    const nlohmann::json video{{"background_type", "video"}, {"background_color", ""}};
    const auto video_fill = background_fill_from_style(video);
    REQUIRE(video_fill.has_value());
    REQUIRE(video_fill->type == BackgroundType::Video);
    REQUIRE(video_fill->color.empty());
    const auto video_theme = seventhslide::theme::resolve_styles({}, video, {1920, 1080});
    REQUIRE(video_theme.background.type == video_fill->type);
    REQUIRE(video_theme.background.color == video_fill->color);

    REQUIRE_FALSE(background_fill_from_style(nlohmann::json::array()).has_value());
}

// A slide copying a background theme (the editor's "From theme", or a song
// saved as Freeform) and the theme playing live read its fade the same way.
TEST_CASE("a background theme's fade has one reading", "[layout][bg]")
{
    using seventhslide::layout::background_fade_from_style;
    using seventhslide::layout::BackgroundFade;

    REQUIRE(background_fade_from_style(nlohmann::json::object()) == BackgroundFade{});
    REQUIRE(background_fade_from_style(nlohmann::json::array()) == BackgroundFade{});

    for (const nlohmann::json& style :
         {nlohmann::json{{"bg_enable_fade", true}, {"bg_fade_duration", 700}},
          nlohmann::json{{"bg_enable_fade", 1}, {"bg_fade_duration", 699.6}},
          nlohmann::json{{"bg_enable_fade", false}, {"bg_fade_duration", "slow"}}}) {
        INFO(style.dump());
        const auto fade = background_fade_from_style(style);
        const auto live = seventhslide::theme::read_theme_background(style);
        REQUIRE(live.enable_fade == fade.enabled);
        REQUIRE(live.fade_duration == fade.duration_ms);
        REQUIRE(seventhslide::freeform::background_copy_of(style).transition
                == seventhslide::model::transition_from_fade(fade.enabled, fade.duration_ms));
    }
    // Truncated, as the painters have always read a theme's numbers.
    REQUIRE(background_fade_from_style({{"bg_fade_duration", 699.6}}).duration_ms == 699);
}

TEST_CASE("a resolved background paints the fill its theme names", "[layout][bg]")
{
    for (const nlohmann::json& style :
         {nlohmann::json{{"background_type", "color"}, {"background_color", "#123456"}},
          nlohmann::json{{"background_type", "image"}, {"background_image", "/a.png"}},
          nlohmann::json{{"background_type", "video"}, {"background_video", "/b.mp4"}},
          nlohmann::json::object()}) {
        INFO(style.dump());
        REQUIRE(seventhslide::theme::read_theme_background(style).fill()
                == background_fill_from_style(style).value());
    }
}
