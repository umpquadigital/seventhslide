#include <catch2/catch_test_macros.hpp>

#include "scene_test_helpers.hpp"

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/build/song_layout.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/scene/paint_order.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/scene/text_metrics.hpp>
#include <seventhslide/scene/theme_draft_preview.hpp>
#include <seventhslide/theme/resolved_theme.hpp>
#include <seventhslide/theme/theme_elements.hpp>

#include "theme_test_helpers.hpp"
#include <seventhslide/devsupport/flat_style.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::scene::SceneMode;
using seventhslide::scene::draft_context_from_string;
using seventhslide::scene::isolate_text_preview_style;
using seventhslide::theme::ElementContext;
namespace th = seventhslide::test;

namespace {

/// The theme draft of ``style``, whose flat body keys (when it has no element
/// model) describe the design.
template <typename... Args>
[[nodiscard]] seventhslide::scene::SceneFrame draft_frame(const nlohmann::json& style,
                                                          std::string_view context,
                                                          Args&&... args)
{
    const auto ctx = draft_context_from_string(context);
    REQUIRE(ctx.has_value());
    return seventhslide::scene::build_theme_draft_frame(th::flat_style(style), *ctx,
                                                        std::forward<Args>(args)...);
}

using seventhslide::test::element_text;

/// True when the frame carries a clock object (one per design context).
[[nodiscard]] bool has_clock(const seventhslide::scene::SceneFrame& frame)
{
    for (const auto& so : frame.objects) {
        if (so.object && so.object->type == seventhslide::canvas::ObjectType::Clock) {
            return true;
        }
    }
    return false;
}

/// A new ``type`` element, at 100,100 and 200 x 100.
[[nodiscard]] seventhslide::theme::Element element_of(seventhslide::freeform::ObjectType type,
                                                      std::string id,
                                                      int z)
{
    auto el = seventhslide::theme::make_element(type, seventhslide::theme::ElementBinding::Static,
                                                std::move(id), seventhslide::geom::k_reference_canvas);
    el.object.z = z;
    el.object.x = 100;
    el.object.y = 100;
    el.object.w = 200;
    el.object.h = 100;
    return el;
}

/// ``style`` with the design its flat keys describe, plus ``add`` in paint order
/// and without the elements named in ``drop``.
[[nodiscard]] nlohmann::json designed(
    nlohmann::json style,
    const std::vector<std::pair<ElementContext, seventhslide::theme::Element>>& add,
    const std::vector<std::string>& drop = {})
{
    auto set = seventhslide::devsupport::elements_from_flat_style(style);
    for (const auto& [ctx, el] : add) {
        th::add_element(set.at(ctx), el);
    }
    for (const auto ctx : seventhslide::theme::k_element_contexts) {
        auto& list = set.at(ctx);
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&drop](const seventhslide::theme::Element& el) {
                                      return std::find(drop.begin(), drop.end(), el.object.id)
                                             != drop.end();
                                  }),
                   list.end());
    }
    style[std::string(seventhslide::theme::k_elements_key)] = set.to_json();
    return style;
}

}  // namespace


TEST_CASE("theme draft contexts", "[theme][draft_preview]")
{
    using seventhslide::scene::DraftContext;
    REQUIRE(draft_context_from_string("song") == DraftContext::Song);
    REQUIRE(draft_context_from_string("bible") == DraftContext::Bible);
    REQUIRE(draft_context_from_string("image") == DraftContext::Image);
    REQUIRE(draft_context_from_string("video") == DraftContext::Video);
    REQUIRE(draft_context_from_string("web") == DraftContext::Web);
    REQUIRE(draft_context_from_string("live") == DraftContext::Live);
    // Audio is not a context: it paints nothing, and its fades are each
    // output's.
    REQUIRE_FALSE(draft_context_from_string("audio"));
    REQUIRE(draft_context_from_string("behavior") == DraftContext::Behavior);
    REQUIRE(draft_context_from_string("bg") == DraftContext::Background);
    REQUIRE(draft_context_from_string("titles") == DraftContext::Titles);
    REQUIRE_FALSE(draft_context_from_string("title"));
}

TEST_CASE("theme draft song sample paints text + chrome", "[theme][draft_preview]")
{
    nlohmann::json style = {
        {"box_x", 100},
        {"box_y", 200},
        {"width_px", 800},
        {"height_px", 300},
        {"font_size", 40},
        {"highlight_color", "#ffffff"},
        {"background_type", "color"},
        {"background_color", "#112233"},
    };
    const auto frame = draft_frame(
        designed(style, {{ElementContext::Song, th::indicator_element()}}), "song",
        1280, 720);
    REQUIRE(frame.mode == SceneMode::Song);
    REQUIRE(frame.canvas_width == 1280);
    REQUIRE(frame.canvas_height == 720);
    REQUIRE(frame.background.type == seventhslide::layout::BackgroundType::Color);
    REQUIRE(frame.background.color == "#112233");
    REQUIRE(frame.text.has_value());
    // Flat keys describe the 1920x1080 reference canvas; on 1280x720 the box
    // keeps its place and size relative to the canvas.
    REQUIRE(frame.text->box.x == 67);
    REQUIRE(frame.text->box.y == 133);
    REQUIRE(frame.text->box.width == 533);
    REQUIRE(frame.text->box.height == 200);
    REQUIRE(frame.text->box.font_size == 27);
    REQUIRE(frame.text->lines.size() >= 2);
    REQUIRE_FALSE(element_text(frame, "te_copyright").empty());
    REQUIRE_FALSE(element_text(frame, "te_indicator").empty());
    const auto plain0 = seventhslide::model::plain_text(frame.text->lines[0].runs);
    REQUIRE(plain0.find("heavens") != std::string::npos);
}

TEST_CASE("theme draft bible sample", "[theme][draft_preview]")
{
    nlohmann::json style = {
        {"bible_text_box_x", 50},
        {"show_bible_text", true},
        {"background_type", "color"},
        {"background_color", "#000000"},
    };
    const auto frame = draft_frame(style, "bible");
    REQUIRE(frame.mode == SceneMode::Bible);
    REQUIRE(frame.text.has_value());
    REQUIRE(element_text(frame, "te_bible_ref") == "Revelation 14:6-7\nNKJV");
    const auto plain = seventhslide::model::plain_text(frame.text->lines[0].runs);
    REQUIRE(plain.find("everlasting gospel") != std::string::npos);
}

TEST_CASE("theme draft bible sample fits the text box", "[theme][draft_preview][bible][fit]")
{
    ApproxTextMeasurer measurer;
    nlohmann::json style = {
        {"show_bible_text", true},
        {"show_bible_verse_numbers", true},
        {"bible_main_font_family", "Helvetica"},
        {"bible_main_font_size", 40},
        {"bible_verse_font_size", 80},
        {"bible_verse_margin_right_pct", 60},
        {"bible_text_box_width", 420},
        {"bible_text_box_height", 180},
        {"bible_text_line_height", 1.2},
    };
    const auto frame = draft_frame(
        style, "bible", 1920, 1080, "preview", &measurer);
    REQUIRE(frame.text.has_value());
    const auto& text = *frame.text;
    const double lh = std::ceil(text.box.font_size * text.box.line_height);
    CHECK(static_cast<double>(text.lines.size()) * lh <= text.box.height);
    REQUIRE_FALSE(text.lines.empty());
    for (const auto& line : text.lines) {
        const auto plain = seventhslide::model::plain_text(line.runs);
        REQUIRE(plain.size() < 160);
    }
}

TEST_CASE("theme draft bible sample runs verses on when the theme does", "[theme][draft_preview][bible][fluid]")
{
    ApproxTextMeasurer measurer;
    nlohmann::json style = {
        {"show_bible_text", true},
        {"bible_main_font_family", "Helvetica"},
        {"bible_main_font_size", 40},
        {"bible_text_box_width", 1800},
        {"bible_text_box_height", 900},
    };
    const auto first_slide = [&](const nlohmann::json& s) {
        const auto frame = draft_frame(s, "bible", 1920, 1080, "preview", &measurer);
        REQUIRE(frame.text.has_value());
        std::string plain;
        for (const auto& line : frame.text->lines) {
            plain += seventhslide::model::plain_text(line.runs) + ' ';
        }
        return plain;
    };

    REQUIRE(first_slide(style).find("Fear God") == std::string::npos);
    style["bible_fluid_verses"] = true;
    const auto run_on = first_slide(style);
    REQUIRE(run_on.find("everlasting gospel") != std::string::npos);
    REQUIRE(run_on.find("Fear God") != std::string::npos);
}

TEST_CASE("theme draft song wraps and fits with measurer", "[theme][draft_preview]")
{
    ApproxTextMeasurer measurer;
    nlohmann::json style = {
        {"box_x", 0},
        {"box_y", 0},
        {"width_px", 200},
        {"height_px", 80},
        {"font_size", 40},
        {"font_family", "Helvetica"},
        {"background_type", "color"},
        {"background_color", "#000000"},
        {"follow_lines", 0},
        {"fluid_slides", true},
    };
    const std::vector<std::string> no_notice{"te_copyright", "te_title_copyright"};

    const auto wide = draft_frame(
        designed({{"box_x", 0},
                  {"box_y", 0},
                  {"width_px", 2000},
                  {"height_px", 800},
                  {"font_size", 40},
                  {"font_family", "Helvetica"},
                  {"background_type", "color"},
                  {"follow_lines", 0},
                  {"fluid_slides", true}},
                 {}, no_notice),
        "song",
        1920,
        1080,
        "preview",
        &measurer);

    const auto narrow = draft_frame(
        designed(style, {}, no_notice), "song", 1920, 1080, "preview", &measurer);

    REQUIRE(wide.text.has_value());
    REQUIRE(narrow.text.has_value());
    // Narrow box must wrap and/or drop lines vs a tall wide box.
    REQUIRE(narrow.text->lines.size() < wide.text->lines.size());
    // Every visual line should be short enough not to be one giant unwrapped string.
    for (const auto& line : narrow.text->lines) {
        const auto plain = seventhslide::model::plain_text(line.runs);
        REQUIRE(plain.size() < 80);
    }
}

TEST_CASE("theme draft bg keeps transparent surface", "[theme][draft_preview]")
{
    const auto style = designed({{"background_type", "transparent"}},
                                {{ElementContext::Song, th::clock_element("te_clock")}});
    const auto frame = draft_frame(style, "bg");
    REQUIRE(frame.mode == SceneMode::Empty);
    REQUIRE_FALSE(frame.text.has_value());
    REQUIRE(frame.background.type == seventhslide::layout::BackgroundType::Transparent);
    // The clock is a text-theme element; a background theme has no design.
    REQUIRE_FALSE(has_clock(frame));
}

TEST_CASE("theme draft clock follows the edited context", "[theme][draft_preview]")
{
    const auto style = designed({{"background_type", "color"}},
                                {{ElementContext::Song, th::clock_element("te_clock")},
                                 {ElementContext::Video, th::clock_element("te_media_clock")}});
    const auto lit = [&](const char* context) {
        return has_clock(draft_frame(style, context));
    };
    REQUIRE(lit("song"));
    REQUIRE(lit("behavior"));
    REQUIRE(lit("video"));
    REQUIRE_FALSE(lit("image"));
    REQUIRE_FALSE(lit("bible"));
    REQUIRE_FALSE(lit("titles"));
    REQUIRE_FALSE(lit("bg"));

    // The designer stage rasters every element, so the clock is there too - it
    // is what the audience will see.
    const auto designer = draft_frame(
        style, "song", 1920, 1080, "preview", nullptr, true);
    REQUIRE(has_clock(designer));
}

TEST_CASE("theme draft text without surface keys is transparent", "[theme][draft_preview]")
{
    const auto frame = draft_frame({{"font_size", 40}}, "song");
    REQUIRE(frame.mode == SceneMode::Song);
    REQUIRE(frame.background.type == seventhslide::layout::BackgroundType::Transparent);
}

TEST_CASE("theme draft titles is empty with partner background", "[theme][draft_preview]")
{
    nlohmann::json style = {
        {"background_type", "color"},
        {"background_color", "#112233"},
    };
    const auto frame = draft_frame(style, "titles");
    REQUIRE(frame.mode == SceneMode::Empty);
    REQUIRE_FALSE(frame.text.has_value());
    // The stock design carries the copyright notice onto the Titles context.
    REQUIRE_FALSE(element_text(frame, "te_title_copyright").empty());
    // The Titles draft paints the theme's own background, as Song does.
    REQUIRE(frame.background.type == seventhslide::layout::BackgroundType::Color);
    REQUIRE(frame.background.color == "#112233");
}

TEST_CASE("isolate_text_preview_style drops partner surface keys", "[theme][draft_preview]")
{
    nlohmann::json text = {
        {"font_size", 48},
        {"highlight_color", "#fff"},
        {"background_type", "image"},
        {"background_color", "#ff00aa"},
        {"background_image", "/static/theme_backgrounds/x.png"},
    };
    const auto isolated = isolate_text_preview_style(text);
    REQUIRE(isolated["font_size"] == 48);
    REQUIRE(isolated["highlight_color"] == "#fff");
    REQUIRE(isolated["background_type"] == "transparent");
    REQUIRE_FALSE(isolated.contains("background_color"));
    REQUIRE_FALSE(isolated.contains("background_image"));
}

TEST_CASE("designer draft omits only the box it is editing", "[theme][draft_preview]")
{
    const auto song = designed({{"font_size", 40},
                                {"background_type", "color"},
                                {"background_color", "#000000"}},
                               {{ElementContext::Song, th::indicator_element()}});
    const auto gallery_song = draft_frame(song, "song");
    REQUIRE_FALSE(element_text(gallery_song, "te_copyright").empty());
    REQUIRE_FALSE(element_text(gallery_song, "te_indicator").empty());
    REQUIRE(gallery_song.text.has_value());

    // The designer paints every element too - only the box it is typing into is
    // left out, so the QML caret is not drawn over its own painted copy.
    const auto designer_song = draft_frame(
        song, "song", 1920, 1080, "preview", nullptr, true, "te_copyright");
    REQUIRE(element_text(designer_song, "te_copyright").empty());
    REQUIRE_FALSE(element_text(designer_song, "te_indicator").empty());
    REQUIRE(designer_song.text.has_value());

    nlohmann::json bible = {
        {"show_bible_text", true},
        {"background_type", "color"},
    };
    const auto gallery_bible = draft_frame(bible, "bible");
    REQUIRE(element_text(gallery_bible, "te_bible_ref") == "Revelation 14:6-7\nNKJV");
    const auto designer_bible = draft_frame(
        bible, "bible", 1920, 1080, "preview", nullptr, true, "te_bible_ref");
    REQUIRE(element_text(designer_bible, "te_bible_ref").empty());
    REQUIRE(designer_bible.text.has_value());

    // A media design is the same on the stage as on a card: the area, with
    // its stand-in.
    const auto gallery_video = draft_frame({{"font_size", 36}}, "video");
    REQUIRE(gallery_video.media_stand_in.has_value());
    const auto designer_video = draft_frame(
        {{"font_size", 36}}, "video", 1920, 1080, "preview", nullptr, true);
    REQUIRE(designer_video.media_stand_in == gallery_video.media_stand_in);
    REQUIRE_FALSE(designer_video.text.has_value());

    // The clock is a text-theme element: a bg preview is the fill alone in both
    // the gallery (Freeform's canvas backdrop) and the designer stage.
    const auto bg = designed({{"background_type", "color"}},
                             {{ElementContext::Song, th::clock_element("te_clock")}});
    const auto gallery_bg = draft_frame(bg, "bg");
    REQUIRE_FALSE(has_clock(gallery_bg));
    const auto designer_bg = draft_frame(
        bg, "bg", 1920, 1080, "preview", nullptr, true);
    REQUIRE_FALSE(has_clock(designer_bg));
}

TEST_CASE("theme draft honors the element switches", "[theme][draft_preview][show]")
{
    nlohmann::json style = {
        {"box_x", 100}, {"box_y", 100}, {"width_px", 800}, {"height_px", 300},
        {"font_family", "Helvetica"}, {"font_size", 40},
    };

    SECTION("song body follows show_lyrics")
    {
        REQUIRE(draft_frame(style, "song").text.has_value());
        style["show_lyrics"] = false;
        const auto off = draft_frame(style, "song");
        REQUIRE_FALSE(off.text.has_value());
        // Behavior is a settings demo - its wrap / follow controls need a body.
        REQUIRE(draft_frame(style, "behavior").text.has_value());
    }

    SECTION("each media stand-in follows its own area's switch")
    {
        style["video_area_width"] = 400;
        style["video_area_height"] = 300;
        REQUIRE(draft_frame(style, "video").media_stand_in.has_value());
        REQUIRE(draft_frame(style, "image").media_stand_in.has_value());

        // Switching videos' area off leaves pictures alone.
        style["video_enabled"] = false;
        REQUIRE_FALSE(draft_frame(style, "video").media_stand_in.has_value());
        REQUIRE_FALSE(seventhslide::scene::paints_media(draft_frame(style, "video")));
        REQUIRE(draft_frame(style, "image").media_stand_in.has_value());

        style["image_enabled"] = false;
        REQUIRE_FALSE(draft_frame(style, "image").media_stand_in.has_value());
    }

    SECTION("a page or live source with no area has no stand-in either")
    {
        REQUIRE(draft_frame(style, "web").media_stand_in.has_value());
        REQUIRE(draft_frame(style, "live").media_stand_in.has_value());
        const auto no_page = designed(style, {}, {"te_web_area"});
        REQUIRE_FALSE(draft_frame(no_page, "web").media_stand_in.has_value());
        REQUIRE_FALSE(seventhslide::scene::paints_media(draft_frame(no_page, "web")));
        REQUIRE(draft_frame(no_page, "live").media_stand_in.has_value());
        const auto no_live = designed(style, {}, {"te_live_area"});
        REQUIRE_FALSE(draft_frame(no_live, "live").media_stand_in.has_value());
        REQUIRE(draft_frame(no_live, "web").media_stand_in.has_value());
    }
}

// The designer's sample slide goes through the audience frame's song row
// builder, so follow-line roles, re-wraps, and verse gaps cannot drift apart.
TEST_CASE("theme draft song rows are the audience frame's rows", "[theme][draft_preview]")
{
    ApproxTextMeasurer measurer;
    const nlohmann::json style{
        {"font_size", 48},     {"width_px", 420},        {"height_px", 900},
        {"follow_lines", 1},   {"active_line", {{"size", 60}}},
        {"other_lines", {{"color", "#777777"}}},
    };
    const auto frame =
        draft_frame(style, "song", 1920, 1080, "preview", &measurer);
    REQUIRE(frame.text.has_value());

    constexpr std::string_view lyrics = "---[Verse:1]---\n"
                                        "[G]Soon the heavens will [C]open [G]wide,\n"
                                        "Christ will [D]come to claim His [G]bride.\n"
                                        "All the [C]universe will [G]sing,\n"
                                        "Halle[Em]lujah! [D]Christ is [G]King!\n"
                                        "We have this hope, this faith and God's great love.\n"
                                        "We are united in Christ.\n";
    // The draft resolved the way the preview resolves it.
    const auto theme = seventhslide::theme::resolve_styles(th::flat_style(style), style, {1920, 1080});
    const auto layout = seventhslide::build::compute_song_layout(theme, measurer, lyrics, "v1");
    auto expected = seventhslide::scene::build_song_slide_text(layout, theme, 0, &measurer);
    REQUIRE_FALSE(expected.lines.empty());
    // A built frame's lines carry their metrics.
    seventhslide::scene::measure_text_content(expected, measurer);
    REQUIRE(*frame.text == expected);
    REQUIRE(frame.text->lines.front().role == seventhslide::scene::SceneLineRole::Active);
}

// Slides are filled by the height layout gives each line; paint must take no
// more and no less, or a chord slide overflows or sits short in its box.
TEST_CASE("a chord line is as tall in layout as it is painted", "[theme][draft_preview][chords]")
{
    ApproxTextMeasurer measurer;
    for (const nlohmann::json& style : {
             nlohmann::json{{"font_size", 48}, {"show_chords", true}},
             nlohmann::json{{"font_size", 48}, {"show_chords", true}, {"line_height", 2.0},
                            {"chord_size_pct", 100}, {"chord_gap_pct", 30}},
         }) {
        const auto theme =
            seventhslide::theme::resolve_styles(th::flat_style(style), style, {1920, 1080});
        const auto layout = seventhslide::build::compute_song_layout(
            theme, measurer, "---[Verse:1]---\n[G]Amazing [D]grace\n", "v1");
        REQUIRE(layout.lines.size() == 1);
        REQUIRE(layout.lines[0].has_chord_layout);
        int group = 0;
        while (layout.groups[static_cast<std::size_t>(group)].is_title) {
            ++group;
        }
        auto text = seventhslide::scene::build_song_slide_text(layout, theme, group, &measurer);
        seventhslide::scene::measure_text_content(text, measurer);
        REQUIRE(text.lines.size() == 1);
        REQUIRE(text.lines[0].metrics->height == layout.lines[0].line_height_px);
    }
}

// Without a measurer the draft has no layout and shows the sample song's lines
// one row each, or the first three as a follow-lines demo.
TEST_CASE("theme draft song sample without a measurer", "[theme][draft_preview]")
{
    using seventhslide::scene::SceneLineRole;
    const auto plain_lines = [](const seventhslide::scene::SceneFrame& frame) {
        std::vector<std::string> out;
        for (const auto& line : frame.text->lines) {
            out.push_back(seventhslide::model::plain_text(line.runs));
        }
        return out;
    };

    nlohmann::json style{{"font_size", 48}, {"follow_lines", 0}};
    auto frame = draft_frame(style, "song");
    REQUIRE(frame.text.has_value());
    CHECK(plain_lines(frame) == std::vector<std::string>{
                                    "Soon the heavens will open wide,",
                                    "Christ will come to claim His bride.",
                                    "All the universe will sing,",
                                    "Hallelujah! Christ is King!",
                                    "We have this hope, this faith and God's great love.",
                                    "We are united in Christ.",
                                });
    for (const auto& line : frame.text->lines) {
        CHECK(line.role == SceneLineRole::Normal);
        CHECK(line.type.empty());
    }

    style["follow_lines"] = 1;
    style["active_line"] = {{"size", 60}};
    style["other_lines"] = {{"color", "#777777"}};
    frame = draft_frame(style, "song");
    REQUIRE(frame.text.has_value());
    CHECK(plain_lines(frame) == std::vector<std::string>{
                                    "Soon the heavens will open wide,",
                                    "Christ will come to claim His bride.",
                                    "All the universe will sing,",
                                });
    const auto& lines = frame.text->lines;
    CHECK(lines[0].role == SceneLineRole::Active);
    CHECK(lines[0].type.size == 60);
    CHECK(lines[0].type.color.empty());  // the box's own
    for (std::size_t i = 1; i < lines.size(); ++i) {
        CHECK(lines[i].role == SceneLineRole::Follow);
        CHECK(lines[i].type.color == "#777777");
        CHECK(lines[i].type.size == 0);
    }
}

TEST_CASE("A media design's draft is a media frame with a stand-in in its area",
          "[theme][draft_preview][paint_order]")
{
    nlohmann::json style = {{"font_size", 36}};
    style["video_area_x"] = 100;
    style["video_area_y"] = 50;
    style["video_area_width"] = 800;
    style["video_area_height"] = 400;
    // A picture's or video's area shows colour bars; a page's or live
    // source's the plain plate.
    const struct {
        const char* context;
        SceneMode mode;
        const char* label;
        bool bars;
    } cases[] = {
        {"image", SceneMode::Image, "Picture", true},
        {"video", SceneMode::Video, "Video", true},
        {"web", SceneMode::Web, "Web page", false},
        {"live", SceneMode::Live, "Live video", false},
    };
    for (const auto& each : cases) {
        INFO(each.context);
        const auto frame = draft_frame(style, each.context, 1920, 1080, "preview", nullptr, true);
        CHECK(frame.mode == each.mode);
        CHECK(seventhslide::scene::paints_media(frame));
        CHECK_FALSE(frame.text.has_value());
        REQUIRE(frame.media_stand_in.has_value());
        CHECK(frame.media_stand_in->bars == each.bars);
        const auto& label = frame.media_stand_in->label;
        REQUIRE(label.lines.size() == 1);
        CHECK(seventhslide::model::plain_text(label.lines[0]) == each.label);
        // On bars the words are outlined, to read on every colour.
        CHECK((label.effects.text_outline.width > 0) == each.bars);
    }

    // The stand-in fills the area it stands in, as the theme places it.
    const auto video = draft_frame(style, "video");
    REQUIRE(video.video.has_value());
    REQUIRE(video.media_stand_in.has_value());
    CHECK(video.video->area.x == 100);
    CHECK(video.video->area.width == 800);
    CHECK(video.media_stand_in->label.x == 100);
    CHECK(video.media_stand_in->label.y == 50);
    CHECK(video.media_stand_in->label.w == 800);
    CHECK(video.media_stand_in->label.h == 400);
    // Nothing plays in it: no file, page or source.
    CHECK(video.video->url.empty());
    CHECK(video.video->source.empty());
}

TEST_CASE("A media design's draft places its area as a frame that plays does",
          "[theme][draft_preview][paint_order]")
{
    // The image area off the canvas's corner at 10, a shape over it at 20.
    auto set = th::design_of(nlohmann::json{{"font_size", 36}});
    auto& image = set.at(ElementContext::Image);
    for (auto& el : image) {
        if (el.binding == seventhslide::theme::ElementBinding::ImageArea) {
            el.object.x = 100;
            el.object.y = 50;
            el.object.w = 800;
            el.object.h = 400;
            el.object.z = 10;
        }
    }
    th::add_element(image, element_of(seventhslide::freeform::ObjectType::Shape, "te_over", 20));
    const nlohmann::json style = {{std::string(seventhslide::theme::k_elements_key), set.to_json()}};

    // The same design on an output, showing a picture.
    seventhslide::model::OutputConfig oc;
    oc.name = "Main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    th::use_design(oc, set);
    seventhslide::build::SlideBuildInput in;
    in.kind = seventhslide::build::BuildKind::Empty;
    in.selection.kind = seventhslide::model::ContentKind::Image;
    in.outputs.push_back(oc);
    ApproxTextMeasurer measurer;
    const auto built = seventhslide::build::build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    seventhslide::scene::SceneExtras extras;
    extras.image = seventhslide::scene::SceneImageMedia{.url = "/static/images/a.png"};
    const auto playing = seventhslide::scene::build_scene_frame(
        {built.outputs[0], SceneMode::Image, 1920, 1080, 0, extras});
    REQUIRE(playing.image.has_value());
    CHECK(playing.image->area.x == 100);
    CHECK(playing.image->area.width == 800);

    const auto frame =
        seventhslide::scene::build_theme_draft_frame(style, seventhslide::scene::DraftContext::Image);
    REQUIRE(frame.image.has_value());
    CHECK(frame.image->area == playing.image->area);
    CHECK(frame.image->fit == playing.image->fit);
    CHECK(frame.image->area.z == 10);
    // In the stack as on the output: the area, then the shape over it.
    const auto order = seventhslide::scene::paint_order(frame);
    const auto media_at = std::ranges::find(order, seventhslide::scene::PaintPart::Media,
                                            &seventhslide::scene::PaintStep::part);
    REQUIRE(media_at != order.end());
    const auto over_at = std::ranges::find_if(order, [&](const auto& step) {
        return step.part == seventhslide::scene::PaintPart::Object
            && frame.objects[step.index].id == "te_over";
    });
    REQUIRE(over_at != order.end());
    CHECK(media_at < over_at);
}

TEST_CASE("On the designer stage a picture not chosen yet stands in at its place",
          "[theme][draft_preview]")
{
    auto pick = element_of(seventhslide::freeform::ObjectType::Image, "te_pick", 25);
    pick.object.rotation = 15;
    auto style = designed({{"font_size", 36}}, {{ElementContext::Song, pick}});
    const auto stage = draft_frame(style, "song", 1920, 1080, "preview", nullptr, true);
    const auto it = std::ranges::find(stage.objects, std::string("te_pick"),
                                      &seventhslide::scene::SceneObject::id);
    REQUIRE(it != stage.objects.end());
    CHECK(it->z == 25);
    REQUIRE(it->text.has_value());
    CHECK(it->text->id == "te_pick");
    CHECK(it->text->x == 100);
    CHECK(it->text->w == 200);
    CHECK(it->text->rotation == 15);
    CHECK(element_text(stage, "te_pick") == "Pick an image");

    // A card shows what will paint: nothing, as on the output.
    const auto card = draft_frame(style, "song");
    CHECK(std::ranges::find(card.objects, std::string("te_pick"),
                            &seventhslide::scene::SceneObject::id)
          == card.objects.end());
}
