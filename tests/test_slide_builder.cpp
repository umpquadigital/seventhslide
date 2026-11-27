#include <catch2/catch_test_macros.hpp>

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/devsupport/flat_style.hpp>
#include <seventhslide/devsupport/scene_dump.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/theme/song_theme_pack.hpp>
#include <seventhslide/theme/theme_resolver.hpp>

#include "scene_test_helpers.hpp"
#include "theme_test_helpers.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using nlohmann::json;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::devsupport::serialize_output_layout;
using seventhslide::model::OutputConfig;
using seventhslide::model::ThemeEntry;
using seventhslide::scene::SceneLineRole;
using seventhslide::scene::SceneMode;
using seventhslide::scene::build_scene_frame;
using seventhslide::build::BuildKind;
using seventhslide::build::SlideBuildInput;
using seventhslide::devsupport::bg_override_present_flags;
using seventhslide::build::build_slides;
using seventhslide::devsupport::overlay_nonempty_flags;
using seventhslide::build::pad_line_to_slide;
using seventhslide::build::set_empty_slides;
using seventhslide::model::ContentKind;
using seventhslide::theme::ThemeSelection;
using seventhslide::test::load_json;

namespace {

using seventhslide::test::oracle_dir;

/// A text theme whose Titles context holds one ``{song-title}`` box.
ThemeEntry titled_text_theme(std::string id)
{
    auto box = seventhslide::test::title_text_element();
    box.object.font_size = 64;

    ThemeEntry text;
    text.id = std::move(id);
    text.name = "Titles";
    text.style = seventhslide::test::style_with_titles(json::object(), {box});
    return text;
}

/// A one-slide Freeform item per deck, each holding a ``{title}`` = "Hello" box.
seventhslide::freeform::Document hello_document(const std::vector<std::string>& decks)
{
    auto doc = seventhslide::freeform::make_document("Welcome", decks);
    for (auto& deck : doc.decks) {
        seventhslide::freeform::Object text;
        text.type = seventhslide::freeform::ObjectType::Text;
        text.w = 100;
        text.h = 100;
        text.bold = true;
        text.text = "{title}";
        deck.slides[0].objects.push_back(text);
    }
    doc.fields["title"] = "Hello";
    seventhslide::freeform::normalize(doc);
    return doc;
}

} // namespace

TEST_CASE("pad_line_to_slide and set_empty_slides", "[session]")
{
    std::vector<int> m = {0, 1};
    pad_line_to_slide(m, 4, 1);
    REQUIRE(m == std::vector<int>{0, 1, 1, 1});
    pad_line_to_slide(m, 2, 1);
    REQUIRE(m == std::vector<int>{0, 1});

    seventhslide::build::OutputRuntime rt;
    rt.name = "X";
    set_empty_slides(rt, 3);
    REQUIRE(rt.slide_texts == std::vector<std::string>{""});
    REQUIRE(rt.line_to_slide == std::vector<int>{0, 0, 0});
    REQUIRE(rt.slide_count() == 1);
}

TEST_CASE("SlideBuilder song path matches the song fixtures", "[session][oracle]")
{
    const auto manifest = load_json(oracle_dir() / "manifest.json");
    ApproxTextMeasurer measurer;

    for (const auto& id_j : manifest.at("fixtures")) {
        const auto id = id_j.get<std::string>();
        INFO(id);
        const auto oracle = load_json(oracle_dir() / (id + ".json"));

        SlideBuildInput in;
        in.kind = BuildKind::Song;
        in.selection.kind = ContentKind::Song;
        in.song.lyrics = oracle.at("input").at("lyrics").get<std::string>();
        in.song.verse_order = oracle.at("input").value("verse_order", "");
        in.song.title = oracle.at("input").value("title", "");
        in.outputs.push_back(seventhslide::test::oc_from_oracle(oracle.at("outputs").at(0)));

        const auto built = build_slides(in, measurer);
        REQUIRE(built.kind == BuildKind::Song);
        REQUIRE(built.outputs.size() == 1);
        REQUIRE(built.player.total_lines == oracle.at("player").at("total_lines").get<int>());
        REQUIRE(built.player.all_lines
                == oracle.at("player").at("all_lines").get<std::vector<std::string>>());
        REQUIRE(built.player.all_line_labels
                == oracle.at("player").at("all_line_labels").get<std::vector<std::string>>());

        const auto& rt = built.outputs[0];
        const auto serialized = serialize_output_layout(
            in.outputs[0].name,
            in.outputs[0].canvas_width,
            in.outputs[0].canvas_height,
            rt.theme,
            rt.layout,
            overlay_nonempty_flags(rt),
            bg_override_present_flags(rt),
            built.player.total_lines);

        REQUIRE(serialized.at("groups") == oracle.at("outputs").at(0).at("groups"));
        REQUIRE(serialized.at("line_to_slide") == oracle.at("outputs").at(0).at("line_to_slide"));
        REQUIRE(serialized.at("verse_codes") == oracle.at("outputs").at(0).at("verse_codes"));
        REQUIRE(serialized.at("verse_indices") == oracle.at("outputs").at(0).at("verse_indices"));
        REQUIRE(serialized.at("all_lines") == oracle.at("outputs").at(0).at("all_lines"));
        REQUIRE(serialized.at("line_labels") == oracle.at("outputs").at(0).at("line_labels"));
        REQUIRE(serialized.at("overlay_nonempty")
                == oracle.at("outputs").at(0).at("overlay_nonempty"));
        REQUIRE(serialized.at("bg_override_present")
                == oracle.at("outputs").at(0).at("bg_override_present"));
    }
}

TEST_CASE("SlideBuilder applies theme cascade then layouts", "[session]")
{
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"font_size", 20}});
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    seventhslide::test::set_look(oc, {{"width_px", 1000}, {"height_px", 800}});
    oc.song_themes = {
        ThemeEntry{"t_big", "Big", seventhslide::test::flat_style({{"font_size", 72}, {"align", "left"}})},
    };
    oc.bg_themes = {
        ThemeEntry{"b1", "BG", {{"background_color", "#112233"}}},
    };
    oc.category_defaults = {{"song", {{"theme", "t_big"}, {"bg", "b1"}}}};

    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.lyrics = "---[Verse:1]---\nHello world\n";
    in.song.verse_order = "v1";
    in.outputs = {oc};

    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    REQUIRE(built.outputs[0].theme.song.font_size == 72);
    REQUIRE(built.outputs[0].theme.background.color == "#112233");
    REQUIRE(in.outputs[0].song_themes[0].style.at("font_size") == 72);
    REQUIRE(built.player.total_lines >= 1);
}

TEST_CASE("SlideBuilder song title_slide overlays", "[session][title]")
{
    const ThemeEntry text = titled_text_theme("t1");

    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
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
    REQUIRE(built.outputs[0].layout.groups.size() >= 1);
    REQUIRE(built.outputs[0].layout.groups[0].is_title);
    REQUIRE(built.outputs[0].slide_overlays.size()
            == built.outputs[0].layout.groups.size());
    REQUIRE_FALSE(built.outputs[0].slide_overlays[0].empty());
    REQUIRE(built.outputs[0].slide_overlays[0].boxes.size() == 1);
    REQUIRE(seventhslide::model::plain_text(built.outputs[0].slide_overlays[0].boxes[0].lines[0])
            == "A Mighty Fortress");
    // Non-title slides stay without the title overlay.
    for (std::size_t i = 1; i < built.outputs[0].slide_overlays.size(); ++i) {
        REQUIRE(built.outputs[0].slide_overlays[i].empty());
    }
}

TEST_CASE("title slide shows upcoming lyrics when the theme overlay is off", "[session][title]")
{
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    seventhslide::test::set_look(oc, {{"box_x", 100}, {"box_y", 200}, {"width_px", 1600}, {"height_px", 500}, {"font_size", 48}, {"follow_lines", 4}, {"title_slide_show_lines", true}, {"fluid_slides", true}, {"other_lines", {{"color", "#888888"}}}});

    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "A Mighty Fortress";
    in.song.lyrics = "---[Verse:1]---\nLine one\nLine two\n---[Verse:2]---\nLine three\n";
    in.song.verse_order = "";
    in.outputs = {oc};

    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    REQUIRE(built.outputs[0].layout.groups.size() >= 1);
    const auto& title = built.outputs[0].layout.groups[0];
    REQUIRE(title.is_title);
    REQUIRE(title.active_count == 0);
    REQUIRE(title.indices.size() >= 2);
    REQUIRE(built.outputs[0].slide_overlays[0].empty());

    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0], SceneMode::Song, 1920, 1080, 0, {},
    };
    req.measurer = &measurer;
    const auto frame = build_scene_frame(req);
    REQUIRE(frame.text.has_value());
    REQUIRE(frame.text->lines.size() >= 2);
    for (const auto& line : frame.text->lines) {
        REQUIRE(line.role == SceneLineRole::Follow);
    }
}

TEST_CASE("a theme with its title design switched off paints no title overlay", "[session][title]")
{
    ThemeEntry text = titled_text_theme("t1");

    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    oc.category_defaults = json{{"song", {{"theme", "t1"}}}};

    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "A Mighty Fortress";
    in.song.lyrics = "---[Verse:1]---\nLine one\nLine two\n";
    in.song.verse_order = "";  // the song asks for a title slide

    ApproxTextMeasurer measurer;

    text.style["show_title_slides"] = false;
    oc.song_themes = {text};
    in.outputs = {oc};
    const auto off = build_slides(in, measurer);
    REQUIRE(off.outputs.size() == 1);
    // The cue is the song's, so it stays; the design simply does not paint.
    REQUIRE(off.outputs[0].layout.groups[0].is_title);
    for (const auto& overlay : off.outputs[0].slide_overlays) {
        REQUIRE(overlay.empty());
    }

    // The switch is not a delete: the same theme, switched back on, still has
    // its title design to paint.
    text.style["show_title_slides"] = true;
    oc.song_themes = {text};
    in.outputs = {oc};
    const auto on = build_slides(in, measurer);
    REQUIRE(on.outputs[0].layout.groups[0].is_title);
    REQUIRE_FALSE(on.outputs[0].slide_overlays[0].empty());
    REQUIRE(seventhslide::model::plain_text(on.outputs[0].slide_overlays[0].boxes[0].lines[0]) == "A Mighty Fortress");
}

TEST_CASE("switching a title design off is one output's business", "[session][title]")
{
    // Two outputs, two theme libraries, one line cursor. The output that keeps
    // its title design still paints it; the one that switched its own off does
    // not - and the slides stay in step, because the cue is the song's.
    ThemeEntry main_text = titled_text_theme("t1");
    ThemeEntry stage_text = titled_text_theme("t2");
    stage_text.style["show_title_slides"] = false;

    OutputConfig main_oc;
    main_oc.name = "Main";
    main_oc.id = "out-main";
    main_oc.canvas_width = 1920;
    main_oc.canvas_height = 1080;
    main_oc.category_defaults = json{{"song", {{"theme", "t1"}}}};
    main_oc.song_themes = {main_text};

    OutputConfig stage_oc = main_oc;
    stage_oc.name = "Stage";
    stage_oc.id = "out-stage";
    stage_oc.category_defaults = json{{"song", {{"theme", "t2"}}}};
    stage_oc.song_themes = {stage_text};

    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "A Mighty Fortress";
    in.song.lyrics = "---[Verse:1]---\nLine one\nLine two\n";
    in.song.verse_order = "";
    in.outputs = {main_oc, stage_oc};

    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 2);
    for (const auto& rt : built.outputs) {
        REQUIRE_FALSE(rt.layout.groups.empty());
        REQUIRE(rt.layout.groups[0].is_title);
    }
    REQUIRE_FALSE(built.outputs[0].slide_overlays[0].empty());
    REQUIRE(built.outputs[1].slide_overlays[0].empty());
    REQUIRE(built.outputs[0].line_to_slide == built.outputs[1].line_to_slide);
}

TEST_CASE("SlideBuilder compiles a Freeform document", "[session]")
{
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";

    SlideBuildInput in;
    in.kind = BuildKind::Freeform;
    in.selection.kind = ContentKind::Freeform;
    in.freeform.document = hello_document({"out-main"});
    in.outputs = {oc};

    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    REQUIRE(built.kind == BuildKind::Freeform);
    REQUIRE(built.player.total_lines == 1);
    REQUIRE(built.player.all_line_labels == std::vector<std::string>{"1"});
    REQUIRE(built.outputs.size() == 1);
    REQUIRE(built.outputs[0].slide_count() == 1);
    // Freeform art is the compiled object stack; slide_overlays stays for
    // song / bible title overlays only.
    REQUIRE(built.outputs[0].slide_overlays[0].empty());
    REQUIRE(seventhslide::model::plain_text(built.outputs[0].compiled_slides[0].objects[0].text->lines[0]) == "Hello");
}

TEST_CASE("SlideBuilder bible and empty kinds", "[session]")
{
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"show_bible_text", true}, {"bible_text_box_width", 800}, {"bible_text_box_height", 400}, {"bible_main_font_size", 40}, {"bible_main_font_family", "Helvetica"}});

    SlideBuildInput bible_in;
    bible_in.kind = BuildKind::Bible;
    bible_in.selection.kind = ContentKind::Bible;
    bible_in.bible.chapter = 3;
    bible_in.bible.verses = {{16, "For God so loved the world."}};
    bible_in.outputs = {oc};

    ApproxTextMeasurer measurer;
    const auto bible = build_slides(bible_in, measurer);
    REQUIRE(bible.kind == BuildKind::Bible);
    REQUIRE(bible.player.total_lines >= 1);
    REQUIRE_FALSE(bible.outputs[0].slide_texts.empty());
    REQUIRE(bible.player.all_line_labels[0] == "3:16");

    SlideBuildInput empty_in;
    empty_in.kind = BuildKind::Empty;
    empty_in.outputs = {oc};
    const auto empty = build_slides(empty_in, measurer);
    REQUIRE(empty.player.total_lines == 0);
    REQUIRE(empty.outputs[0].slide_count() == 0);
    // An empty build still resolves each output.
    REQUIRE(empty.outputs[0].name == oc.name);
}

TEST_CASE("SlideBuilder theme chips resolve per-slide styles", "[session][theme_chips]")
{
    using seventhslide::model::ThemeEntry;

    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"font_size", 48}});
    oc.song_themes = {
        ThemeEntry{"t-a", "A", seventhslide::test::flat_style({{"font_size", 32}})},
        ThemeEntry{"t-b", "B", seventhslide::test::flat_style({{"font_size", 64}})},
    };
    oc.bg_themes = {
        ThemeEntry{"b-a", "BA", {{"background_color", "#111111"}}},
        ThemeEntry{"b-b", "BB", {{"background_color", "#222222"}}},
    };

    json pack = {
        {"active", "a1"},
        {"arrangements",
         json::array({json{{"id", "a1"},
                           {"name", "Default"},
                           {"order", "TH1 v1 TH2 c1"},
                           {"chips",
                            {{"TH1", {{"out-main", {{"song", {{"theme", "t-a"}, {"bg", "b-a"}}}}}}},
                             {"TH2", {{"out-main", {{"song", {{"theme", "t-b"}, {"bg", "b-b"}}}}}}}}}}})}};

    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.theme_map = pack;
    in.song.lyrics = "---[Verse:1]---\nHello friends tonight\n\n---[Chorus:1]---\nChorus of the song\n";
    in.song.verse_order = "TH1 v1 TH2 c1";
    in.song.title = "Test";
    in.outputs = {oc};

    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    REQUIRE(built.kind == BuildKind::Song);
    REQUIRE_FALSE(built.outputs.empty());
    const auto& rt = built.outputs.front();
    const int slides = rt.slide_count();
    REQUIRE(slides >= 2);
    REQUIRE(rt.segment_themes.size() == 2);
    REQUIRE(rt.theme_for_slide(0).song.font_size == 32);
    REQUIRE(rt.theme_for_slide(slides - 1).song.font_size == 64);
    REQUIRE(rt.theme_for_slide(0).background.color == "#111111");
    REQUIRE(rt.theme_for_slide(slides - 1).background.color == "#222222");
}

TEST_CASE("follow lines display past a theme chip", "[session][theme_chips][follow]")
{
    using seventhslide::model::ThemeEntry;

    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    json look = {{"box_x", 100}, {"box_y", 200}, {"width_px", 1600}, {"height_px", 800},
                 {"font_size", 48}, {"follow_lines", 1}, {"fluid_slides", true},
                 {"other_lines", {{"color", "#888888"}}}, {"highlight_color", "#ffffff"}};
    look["elements"] = seventhslide::devsupport::elements_from_flat_style(look).to_json();
    oc.song_themes = {
        ThemeEntry{"t-a", "A", look},
        ThemeEntry{"t-b", "B", look},
    };

    json pack = {
        {"active", "a1"},
        {"arrangements",
         json::array({json{{"id", "a1"},
                           {"name", "Default"},
                           {"order", "TH1 v1 TH2 v2"},
                           {"chips",
                            {{"TH1", {{"out-main", {{"song", {{"theme", "t-a"}}}}}}},
                             {"TH2", {{"out-main", {{"song", {{"theme", "t-b"}}}}}}}}}}})}};

    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.theme_map = pack;
    in.song.lyrics = "---[Verse:1]---\nFirst line\nSecond line\n---[Verse:2]---\nThird line\nFourth line\n";
    in.song.verse_order = "TH1 v1 TH2 v2";
    in.song.title = "Test";
    in.outputs = {oc};

    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    const auto& rt = built.outputs.front();
    REQUIRE_FALSE(rt.layout.groups.empty());
    const auto& first = rt.layout.groups.front();
    REQUIRE_FALSE(first.is_title);
    REQUIRE(first.active_count == 1);
    REQUIRE(first.indices.size() >= 3);

    bool saw_next_theme_line = false;
    for (int idx : first.indices) {
        if (idx < 0 || idx >= static_cast<int>(rt.layout.lines.size())) {
            continue;
        }
        if (rt.layout.lines[static_cast<std::size_t>(idx)].text.find("Third")
            != std::string::npos) {
            saw_next_theme_line = true;
        }
    }
    REQUIRE(saw_next_theme_line);

    seventhslide::scene::SceneFrameRequest req{
        rt, SceneMode::Song, 1920, 1080, 0, {},
    };
    req.measurer = &measurer;
    const auto frame = build_scene_frame(req);
    REQUIRE(frame.text.has_value());
    REQUIRE(frame.text->lines.size() >= 3);
    REQUIRE(frame.text->lines.front().role == SceneLineRole::Active);
    bool saw_follow_from_next_theme = false;
    for (std::size_t i = 1; i < frame.text->lines.size(); ++i) {
        REQUIRE(frame.text->lines[i].role == SceneLineRole::Follow);
        if (seventhslide::model::plain_text(frame.text->lines[i].runs).find("Third")
            != std::string::npos) {
            saw_follow_from_next_theme = true;
        }
    }
    REQUIRE(saw_follow_from_next_theme);
}

TEST_CASE("title upcoming lines display past a theme chip", "[session][theme_chips][follow]")
{
    using seventhslide::model::ThemeEntry;

    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    json look = {{"box_x", 100}, {"box_y", 200}, {"width_px", 1600}, {"height_px", 800},
                 {"font_size", 48}, {"follow_lines", 2}, {"title_slide_show_lines", true},
                 {"other_lines", {{"color", "#888888"}}}};
    auto design = seventhslide::devsupport::elements_from_flat_style(look);
    design.titles.clear();
    look["elements"] = design.to_json();
    oc.song_themes = {
        ThemeEntry{"t-a", "A", look},
        ThemeEntry{"t-b", "B", look},
    };

    json pack = {
        {"active", "a1"},
        {"arrangements",
         json::array({json{{"id", "a1"},
                           {"name", "Default"},
                           {"order", "TH1 t1 TH2 v1"},
                           {"chips",
                            {{"TH1", {{"out-main", {{"song", {{"theme", "t-a"}}}}}}},
                             {"TH2", {{"out-main", {{"song", {{"theme", "t-b"}}}}}}}}}}})}};

    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.theme_map = pack;
    in.song.lyrics = "---[Verse:1]---\nHello after the title\nAnd another line\n";
    in.song.verse_order = "TH1 t1 TH2 v1";
    in.song.title = "Test";
    in.outputs = {oc};

    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    const auto& rt = built.outputs.front();
    REQUIRE_FALSE(rt.layout.groups.empty());
    const auto& title = rt.layout.groups.front();
    REQUIRE(title.is_title);
    REQUIRE(title.active_count == 0);
    REQUIRE(title.indices.size() >= 2);

    seventhslide::scene::SceneFrameRequest req{
        rt, SceneMode::Song, 1920, 1080, 0, {},
    };
    req.measurer = &measurer;
    const auto frame = build_scene_frame(req);
    REQUIRE(frame.text.has_value());
    REQUIRE(frame.text->lines.size() >= 2);
    for (const auto& line : frame.text->lines) {
        REQUIRE(line.role == SceneLineRole::Follow);
    }
}

TEST_CASE("paging does not mix active lines across a theme chip", "[session][theme_chips]")
{
    using seventhslide::model::ThemeEntry;

    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    seventhslide::test::set_look(oc, {{"width_px", 1600}, {"height_px", 800}, {"font_size", 48}, {"follow_lines", 0}, {"fluid_slides", true}});
    oc.song_themes = {
        ThemeEntry{"t-a", "A", {{"font_size", 48}}},
        ThemeEntry{"t-b", "B", {{"font_size", 48}}},
    };

    json pack = {
        {"active", "a1"},
        {"arrangements",
         json::array({json{{"id", "a1"},
                           {"name", "Default"},
                           {"order", "TH1 v1 TH2 v2"},
                           {"chips",
                            {{"TH1", {{"out-main", {{"song", {{"theme", "t-a"}}}}}}},
                             {"TH2", {{"out-main", {{"song", {{"theme", "t-b"}}}}}}}}}}})}};

    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.theme_map = pack;
    in.song.lyrics = "---[Verse:1]---\nFirst line\n---[Verse:2]---\nSecond line\n";
    in.song.verse_order = "TH1 v1 TH2 v2";
    in.outputs = {oc};

    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    const auto& rt = built.outputs.front();
    REQUIRE(rt.layout.groups.size() >= 2);
    REQUIRE(rt.layout.groups.front().indices.size() == 1);
    REQUIRE(rt.layout.lines[static_cast<std::size_t>(rt.layout.groups.front().indices[0])].text
            == "First line");
}

namespace {

/// Whether the slide ``rt`` shows at cursor ``row`` has a line containing ``text``.
[[nodiscard]] bool slide_at_row_shows(const seventhslide::build::OutputRuntime& rt, int row,
                                      const std::string& text)
{
    const int slide = rt.line_to_slide.at(static_cast<std::size_t>(row));
    for (const int idx : rt.layout.groups.at(static_cast<std::size_t>(slide)).indices) {
        if (rt.layout.lines.at(static_cast<std::size_t>(idx)).text.find(text)
            != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] OutputConfig song_box(std::string id, int width_px, int lines_high)
{
    OutputConfig oc;
    oc.id = id;
    oc.name = std::move(id);
    seventhslide::test::set_look(oc, {{"width_px", width_px}, {"height_px", lines_high * 58}, {"font_size", 48}});
    return oc;
}

}  // namespace

// One cursor drives every output, so every output must walk the same
// rows. Grouping them differently (4 lines a slide here, 2 there) is the
// intended feature; splitting a line for one box used to give that output
// extra rows, and the others then showed different lyrics at the same row.
TEST_CASE("Song outputs walk the same rows when one box splits a long line", "[slides][song]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "T";
    in.song.verse_order = "v1";
    in.song.lyrics = "---[Verse:1]---\nLine one\nThis is a very long line of lyrics that will "
                     "certainly not fit in the small lower third box at all, it keeps going "
                     "and going and going.\nLine three\nLine four\nLine five";
    in.outputs = {song_box("big", 1800, 4), song_box("small", 900, 2)};
    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 2);
    const auto& big = built.outputs[0];
    const auto& small = built.outputs[1];

    // The small box needs the long line in parts, so there are more rows than
    // lyric lines; the big box shows it whole and holds it across those rows.
    const int rows = built.player.total_lines;
    REQUIRE(rows > 5);
    REQUIRE(static_cast<int>(big.line_to_slide.size()) == rows);
    REQUIRE(static_cast<int>(small.line_to_slide.size()) == rows);
    REQUIRE(big.layout.lines.size() == 5);

    for (int r = 0; r < rows; ++r) {
        const auto& row = built.player.all_lines[static_cast<std::size_t>(r)];
        INFO("row " << r << ": " << row);
        CHECK(slide_at_row_shows(big, r, row));
        CHECK(slide_at_row_shows(small, r, row));
    }
    CHECK(built.player.all_lines.back() == "Line five");
}

TEST_CASE("A chord-only line keeps its row on outputs that hide chords", "[slides][song]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "T";
    in.song.verse_order = "v1";
    in.song.lyrics = "---[Verse:1]---\nFirst line\n[G] [D] [Em]\nSecond line";
    auto stage = song_box("stage", 1800, 1);
    seventhslide::test::set_look(stage, {{"show_chords", true}});
    auto main = song_box("main", 1800, 1);  // one line a slide, chords hidden
    in.outputs = {stage, main};
    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 2);

    // The stage shows the instrumental line as its own row; the main screen
    // stays on "First line" for it rather than skipping ahead.
    REQUIRE(built.player.total_lines == 3);
    const auto& main_rt = built.outputs[1];
    REQUIRE(main_rt.line_to_slide.size() == 3);
    REQUIRE(main_rt.layout.lines.size() == 2);
    CHECK(main_rt.line_to_slide[1] == main_rt.line_to_slide[0]);
    CHECK(slide_at_row_shows(main_rt, 2, "Second line"));
    CHECK(built.outputs[0].line_to_slide[1] != built.outputs[0].line_to_slide[0]);
}
