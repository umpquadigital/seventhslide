#include <catch2/catch_test_macros.hpp>

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/devsupport/flat_style.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/scene/chrome_text.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/scene/text_metrics.hpp>
#include <seventhslide/scene/theme_draft_preview.hpp>
#include <seventhslide/session/session.hpp>
#include <seventhslide/session/stack_runtime.hpp>
#include <seventhslide/theme/resolved_theme.hpp>
#include <seventhslide/theme/theme_elements.hpp>

#include "freeform_test_helpers.hpp"
#include "scene_test_helpers.hpp"
#include "theme_test_helpers.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

using nlohmann::json;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::model::OutputConfig;
using seventhslide::model::Song;
using seventhslide::model::ThemeEntry;
using seventhslide::scene::SceneLineRole;
using seventhslide::scene::SceneMode;
using seventhslide::scene::copyright_token_values;
using seventhslide::scene::build_scene_frame;
using seventhslide::session::build_scene_frames;
using seventhslide::scene::scene_mode_from_build_kind;
using seventhslide::build::BuildKind;
using seventhslide::session::PlayerController;
using seventhslide::session::Session;
using seventhslide::build::SlideBuildInput;
using seventhslide::build::build_slides;
using seventhslide::model::ContentKind;
namespace th = seventhslide::test;
using seventhslide::test::load_json;
using seventhslide::test::element_text;

namespace {

/// What a painter needs of any frame: a canvas to map onto the window.
void require_paintable(const seventhslide::scene::SceneFrame& frame)
{
    REQUIRE(frame.canvas_width > 0);
    REQUIRE(frame.canvas_height > 0);
}

}  // namespace


namespace {

using seventhslide::test::oracle_dir;

OutputConfig make_song_oc(std::string name = "Main", std::string id = "out-main")
{
    OutputConfig oc;
    oc.id = std::move(id);
    oc.name = std::move(name);
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    seventhslide::test::set_look(oc, {{"box_x", 100}, {"box_y", 200}, {"width_px", 1600}, {"height_px", 500}, {"font_family", "Helvetica"}, {"font_size", 48}, {"follow_lines", 2}, {"highlight_color", "#ffffff"}, {"other_lines", {{"color", "#888888"}}}, {"active_line", {{"size", 60}}}, {"background_type", "color"}, {"background_color", "#010203"}});
    return oc;
}

/// The one clock object on a frame, or null. Each design context carries its own
/// clock element, so which one this is depends on the frame's mode.
[[nodiscard]] const seventhslide::canvas::Object* clock_object(
    const seventhslide::scene::SceneFrame& frame)
{
    for (const auto& so : frame.objects) {
        if (so.object && so.object->type == seventhslide::canvas::ObjectType::Clock) {
            return &*so.object;
        }
    }
    return nullptr;
}

/// The content kind that plays in ``mode``.
[[nodiscard]] ContentKind content_kind_of(SceneMode mode)
{
    switch (mode) {
    case SceneMode::Song:
        return ContentKind::Song;
    case SceneMode::Bible:
        return ContentKind::Bible;
    case SceneMode::Freeform:
        return ContentKind::Freeform;
    case SceneMode::Image:
        return ContentKind::Image;
    case SceneMode::Video:
        return ContentKind::Video;
    case SceneMode::Audio:
        return ContentKind::Audio;
    case SceneMode::Web:
        return ContentKind::Web;
    case SceneMode::Live:
        return ContentKind::Live;
    case SceneMode::Empty:
        break;
    }
    return ContentKind::Empty;
}

/// The frame ``oc`` paints in ``mode``, under the look of what plays in it:
/// a one-verse song in Song, and in any other mode content of its kind (none
/// yet), so the frame takes that kind's theme as it would live.
[[nodiscard]] seventhslide::scene::SceneFrame frame_playing(const OutputConfig& oc,
                                                            SceneMode mode)
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.selection.kind = content_kind_of(mode);
    in.kind = BuildKind::Empty;
    if (mode == SceneMode::Song) {
        in.kind = BuildKind::Song;
        in.song.lyrics = "---[Verse:1]---\nOne\n";
        in.song.verse_order = "v1";
    }
    in.outputs.push_back(oc);
    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    return build_scene_frame({built.outputs[0], mode, 1920, 1080, 0, {}});
}

}  // namespace

TEST_CASE("scene_mode_from_build_kind maps kinds", "[scene]")
{
    REQUIRE(scene_mode_from_build_kind(BuildKind::Song) == SceneMode::Song);
    REQUIRE(scene_mode_from_build_kind(BuildKind::Bible) == SceneMode::Bible);
    REQUIRE(scene_mode_from_build_kind(BuildKind::Freeform) == SceneMode::Freeform);
    REQUIRE(scene_mode_from_build_kind(BuildKind::Empty) == SceneMode::Empty);
}

TEST_CASE("build_scene_frame resolves the clock per design context", "[scene][clock]")
{
    auto oc = make_song_oc();
    auto design = th::design_of(oc);
    th::add_element(design.song, th::clock_element("te_clock"));
    // Each media type is designed on its own: a clock on pictures and on videos.
    th::add_element(design.image, th::clock_element("te_image_clock"));
    th::add_element(design.video, th::clock_element("te_video_clock"));
    th::use_design(oc, design);  // none on bible, none on song title slides

    const auto clock_on = [&](SceneMode mode) {
        return clock_object(frame_playing(oc, mode)) != nullptr;
    };

    REQUIRE(clock_on(SceneMode::Song));
    REQUIRE(clock_on(SceneMode::Image));
    REQUIRE(clock_on(SceneMode::Video));
    // Each context is independent: bible stays dark while song is lit.
    REQUIRE_FALSE(clock_on(SceneMode::Bible));
    // An idle output paints no themed chrome, the clock included.
    REQUIRE_FALSE(clock_on(SceneMode::Empty));
    // Freeform decks own their canvas, Clock objects and all.
    REQUIRE_FALSE(clock_on(SceneMode::Freeform));
}

TEST_CASE("A body box's margins keep its words inside the element", "[scene][paragraph][margins]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.lyrics = "---[Verse:1]---\nOne\n";
    in.song.verse_order = "v1";
    auto oc = make_song_oc();
    auto design = th::design_of(oc);
    auto& lyrics = th::element_in(design.song, "te_lyrics").object;
    lyrics.paragraph.margins = {20, 10, 30, 40};
    lyrics.fill.color = "#80000000";
    th::use_design(oc, design);
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    // The element is the look's box (100, 200, 1600 x 500); the words' box
    // is that less the margins, which the box remembers for its frame.
    const auto& song = built.outputs[0].theme.song;
    CHECK(song.x == 120);
    CHECK(song.y == 210);
    CHECK(song.width == 1550);
    CHECK(song.height == 450);
    CHECK(song.margins.left == 20);
    CHECK(song.margins.bottom == 40);
    CHECK(song.fill.color == "#80000000");
    seventhslide::scene::SceneFrameRequest req{built.outputs[0], SceneMode::Song, 1920, 1080, 0, {}};
    const auto frame = build_scene_frame(req);
    REQUIRE(frame.text);
    CHECK(frame.text->box.x == 120);
    CHECK(frame.text->box.margins.right == 30);
}

TEST_CASE("build_scene_frame paints each context's own clock design", "[scene][clock]")
{
    auto oc = make_song_oc();
    auto design = th::design_of(oc);
    // Bottom left on songs, top right on Bible.
    auto song_design = th::clock_element("te_clock", 40, 980);
    song_design.object.font_color = "#00ff00";
    th::add_element(design.song, song_design);
    auto bible_design = th::clock_element("te_bible_clock", 1600, 40);
    bible_design.object.font_color = "#ffcc00";
    th::add_element(design.bible, bible_design);
    // Media is placed but never restyled, so it keeps its own default colour.
    th::add_element(design.image, th::clock_element("te_media_clock", 900, 20));
    th::use_design(oc, design);
    const auto frame_for = [&](SceneMode mode) { return frame_playing(oc, mode); };

    const auto song = frame_for(SceneMode::Song);
    const auto* song_clock = clock_object(song);
    REQUIRE(song_clock != nullptr);
    REQUIRE(song_clock->x == 40);
    REQUIRE(song_clock->y == 980);

    const auto bible = frame_for(SceneMode::Bible);
    const auto* bible_clock = clock_object(bible);
    REQUIRE(bible_clock != nullptr);
    REQUIRE(bible_clock->x == 1600);
    REQUIRE(bible_clock->y == 40);
    REQUIRE(bible_clock->font_color == "#ffcc00");

    const auto image = frame_for(SceneMode::Image);
    const auto* media_clock = clock_object(image);
    REQUIRE(media_clock != nullptr);
    REQUIRE(media_clock->x == 900);
    // Recolouring the song clock leaves the other contexts alone.
    REQUIRE(song_clock->font_color == "#00ff00");
    REQUIRE(media_clock->font_color == "#ffffff");
}

TEST_CASE("build_scene_frame fills the copyright notice's tokens", "[scene][copyright]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.lyrics = "---[Verse:1]---\nOne\n";
    in.song.verse_order = "v1";
    auto oc = make_song_oc();
    auto design = th::design_of(oc);
    th::set_copyright_text(design, "{-authors} / {-copyright} / {ccli-number}");
    th::use_design(oc, design);
    in.outputs.push_back(oc);
    const auto built = build_slides(in, measurer);

    Song meta;
    meta.show_copyright = true;
    meta.authors = {"Wayne Hooper"};
    meta.copyright = "© 1962";
    meta.ccli_song_number = "7201044";

    seventhslide::scene::SceneExtras extras;
    extras.copyright_vars = copyright_token_values(meta, "999");
    extras.song_copyright = meta.show_copyright;
    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0], SceneMode::Song, 1920, 1080, 0, extras,
    };
    const auto frame = build_scene_frame(req);
    REQUIRE(element_text(frame, "te_copyright") == "Wayne Hooper / © 1962 / 7201044");
}

TEST_CASE("a song that opts out of copyright keeps every other token", "[scene][copyright][show_when]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.lyrics = "---[Verse:1]---\nOne\n";
    in.song.verse_order = "v1";
    auto oc = make_song_oc();
    auto design = th::design_of(oc);
    th::set_copyright_text(design, "{-authors}");
    th::use_design(oc, design);
    in.outputs.push_back(oc);
    const auto built = build_slides(in, measurer);

    Song meta;
    meta.title = "We Have This Hope";
    meta.authors = {"Wayne Hooper"};
    meta.key = "A";
    meta.show_copyright = false;

    seventhslide::scene::SceneExtras extras;
    extras.copyright_vars = copyright_token_values(meta, "999");
    extras.song_copyright = meta.show_copyright;
    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0], SceneMode::Song, 1920, 1080, 0, extras,
    };
    const auto frame = build_scene_frame(req);

    // The notice is gone because the element declares itself part of it.
    REQUIRE(element_text(frame, "te_copyright").empty());

    // The tokens themselves still go out. A title design is entitled to say
    // ``{authors}`` or ``{song-title}`` for its own reasons, and the toggle is
    // not a statement about those fields.
    const auto pool = seventhslide::scene::frame_token_pool(extras);
    REQUIRE(pool.at("song-title") == "We Have This Hope");
    REQUIRE(pool.at("authors") == "Wayne Hooper");
    REQUIRE(pool.at("key") == "A");
}

TEST_CASE("a title slide's copyright notice follows the song's toggle", "[scene][copyright][show_when][title]")
{
    // Title boxes reach the frame through the overlay, not the element object
    // path, so they need the toggle applied on their own road.
    ApproxTextMeasurer measurer;
    const auto title_overlay_text = [&](bool show_copyright) {
        auto oc = make_song_oc();
        auto design = th::design_of(oc);
        th::set_copyright_text(design, "NOTICE");
        th::use_design(oc, design);

        SlideBuildInput in;
        in.kind = BuildKind::Song;
        in.selection.kind = ContentKind::Song;
        in.song.title = "We Have This Hope";
        in.song.lyrics = "---[Verse:1]---\nLine one\nLine two\n";
        in.song.verse_order = "";  // the song asks for a title slide
        in.song.show_copyright = show_copyright;
        in.outputs = {oc};
        const auto built = build_slides(in, measurer);
        REQUIRE(built.outputs[0].layout.groups[0].is_title);

        seventhslide::scene::SceneExtras extras;
        extras.copyright_vars = copyright_token_values(in.song, "");
        extras.song_copyright = show_copyright;
        seventhslide::scene::SceneFrameRequest req{
            built.outputs[0], SceneMode::Song, 1920, 1080, 0, extras,
        };
        const auto frame = build_scene_frame(req);
        std::string out;
        for (const auto& box : frame.overlay.boxes) {
            for (const auto& line : box.lines) {
                for (const auto& span : line) {
                    out += span.text;
                }
            }
        }
        return out;
    };

    REQUIRE(title_overlay_text(true) == "NOTICE");
    REQUIRE(title_overlay_text(false).empty());
}

TEST_CASE("a switched-off title design leaves the cue in the song design", "[scene][elements][title]")
{
    // Turning a theme's title slide off is a statement about this output only:
    // the cue belongs to the song and every output walks one line cursor, so the
    // slide stays put. What changes is which design paints it - the Titles
    // context, or the ordinary song one with nothing in the lyrics box.
    ApproxTextMeasurer measurer;
    const auto build_with = [&](bool design_on, int follow_lines) {
        auto oc = make_song_oc();
        seventhslide::test::set_look(oc, {{"follow_lines", follow_lines}});
        auto design = th::design_of(oc);
        th::set_copyright_text(design, "{-authors}");
        // One clock per design context: which one paints says which context the
        // frame is in.
        th::add_element(design.song, th::clock_element("te_clock"));
        th::add_element(design.titles, th::clock_element("te_title_clock", 10, 10, 20));

        ThemeEntry text;
        text.id = "t1";
        text.name = "Titles";
        json style = json::object();
        style["follow_lines"] = follow_lines;
        if (!design_on) {
            style["show_title_slides"] = false;
        }
        text.style = th::with_titles(style, design, {th::title_text_element()});

        oc.category_defaults = json{{"song", {{"theme", "t1"}}}};
        oc.song_themes = {text};

        SlideBuildInput in;
        in.kind = BuildKind::Song;
        in.selection.kind = ContentKind::Song;
        in.song.title = "A Mighty Fortress";
        in.song.lyrics = "---[Verse:1]---\nLine one\nLine two\n";
        in.song.verse_order = "";  // the song asks for a title slide
        in.outputs = {oc};
        return build_slides(in, measurer);
    };

    Song meta;
    meta.show_copyright = true;
    meta.authors = {"Wayne Hooper"};
    seventhslide::scene::SceneExtras extras;
    extras.copyright_vars = copyright_token_values(meta, "");
    extras.song_copyright = meta.show_copyright;

    const auto has_object = [](const seventhslide::scene::SceneFrame& frame,
                               std::string_view id) {
        for (const auto& so : frame.objects) {
            if (so.id == id) {
                return true;
            }
        }
        return false;
    };

    const auto title_frame = [&](const seventhslide::build::SlideBuildResult& built) {
        seventhslide::scene::SceneFrameRequest req{
            built.outputs[0], SceneMode::Song, 1920, 1080, 0, extras,
        };
        req.measurer = &measurer;
        return build_scene_frame(req);
    };

    SECTION("on: the Titles design paints and the body stays out of the way")
    {
        const auto built = build_with(true, 0);
        REQUIRE(built.outputs[0].layout.groups[0].is_title);
        const auto frame = title_frame(built);
        REQUIRE_FALSE(frame.overlay.empty());
        REQUIRE(has_object(frame, "te_title_clock"));
        REQUIRE_FALSE(has_object(frame, "te_clock"));
        REQUIRE(element_text(frame, "te_copyright").empty());
    }

    SECTION("off: the song design paints, with an empty lyrics box")
    {
        const auto built = build_with(false, 0);
        REQUIRE(built.outputs[0].layout.groups[0].is_title);
        const auto frame = title_frame(built);
        REQUIRE(frame.overlay.empty());
        REQUIRE(has_object(frame, "te_clock"));
        REQUIRE_FALSE(has_object(frame, "te_title_clock"));
        REQUIRE(element_text(frame, "te_copyright") == "Wayne Hooper");
        REQUIRE(frame.text.has_value());
        for (const auto& line : frame.text->lines) {
            REQUIRE(seventhslide::model::plain_text(line.runs).empty());
        }
    }

    SECTION("off under follow-lines: the opening lines show as upcoming")
    {
        const auto built = build_with(false, 2);
        const auto& group = built.outputs[0].layout.groups[0];
        REQUIRE(group.is_title);
        REQUIRE(group.active_count == 0);
        const auto frame = title_frame(built);
        REQUIRE(element_text(frame, "te_copyright") == "Wayne Hooper");
        REQUIRE(frame.text.has_value());
        REQUIRE(frame.text->lines.size() >= 2);
        for (const auto& line : frame.text->lines) {
            REQUIRE(line.role == SceneLineRole::Follow);
            REQUIRE_FALSE(seventhslide::model::plain_text(line.runs).empty());
        }
    }
}

TEST_CASE("a slide rule counts the slides its own design paints", "[scene][elements][visibility]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "We Have This Hope";
    in.song.lyrics = "---[Verse:1]---\nOne\n---[Verse:2]---\nTwo\n";
    // Blank verse order opts the song into a title slide.
    in.song.verse_order = "";
    auto oc = make_song_oc();
    auto design = th::design_of(oc);
    th::set_copyright_text(design, "{-authors}");
    auto& notice = th::element_in(design.song, "te_copyright");
    notice.visibility.rule = seventhslide::theme::SlideRule::First;
    notice.visibility.count = 1;
    th::use_design(oc, design);
    in.outputs.push_back(oc);
    const auto built = build_slides(in, measurer);

    const auto& groups = built.outputs[0].layout.groups;
    REQUIRE(groups.size() >= 3);
    REQUIRE(groups[0].is_title);
    REQUIRE_FALSE(groups[1].is_title);

    Song meta;
    meta.authors = {"Wayne Hooper"};
    meta.show_copyright = true;
    seventhslide::scene::SceneExtras extras;
    extras.copyright_vars = copyright_token_values(meta, "");
    extras.song_copyright = true;

    const auto text_on = [&](int slide) {
        seventhslide::scene::SceneFrameRequest req{
            built.outputs[0], SceneMode::Song, 1920, 1080, slide, extras,
        };
        return element_text(build_scene_frame(req), "te_copyright");
    };

    // The title slide is painted by the Titles design, so the Song design's
    // "first slide" is the first lyric slide - not slide 0 of the item, which
    // this design never paints and where the designer would see nothing.
    REQUIRE_FALSE(text_on(1).empty());
    REQUIRE(text_on(2).empty());
}

TEST_CASE("build_scene_frame fills the reference's tokens", "[scene][bible_ref]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 3;
    in.bible.verses = {{16, "For God so loved"}};
    auto oc = make_song_oc();
    auto design = th::design_of(oc);
    th::element_in(design.bible, "te_bible_ref").object.text =
        "{book} {chapter}:{verses} ({version})";
    th::use_design(oc, design);
    in.outputs.push_back(oc);
    const auto built = build_slides(in, measurer);

    seventhslide::model::BiblePassage passage;
    passage.book = "John";
    passage.chapter = 3;
    passage.verse_start = 16;
    passage.verse_end = 16;
    passage.version = "KJV";

    seventhslide::scene::SceneExtras extras;
    extras.bible_ref_vars = seventhslide::scene::bible_ref_token_values(passage);
    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0], SceneMode::Bible, 1920, 1080, 0, extras,
    };
    const auto frame = build_scene_frame(req);
    REQUIRE(element_text(frame, "te_bible_ref") == "John 3:16 (KJV)");
}

TEST_CASE("build_scene_frame separates title slides from song body", "[scene][clock]")
{
    auto box = seventhslide::test::title_text_element();
    box.object.font_size = 64;

    const auto clock_flags = [&](bool song_on, bool title_on) {
        OutputConfig oc = make_song_oc();
        auto design = th::design_of(oc);
        if (song_on) {
            th::add_element(design.song, th::clock_element("te_clock"));
        }
        if (title_on) {
            th::add_element(design.titles, th::clock_element("te_title_clock", 10, 10, 20));
        }
        ThemeEntry text;
        text.id = "t1";
        text.name = "Titles";
        text.style = th::with_titles(json::object(), design, {box});

        oc.category_defaults = json{{"song", {{"theme", "t1"}}}};
        oc.song_themes = {text};

        SlideBuildInput in;
        in.kind = BuildKind::Song;
        in.selection.kind = ContentKind::Song;
        in.song.title = "A Mighty Fortress";
        in.song.lyrics = "---[Verse:1]---\nLine one\nLine two\n";
        in.song.verse_order = "";  // empty -> implicit title slide
        in.outputs = {oc};

        ApproxTextMeasurer measurer;
        const auto built = build_slides(in, measurer);
        REQUIRE(built.outputs.size() == 1);
        REQUIRE(built.outputs[0].layout.groups.size() >= 2);
        REQUIRE(built.outputs[0].layout.groups[0].is_title);
        REQUIRE_FALSE(built.outputs[0].layout.groups[1].is_title);

        const auto at = [&](int slide) {
            seventhslide::scene::SceneFrameRequest req{
                built.outputs[0], SceneMode::Song, 1920, 1080, slide, {},
            };
            return clock_object(build_scene_frame(req)) != nullptr;
        };
        return std::pair<bool, bool>{at(0), at(1)};
    };

    // Title slides read the Titles context; the body reads Song. Neither
    // follows the other.
    REQUIRE(clock_flags(true, false) == std::pair<bool, bool>{false, true});
    REQUIRE(clock_flags(false, true) == std::pair<bool, bool>{true, false});
}

TEST_CASE("build_scene_frame song lines roles and blank", "[scene]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.lyrics = "---[Verse:1]---\nOne\n[---][---]\nTwo\n[---][---]\nThree\n";
    in.song.verse_order = "v1";
    in.outputs.push_back(make_song_oc());

    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    REQUIRE(built.outputs[0].layout.groups.size() >= 2);

    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0],
        SceneMode::Song,
        1920,
        1080,
        0,
        {},
    };
    auto frame = build_scene_frame(req);
    require_paintable(frame);
    REQUIRE(frame.mode == SceneMode::Song);
    REQUIRE(frame.background.type == seventhslide::layout::BackgroundType::Color);
    REQUIRE(frame.background.color == "#010203");
    REQUIRE(frame.text.has_value());
    REQUIRE(frame.text->box.x == 100);
    REQUIRE(frame.text->box.width == 1600);
    REQUIRE_FALSE(frame.text->lines.empty());
    // First slide with follow_lines: first line active, rest follow when present.
    REQUIRE(frame.text->lines[0].role == SceneLineRole::Active);
    REQUIRE(frame.text->lines[0].type.color.empty());  // the box's own colour
    REQUIRE(frame.text->lines[0].type.size == 60);

    seventhslide::scene::apply_scene_blank(frame);
    REQUIRE(frame.is_blank);
    REQUIRE_FALSE(frame.text.has_value());
    REQUIRE(frame.overlay.empty());
    REQUIRE(frame.background.color == "#010203");
}

TEST_CASE("build_scene_frame follows the player through a fixture song", "[scene][oracle]")
{
    const auto oracle = load_json(oracle_dir() / "song_simple_v1.json");
    ApproxTextMeasurer measurer;
    Session session(measurer);
    session.set_outputs({seventhslide::test::oc_from_oracle(oracle.at("outputs").at(0))});

    seventhslide::model::Song song;
    song.title = oracle.at("input").value("title", "");
    song.lyrics = oracle.at("input").at("lyrics").get<std::string>();
    song.verse_order = oracle.at("input").value("verse_order", "");
    session.play_song(std::move(song));

    auto frame = session.scene_frame(session.outputs()[0].id);
    REQUIRE(frame.has_value());
    require_paintable(*frame);
    REQUIRE(frame->mode == SceneMode::Song);
    REQUIRE(frame->text.has_value());
    REQUIRE(frame->canvas_width == oracle.at("outputs").at(0).at("canvas").at("width").get<int>());

    // Advance and ensure slide_index tracks player, not stale runtime.index.
    if (session.focused_stack().program.next_slide()) {
        auto next = session.scene_frame(session.outputs()[0].id);
        REQUIRE(next.has_value());
        REQUIRE(next->slide_index == session.focused_stack().program.player().outputs()[0].index);
    }
}

TEST_CASE("build_scene_frame paints a Freeform document as objects", "[scene]")
{
    ApproxTextMeasurer measurer;
    auto doc = seventhslide::freeform::make_document("Welcome", {"out-main"});
    seventhslide::freeform::Object text;
    text.type = seventhslide::freeform::ObjectType::Text;
    text.x = 10;
    text.y = 20;
    text.w = 80;
    text.h = 40;
    text.text = "{title}";
    doc.decks[0].slides[0].objects.push_back(text);
    doc.fields["title"] = "Hello";
    seventhslide::freeform::normalize(doc);

    SlideBuildInput in;
    in.kind = BuildKind::Freeform;
    in.selection.kind = ContentKind::Freeform;
    in.freeform.document = std::move(doc);
    in.outputs.push_back(make_song_oc("Main"));

    const auto built = build_slides(in, measurer);
    PlayerController player;
    player.apply_build_result(built);

    const auto frames = build_scene_frames(built, in.outputs, player);
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].mode == SceneMode::Freeform);
    REQUIRE_FALSE(frames[0].text.has_value());
    // Freeform text is an object, so it stacks by z with shapes and images.
    REQUIRE(frames[0].overlay.empty());
    REQUIRE_FALSE(frames[0].objects.empty());
    REQUIRE(frames[0].objects[0].text.has_value());
    REQUIRE(seventhslide::model::plain_text(frames[0].objects[0].text->lines[0]) == "Hello");
}

TEST_CASE("build_scene_frame bible text and title bg override", "[scene]")
{
    ApproxTextMeasurer measurer;
    OutputConfig oc = make_song_oc();
    seventhslide::test::set_look(oc, {{"show_bible_text", true}, {"bible_text_color", "#abcdef"}, {"bible_text_box_x", 50}});

    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 1;
    in.bible.verses = {{1, "In the beginning"}, {2, "And the earth"}};
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    REQUIRE_FALSE(built.outputs[0].slide_texts.empty());

    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0],
        SceneMode::Bible,
        1920,
        1080,
        0,
        {},
    };
    auto frame = build_scene_frame(req);
    REQUIRE(frame.text.has_value());
    REQUIRE(frame.text->box.x == 50);
    REQUIRE(frame.text->box.color == "#abcdef");
    REQUIRE_FALSE(frame.text->lines.empty());

    // Verse numbers off by default - body text only (no ch:v prefix styling).
    const auto plain0 = seventhslide::model::plain_text(frame.text->lines.front().runs);
    REQUIRE(plain0.find("1:1") != 0);

    // Per-slide bg override merges into style + SceneBackground.
    auto rt = built.outputs[0];
    rt.slide_bg_overrides = {
        seventhslide::layout::BackgroundFill{.type = seventhslide::layout::BackgroundType::Color,
                                             .color = "#ff0000"},
    };
    seventhslide::scene::SceneFrameRequest tr{
        rt,
        SceneMode::Bible,
        1920,
        1080,
        0,
        {},
    };
    auto tf = build_scene_frame(tr);
    REQUIRE(tf.background.color == "#ff0000");
}

TEST_CASE("bible verse numbers become themed superscript spans", "[scene][bible][verse]")
{
    ApproxTextMeasurer measurer;
    OutputConfig oc = make_song_oc();
    seventhslide::test::set_look(oc, {{"show_bible_text", true}, {"show_bible_verse_numbers", true}, {"bible_main_font_size", 40}, {"bible_verse_font_size", 20}, {"bible_verse_color", "#ffcc00"}, {"bible_verse_valign", "super"}, {"bible_verse_margin_right_pct", 40}, {"bible_verse_font_bold", true}});

    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 3;
    in.bible.verses = {{16, "For God so loved the world."}};
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0],
        SceneMode::Bible,
        1920,
        1080,
        0,
        {},
        &measurer,
    };
    auto frame = build_scene_frame(req);
    REQUIRE(frame.text.has_value());
    REQUIRE_FALSE(frame.text->lines.empty());
    const auto& runs = frame.text->lines.front().runs;
    REQUIRE(runs.size() >= 2);
    REQUIRE(runs[0].text == "3:16");
    REQUIRE(runs[0].style.size_pct == 50);  // 20px of 40px body
    REQUIRE(runs[0].style.baseline_rise_pct == 40);
    REQUIRE(runs[0].style.color == "#ffcc00");
    REQUIRE(runs[0].style.bold);
    REQUIRE(runs[0].style.margin_right_pct == 40);
    REQUIRE(runs[1].text.find("For God") == 0);
}

TEST_CASE("run-on bible verses style every citation on the slide", "[scene][bible][verse][fluid]")
{
    ApproxTextMeasurer measurer;
    OutputConfig oc = make_song_oc();
    seventhslide::test::set_look(oc, {{"show_bible_text", true}, {"show_bible_verse_numbers", true}, {"bible_fluid_verses", true}, {"bible_text_box_width", 1600}, {"bible_text_box_height", 400}, {"bible_main_font_size", 40}, {"bible_verse_font_size", 20}});

    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 1;
    in.bible.verses = {
        {1, "In the beginning."},
        {2, "See 3:16 here."},
        {3, "It was good."},
    };
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs[0].slide_texts.size() == 1);
    REQUIRE(built.outputs[0].slide_citations[0] == std::vector<std::string>{"1:1", "1:2", "1:3"});

    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0], SceneMode::Bible, 1920, 1080, 0, {}, &measurer,
    };
    const auto frame = build_scene_frame(req);
    REQUIRE(frame.text.has_value());

    std::vector<std::string> cited;
    for (const auto& line : frame.text->lines) {
        for (std::size_t i = 0; i < line.runs.size(); ++i) {
            const auto& span = line.runs[i];
            if (span.style.baseline_rise_pct == 0) {
                continue;
            }
            cited.push_back(span.text);
            REQUIRE(span.style.size_pct == 50);
            // The layout space after a citation is replaced by its margin.
            if (i + 1 < line.runs.size()) {
                REQUIRE(line.runs[i + 1].text.front() != ' ');
            }
        }
    }
    // "3:16" in the verse text is not a citation of this slide, so it stays body text.
    REQUIRE(cited == std::vector<std::string>{"1:1", "1:2", "1:3"});
}

TEST_CASE("build_scene_frame image/video force black background", "[scene]")
{
    ApproxTextMeasurer measurer;
    OutputConfig oc = make_song_oc();
    seventhslide::test::set_look(oc, {{"background_type", "image"}, {"background_color", "#ff00ff"}, {"background_image", "/static/theme_backgrounds/old.png"}, {"background_video", "/static/theme_backgrounds/loop.mp4"}});

    SlideBuildInput in;
    in.kind = BuildKind::Empty;
    in.selection.kind = ContentKind::Video;
    in.outputs.push_back(oc);
    const auto built = build_slides(in, measurer);

    seventhslide::scene::SceneExtras extras;
    extras.image = seventhslide::scene::SceneImageMedia{
        .url = "/static/images/a.png",
        .index = 0,
        .count = 1,
    };
    seventhslide::scene::SceneFrameRequest image_req{
        built.outputs[0],
        SceneMode::Image,
        1920,
        1080,
        0,
        extras,
    };
    auto image_frame = build_scene_frame(image_req);
    REQUIRE(image_frame.background.type == seventhslide::layout::BackgroundType::Color);
    REQUIRE(image_frame.background.color == "#000000");
    REQUIRE(image_frame.background.image.empty());
    REQUIRE(image_frame.background.video.empty());
    REQUIRE(image_frame.background.type == seventhslide::layout::BackgroundType::Color);

    extras.image = std::nullopt;
    extras.video = seventhslide::scene::SceneVideoMedia{.url = "/static/videos/clip.mp4"};
    seventhslide::scene::SceneFrameRequest video_req{
        built.outputs[0],
        SceneMode::Video,
        1920,
        1080,
        0,
        extras,
    };
    auto video_frame = build_scene_frame(video_req);
    REQUIRE(video_frame.background.type == seventhslide::layout::BackgroundType::Color);
    REQUIRE(video_frame.background.color == "#000000");
    REQUIRE(video_frame.background.image.empty());
    REQUIRE(video_frame.background.video.empty());
}

TEST_CASE("build_scene_frame fits a web page into the theme's Web area", "[scene][web]")
{
    ApproxTextMeasurer measurer;
    OutputConfig oc = make_song_oc();
    // The page in the right half of the canvas, cropped to fill it.
    auto set = seventhslide::theme::default_elements();
    for (auto& el : set.web) {
        if (el.binding == seventhslide::theme::ElementBinding::WebArea) {
            el.object.x = 960;
            el.object.y = 0;
            el.object.w = 960;
            el.object.h = 1080;
            el.object.object_fit = "cover";
        }
    }
    seventhslide::test::set_look(oc, {{"elements", set.to_json()}});

    SlideBuildInput in;
    in.kind = BuildKind::Empty;
    in.selection.kind = ContentKind::Web;
    in.outputs.push_back(oc);
    const auto built = build_slides(in, measurer);
    seventhslide::scene::SceneExtras extras;
    extras.web = seventhslide::scene::SceneWebMedia{.source = "page", .url = "https://example.com"};
    const auto frame = build_scene_frame({built.outputs[0], SceneMode::Web, 1920, 1080, 0, extras});
    REQUIRE(frame.web.has_value());
    CHECK(frame.web->area.enabled);
    CHECK(frame.web->area.x == 960);
    CHECK(frame.web->area.width == 960);
    CHECK(frame.web->area.height == 1080);
    CHECK(frame.web->fit == "cover");
    CHECK(frame.background.color == "#000000");

    // A theme without one shows no page, as one without an Image area shows
    // no image: the canvas is black.
    std::erase_if(set.web, [](const auto& el) {
        return el.binding == seventhslide::theme::ElementBinding::WebArea;
    });
    seventhslide::test::set_look(oc, {{"elements", set.to_json()}});
    in.outputs = {oc};
    const auto bare = build_slides(in, measurer);
    const auto whole = build_scene_frame({bare.outputs[0], SceneMode::Web, 1920, 1080, 0, extras});
    REQUIRE(whole.web.has_value());
    CHECK_FALSE(whole.web->area.enabled);
    CHECK(whole.background.color == "#000000");
}

TEST_CASE("build_scene_frame video background theme", "[scene]")
{
    ApproxTextMeasurer measurer;
    OutputConfig oc = make_song_oc();
    seventhslide::test::set_look(oc, {{"background_type", "video"}, {"background_color", "#102030"}, {"background_video", "/static/theme_backgrounds/loop.mp4"}});

    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "Hi";
    in.song.lyrics = "---[Verse:1]---\nOne\n";
    in.song.verse_order = "v1";
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0],
        SceneMode::Song,
        1920,
        1080,
        0,
        {},
    };
    auto frame = build_scene_frame(req);
    REQUIRE(frame.background.type == seventhslide::layout::BackgroundType::Video);
    REQUIRE(frame.background.video == "/static/theme_backgrounds/loop.mp4");
    REQUIRE(frame.background.image.empty());
    REQUIRE(frame.background.color == "#102030");
}

TEST_CASE("build_scene_frame expands long song lines into visual wraps", "[scene][wrap]")
{
    ApproxTextMeasurer measurer;
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    // ApproxTextMeasurer: ~0.5*size per char -> wide glyphs.
    seventhslide::test::set_look(oc, {{"box_x", 0}, {"box_y", 0}, {"width_px", 200}, {"height_px", 400},
                                      {"font_family", "X"}, {"font_size", 40}, {"follow_lines", 0},
                                      {"balance_wrapped_lines", false}, {"background_type", "color"},
                                      {"background_color", "#000000"}});

    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.lyrics =
        "---[Verse:1]---\n"
        "one two three four five six seven eight nine ten\n";
    in.song.verse_order = "v1";
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    REQUIRE_FALSE(built.outputs.empty());
    // Layout must bake wrap_fragments for the tall line.
    bool baked = false;
    for (const auto& ln : built.outputs[0].layout.lines) {
        if (ln.wrap_fragments.size() > 1) {
            baked = true;
            break;
        }
    }
    REQUIRE(baked);

    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0],
        SceneMode::Song,
        1920,
        1080,
        0,
        {},
        &measurer,
    };
    auto frame = build_scene_frame(req);
    REQUIRE(frame.text.has_value());
    // One logical lyric -> multiple SceneTextLine visual rows.
    REQUIRE(frame.text->lines.size() > 1);
}

TEST_CASE("build_scene_frame wraps long bible slide text", "[scene][wrap]")
{
    ApproxTextMeasurer measurer;
    seventhslide::build::OutputRuntime rt;
    rt.name = "Main";
    rt.output_id = "out-main";
    rt.theme = seventhslide::test::resolved_from_flat({
        {"show_bible_text", true},
        {"bible_text_box_x", 0},
        {"bible_text_box_y", 0},
        {"bible_text_box_width", 120},
        {"bible_text_box_height", 400},
        {"bible_main_font_family", "X"},
        {"bible_main_font_size", 40},
        {"bible_text_color", "#ffffff"},
    });
    rt.slide_texts = {
        "For God so loved the world that he gave his only Son"};

    seventhslide::scene::SceneFrameRequest req{
        rt,
        SceneMode::Bible,
        1920,
        1080,
        0,
        {},
        &measurer,
    };
    auto frame = build_scene_frame(req);
    REQUIRE(frame.text.has_value());
    REQUIRE(frame.text->lines.size() > 1);
}

TEST_CASE("build_scene_frame freeform objects and overlay", "[scene][freeform]")
{
    ApproxTextMeasurer measurer;
    std::ifstream in(std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "freeform"
                     / "simple_two_outputs.json");
    REQUIRE(in);
    json j;
    in >> j;

    OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    OutputConfig lt;
    lt.name = "Lower Third";
    lt.id = "out-lt";

    SlideBuildInput input;
    input.kind = BuildKind::Freeform;
    input.selection.kind = ContentKind::Freeform;
    input.freeform.document = seventhslide::freeform::Document::from_json(j);
    input.outputs = {main, lt};

    const auto built = build_slides(input, measurer);
    PlayerController player;
    player.apply_build_result(built);

    auto frames = build_scene_frames(built, input.outputs, player, {}, SceneMode::Freeform);
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0].mode == SceneMode::Freeform);
    REQUIRE_FALSE(frames[0].text.has_value());
    REQUIRE(frames[0].overlay.empty());
    REQUIRE_FALSE(frames[0].objects.empty());
    REQUIRE(seventhslide::model::plain_text(frames[0].objects[0].text->lines[0]) == "Sunday Worship");
    REQUIRE(frames[1].objects.empty());

    REQUIRE(player.jump_to_line(2));
    frames = build_scene_frames(built, input.outputs, player, {}, SceneMode::Freeform);
    REQUIRE(seventhslide::model::plain_text(frames[0].objects[0].text->lines[0]) == "Main only — slot 3");
    REQUIRE(seventhslide::model::plain_text(frames[1].objects[0].text->lines[0]) == "LT slot 3");
}

TEST_CASE("A theme indicator sets its labels its gap apart", "[scene][indicator]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.lyrics = "---[Verse:1]---\nOne\n\n---[Chorus:1]---\nTwo\n";
    in.song.verse_order = "v1 c1";

    auto oc = make_song_oc();
    auto design = th::design_of(oc);
    auto indicator = th::indicator_element();
    indicator.object.indicator_gap = 2.0;
    th::add_element(design.song, indicator);
    th::use_design(oc, design);
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0], SceneMode::Song, 1920, 1080, 0, {},
    };
    const auto frame = build_scene_frame(req);
    const seventhslide::layout::OverlayBox* box = nullptr;
    for (const auto& so : frame.objects) {
        if (so.id == "te_indicator" && so.text) {
            box = &*so.text;
        }
    }
    REQUIRE(box != nullptr);
    REQUIRE(box->lines.size() == 1);
    CHECK(box->lines[0]
          == seventhslide::scene::indicator_line({{"V1", true}, {"C1", false}}, 2.0));
}

TEST_CASE("A theme indicator shows a verse played twice running twice", "[scene][indicator]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.lyrics = "---[Verse:1]---\nOne\n\n---[Verse:2]---\nTwo\n\n---[Verse:3]---\nThree\n\n"
                     "---[Verse:4]---\nFour\n";
    in.song.verse_order = "V1 V2 V3 V3 V4";

    auto oc = make_song_oc();
    auto design = th::design_of(oc);
    th::add_element(design.song, th::indicator_element());
    th::use_design(oc, design);
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    const auto& rt = built.outputs[0];
    REQUIRE(rt.verse_codes == std::vector<std::string>{"v1", "v2", "v3", "v3", "v4"});

    // The slide each V3 starts on.
    const auto slide_of_section = [&rt](int section) {
        for (std::size_t i = 0; i < rt.verse_indices.size() && i < rt.line_to_slide.size(); ++i) {
            if (rt.verse_indices[i] == section) {
                return rt.line_to_slide[i];
            }
        }
        return -1;
    };
    const auto indicator_on = [&](int slide) {
        seventhslide::scene::SceneFrameRequest req{rt, SceneMode::Song, 1920, 1080, slide, {}};
        const auto frame = build_scene_frame(req);
        for (const auto& so : frame.objects) {
            if (so.id == "te_indicator" && so.text && !so.text->lines.empty()) {
                return so.text->lines[0];
            }
        }
        return seventhslide::model::RichLine{};
    };
    using seventhslide::scene::indicator_line;
    const int first = slide_of_section(2);
    const int again = slide_of_section(3);
    REQUIRE(first >= 0);
    REQUIRE(again > first);
    CHECK(indicator_on(first)
          == indicator_line({{"V1", false}, {"V2", false}, {"V3", true}, {"V3", false}, {"V4", false}}));
    CHECK(indicator_on(again)
          == indicator_line({{"V1", false}, {"V2", false}, {"V3", false}, {"V3", true}, {"V4", false}}));
}

TEST_CASE("A Freeform indicator shows a verse played twice running twice", "[scene][indicator][freeform]")
{
    // V1 V2 V3 V3 V4 as cues: each V3 is two lines on one slide, and the
    // repeat's first line starts a section of its own.
    const auto cue = [](const char* id, const char* text, int number, bool starts = false) {
        json j = {{"id", id}, {"text", text}, {"verse_type", "Verse"}, {"verse_number", number}};
        if (starts) {
            j["section_start"] = true;
        }
        return j;
    };
    json slides = json::array();
    for (int i = 1; i <= 5; ++i) {
        slides.push_back({{"id", "s" + std::to_string(i)}, {"objects", json::array()}});
    }
    auto doc = seventhslide::freeform::Document::from_json({
        {"schema", "seventhslide.freeform"},
        {"schema_version", seventhslide::freeform::k_schema_version},
        {"name", "Repeat"},
        {"line_control", true},
        {"next_id", 100},
        {"cues",
         {cue("c1", "one", 1), cue("c2", "two", 2), cue("c3", "three", 3), cue("c4", "more", 3),
          cue("c5", "three", 3, true), cue("c6", "more", 3), cue("c7", "four", 4)}},
        {"decks",
         {{{"output", "out-main"},
           {"canvas", {{"width", 1920}, {"height", 1080}}},
           {"line_to_slide", {0, 1, 2, 2, 3, 3, 4}},
           {"slides", slides}}}},
    });
    // One indicator per slide, all with one id: a frame has its slide's.
    for (int slide = 0; slide < 5; ++slide) {
        auto indicator = seventhslide::freeform::make_indicator_object(doc, {1920, 1080});
        indicator.id = "indicator";
        REQUIRE(seventhslide::test::add_object(doc, "out-main", slide, std::move(indicator)));
    }
    seventhslide::freeform::normalize(doc);
    REQUIRE(doc.cues[4].section_start);

    ApproxTextMeasurer measurer;
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    const auto indicator_at = [&](const seventhslide::freeform::Document& d, int line) {
        SlideBuildInput in;
        in.kind = BuildKind::Freeform;
        in.selection.kind = ContentKind::Freeform;
        in.freeform.document = d;
        in.outputs = {oc};
        const auto built = build_slides(in, measurer);
        REQUIRE(built.outputs.size() == 1);
        const auto& rt = built.outputs[0];
        REQUIRE(line < static_cast<int>(rt.line_to_slide.size()));
        seventhslide::scene::SceneFrameRequest req{
            rt, SceneMode::Freeform, 1920, 1080,
            rt.line_to_slide[static_cast<std::size_t>(line)], {}, &measurer, line,
        };
        const auto frame = build_scene_frame(req);
        for (const auto& so : frame.objects) {
            if (so.id == "indicator" && so.text && !so.text->lines.empty()) {
                return so.text->lines[0];
            }
        }
        return seventhslide::model::RichLine{};
    };

    using seventhslide::scene::indicator_line;
    // Both lines of the first V3 light the first; both of the repeat, the second.
    for (const int line : {2, 3}) {
        CHECK(indicator_at(doc, line)
              == indicator_line({{"V1", false}, {"V2", false}, {"V3", true}, {"V3", false}, {"V4", false}}));
    }
    for (const int line : {4, 5}) {
        CHECK(indicator_at(doc, line)
              == indicator_line({{"V1", false}, {"V2", false}, {"V3", false}, {"V3", true}, {"V4", false}}));
    }

    // Without the mark the four V3 lines are one section, as before.
    REQUIRE(seventhslide::freeform::set_cue_section_start(doc, 4, false));
    CHECK(indicator_at(doc, 4)
          == indicator_line({{"V1", false}, {"V2", false}, {"V3", true}, {"V4", false}}));
}

TEST_CASE("show_lyrics hides the song body and keeps the chrome", "[scene][show_lyrics]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.lyrics = "---[Verse:1]---\nOne\n[---][---]\nTwo\n";
    in.song.verse_order = "v1";

    auto oc = make_song_oc();
    seventhslide::test::set_look(oc, {{"show_lyrics", false}});
    auto design = th::design_of(oc);
    th::add_element(design.song, th::indicator_element());
    th::use_design(oc, design);
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    // Layout still runs: hiding the box must not renumber lines for the other
    // outputs, which share one global line index.
    REQUIRE(built.outputs[0].layout.groups.size() >= 1);

    seventhslide::scene::SceneExtras extras;
    extras.copyright_vars = copyright_token_values(
        [] {
            Song song;
            song.show_copyright = true;
            song.authors = {"Writer"};
            return song;
        }(),
        "");
    extras.song_copyright = true;

    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0], SceneMode::Song, 1920, 1080, 0, extras,
    };
    const auto frame = build_scene_frame(req);
    require_paintable(frame);
    REQUIRE_FALSE(frame.text.has_value());
    REQUIRE(frame.background.color == "#010203");
    REQUIRE_FALSE(element_text(frame, "te_indicator").empty());
    REQUIRE_FALSE(element_text(frame, "te_copyright").empty());
}

TEST_CASE("Follow-lines sets the active line and the other lines in their own types", "[scene][lines]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.lyrics = "---[Verse:1]---\nOne\n[---][---]\nTwo\n[---][---]\nThree\n";
    in.song.verse_order = "v1";
    auto oc = make_song_oc();
    seventhslide::test::set_look(
        oc, {{"font_size", 48},  // a body key: the look's elements are made again
             {"follow_lines", 2},
             {"active_line", {{"family", "Georgia"}, {"bold", true}, {"underline", true}}},
             {"other_lines", {{"size", 24}, {"italic", true}, {"color", "#777777"}}}});
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    seventhslide::scene::SceneFrameRequest req{built.outputs[0], SceneMode::Song, 1920, 1080, 0, {}};
    auto frame = build_scene_frame(req);
    require_paintable(frame);
    REQUIRE(frame.text.has_value());
    REQUIRE(frame.text->lines.size() >= 2);
    const auto& box = frame.text->box;
    const auto& active = frame.text->lines[0];
    const auto& other = frame.text->lines[1];
    REQUIRE(active.role == SceneLineRole::Active);
    REQUIRE(active.type.family == "Georgia");
    REQUIRE(active.type.bold == true);
    REQUIRE(other.role == SceneLineRole::Follow);
    REQUIRE(other.type.size == 24);
    REQUIRE(other.type.italic == true);
    REQUIRE(other.type.color == "#777777");

    // Over the box: the active line keeps its size and colour; the other
    // lines keep its family.
    const auto a = seventhslide::canvas::painted_type(box, active.type);
    REQUIRE(a.size == 48);
    REQUIRE(a.color == "#ffffff");
    REQUIRE(a.underline);
    const auto o = seventhslide::canvas::painted_type(box, other.type);
    REQUIRE(o.family == "Helvetica");
    REQUIRE_FALSE(o.bold);

    // Each line is measured in its own type: the other lines' rows are half
    // the active line's.
    seventhslide::scene::measure_text_content(*frame.text, measurer);
    REQUIRE(active.metrics.has_value());
    REQUIRE(other.metrics.has_value());
    REQUIRE(other.metrics->height < active.metrics->height);
}

TEST_CASE("An audio frame cuts what its stack showed, whatever the themes say",
          "[scene][transitions][audio]")
{
    ApproxTextMeasurer measurer;
    OutputConfig oc = make_song_oc();
    // Every content type fades, and so does the background.
    seventhslide::test::set_look(
        oc, {{"transitions", seventhslide::test::every_transition(
                                 {seventhslide::model::TransitionType::Fade, 600})}});
    oc.bg_themes.front().style["bg_enable_fade"] = true;
    oc.bg_themes.front().style["bg_fade_duration"] = 900;
    SlideBuildInput in;
    in.kind = BuildKind::Empty;
    in.selection.kind = ContentKind::Audio;
    in.outputs.push_back(oc);
    const auto built = build_slides(in, measurer);
    const auto audio = build_scene_frame({built.outputs[0], SceneMode::Audio, 1920, 1080, 0, {}});
    // Audio is no theme context: it paints nothing, and comes in as a stack
    // cleared goes, by a cut.
    CHECK_FALSE(audio.content_transition.plays());
    CHECK(audio.background.type == seventhslide::scene::SceneBackground{}.type);
    const auto video = build_scene_frame({built.outputs[0], SceneMode::Video, 1920, 1080, 0, {}});
    CHECK(video.content_transition.plays());
}

TEST_CASE("A frame changes by its own content type's transition", "[scene][transitions][contexts]")
{
    ApproxTextMeasurer measurer;
    OutputConfig oc = make_song_oc();
    seventhslide::test::set_look(oc, {{"transitions", seventhslide::test::every_transition({})}});
    auto& style = oc.media_themes.front().style;
    seventhslide::theme::set_context_transition(
        style, seventhslide::theme::ElementContext::Video,
        {seventhslide::model::TransitionType::Fade, 700});
    // The Live area is its own: a source goes in the right half.
    auto set = seventhslide::theme::ElementSet::from_json(seventhslide::theme::element_model(style));
    for (auto& el : set.live) {
        el.object.x = 960;
        el.object.w = 960;
    }
    style["elements"] = set.to_json();
    oc.bg_themes.front().style["bg_enable_fade"] = true;
    oc.bg_themes.front().style["bg_fade_duration"] = 900;

    SlideBuildInput in;
    in.kind = BuildKind::Empty;
    in.selection.kind = ContentKind::Video;
    in.outputs.push_back(oc);
    const auto built = build_slides(in, measurer);
    seventhslide::scene::SceneExtras extras;
    extras.video = seventhslide::scene::SceneVideoMedia{.url = "/static/videos/clip.mp4"};
    const auto video = build_scene_frame({built.outputs[0], SceneMode::Video, 1920, 1080, 0, extras});
    CHECK(video.content_transition
          == seventhslide::model::Transition{seventhslide::model::TransitionType::Fade, 700});
    // The black under a video is part of it: it comes in as the video does,
    // and no background theme's fade reaches it (media takes none).
    CHECK(video.background_transition == video.content_transition);
    const auto image = build_scene_frame({built.outputs[0], SceneMode::Image, 1920, 1080, 0, {}});
    CHECK_FALSE(image.content_transition.plays());
    const auto empty = build_scene_frame({built.outputs[0], SceneMode::Empty, 1920, 1080, 0, {}});
    CHECK_FALSE(empty.content_transition.plays());

    extras = {};
    extras.live = seventhslide::scene::SceneLiveMedia{};
    const auto live = build_scene_frame({built.outputs[0], SceneMode::Live, 1920, 1080, 0, extras});
    REQUIRE(live.live.has_value());
    CHECK(live.live->area.x == 960);
    CHECK(live.live->area.width == 960);
    REQUIRE(video.video.has_value());
    CHECK(video.video->area.x == 0);
}

TEST_CASE("Each slot of a theme carries its element's place in the stack onto the frame",
          "[scene][paint_order]")
{
    using seventhslide::theme::ElementBinding;
    ApproxTextMeasurer measurer;
    OutputConfig oc = make_song_oc();
    auto set = seventhslide::theme::default_elements();
    const auto set_z = [](std::vector<seventhslide::theme::Element>& list, ElementBinding binding,
                          int z) {
        for (auto& el : list) {
            if (el.binding == binding) {
                el.object.z = z;
            }
        }
    };
    set_z(set.song, ElementBinding::Lyrics, 7);
    set_z(set.bible, ElementBinding::BibleText, 8);
    set_z(set.image, ElementBinding::ImageArea, 10);
    set_z(set.video, ElementBinding::VideoArea, 11);
    set_z(set.web, ElementBinding::WebArea, 12);
    set_z(set.live, ElementBinding::VideoArea, 13);
    auto title = seventhslide::theme::make_element(seventhslide::freeform::ObjectType::Text,
                                                   ElementBinding::Static, "te_title", {1920, 1080});
    title.object.text = "{song-title}";
    title.object.z = 14;
    set.titles.push_back(title);
    seventhslide::test::set_look(oc, {{"elements", set.to_json()}});

    const json style = {{"elements", set.to_json()}};
    const auto theme = seventhslide::theme::resolve_styles(style, {}, {1920, 1080});
    CHECK(theme.song.z == 7);
    CHECK(theme.bible.z == 8);
    CHECK(theme.image.z == 10);
    CHECK(theme.video.z == 11);
    CHECK(theme.web.z == 12);
    CHECK(theme.live.z == 13);
    const auto titles = seventhslide::theme::title_boxes(theme.elements, {});
    REQUIRE(titles.size() == 1);
    CHECK(titles.front().z == 14);

    const auto song = frame_playing(oc, SceneMode::Song);
    REQUIRE(song.text.has_value());
    CHECK(song.text->z == 7);

    // A passage, and a song's title slide.
    {
        SlideBuildInput in;
        in.kind = BuildKind::Bible;
        in.selection.kind = ContentKind::Bible;
        in.bible.chapter = 3;
        in.bible.verses = {{16, "For God so loved"}};
        in.outputs.push_back(oc);
        const auto built = build_slides(in, measurer);
        const auto bible = build_scene_frame({built.outputs[0], SceneMode::Bible, 1920, 1080, 0, {}});
        REQUIRE(bible.text.has_value());
        CHECK(bible.text->z == 8);
    }
    {
        SlideBuildInput in;
        in.kind = BuildKind::Song;
        in.selection.kind = ContentKind::Song;
        in.song.title = "We Have This Hope";
        in.song.lyrics = "---[Verse:1]---\nLine one\n";
        in.song.verse_order = "";  // the song asks for a title slide
        in.outputs.push_back(oc);
        const auto built = build_slides(in, measurer);
        REQUIRE(built.outputs[0].layout.groups[0].is_title);
        const auto title_slide =
            build_scene_frame({built.outputs[0], SceneMode::Song, 1920, 1080, 0, {}});
        const auto it = std::ranges::find_if(title_slide.overlay.boxes, [](const auto& box) {
            std::string text;
            for (const auto& line : box.lines) {
                for (const auto& span : line) {
                    text += span.text;
                }
            }
            return text.find("Hope") != std::string::npos;
        });
        REQUIRE(it != title_slide.overlay.boxes.end());
        CHECK(it->z == 14);
    }

    const auto media_frame = [&](SceneMode mode) {
        SlideBuildInput in;
        in.kind = BuildKind::Empty;
        in.selection.kind = content_kind_of(mode);
        in.outputs.push_back(oc);
        const auto built = build_slides(in, measurer);
        seventhslide::scene::SceneExtras extras;
        extras.image = seventhslide::scene::SceneImageMedia{.url = "/static/images/a.png"};
        extras.video = seventhslide::scene::SceneVideoMedia{.url = "/static/videos/a.mp4"};
        extras.web = seventhslide::scene::SceneWebMedia{.url = "https://example.org"};
        extras.live = seventhslide::scene::SceneLiveMedia{};
        return build_scene_frame({built.outputs[0], mode, 1920, 1080, 0, extras});
    };
    CHECK(media_frame(SceneMode::Image).image->area.z == 10);
    CHECK(media_frame(SceneMode::Video).video->area.z == 11);
    CHECK(media_frame(SceneMode::Web).web->area.z == 12);
    CHECK(media_frame(SceneMode::Live).live->area.z == 13);

    // A design preview's area is the same, in the stack too.
    const auto card = seventhslide::scene::build_theme_draft_frame(
        style, seventhslide::scene::DraftContext::Image);
    REQUIRE(card.image.has_value());
    CHECK(card.image->area.z == 10);
}
