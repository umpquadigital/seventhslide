#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "freeform_test_helpers.hpp"
#include <seventhslide/build/bible_layout.hpp>
#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/freeform/compile.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/editor.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/model/transition.hpp>
#include <seventhslide/scene/chrome_text.hpp>
#include <seventhslide/scene/freeform_convert.hpp>

#include "theme_test_helpers.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using seventhslide::freeform::compile;
using seventhslide::scene::FreeformConvertOptions;
using seventhslide::freeform::ObjectType;
using seventhslide::freeform::deck_list_rows;
using seventhslide::freeform::deck_spans;
using seventhslide::scene::document_from_build;
using seventhslide::freeform::ListRowKind;
using seventhslide::freeform::make_document;
using seventhslide::test::validate;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::model::OutputConfig;
using seventhslide::model::Song;
using seventhslide::model::TransitionType;
using seventhslide::scene::copyright_token_values;
using seventhslide::build::BuildKind;
using seventhslide::build::SlideBuildInput;
using seventhslide::build::build_slides;
using seventhslide::model::ContentKind;
namespace th = seventhslide::test;

namespace {

OutputConfig make_oc(std::string id, std::string name, int w, int h)
{
    OutputConfig oc;
    oc.id = std::move(id);
    oc.name = std::move(name);
    oc.canvas_width = w;
    oc.canvas_height = h;
    return oc;
}

SlideBuildInput song_input()
{
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "A Mighty Fortress";
    in.song.lyrics = "[V1]\nA mighty fortress is our God\nA bulwark never failing\n\n[V2]\nDid we in our own strength confide\nOur striving would be losing";
    in.song.verse_order = "v1 v2";
    in.outputs.push_back(make_oc("out-main", "Main", 1920, 1080));
    in.outputs.push_back(make_oc("out-lt", "Lower Third", 1920, 240));
    return in;
}

} // namespace

TEST_CASE("document_from_build copies cues and line maps", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    auto in = song_input();
    const auto built = build_slides(in, measurer);
    REQUIRE(built.kind == BuildKind::Song);
    REQUIRE(built.player.total_lines > 0);
    REQUIRE(built.outputs.size() == 2);

    FreeformConvertOptions opts;
    opts.name = "A Mighty Fortress (edit)";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE(doc.line_control);
    REQUIRE(doc.name == "A Mighty Fortress (edit)");
    REQUIRE(static_cast<int>(doc.cues.size()) == built.player.total_lines);
    REQUIRE(doc.cues.size() == built.player.all_lines.size());
    for (std::size_t i = 0; i < doc.cues.size(); ++i) {
        REQUIRE(doc.cues[i].text == built.player.all_lines[i]);
    }
    REQUIRE(doc.decks.size() == 2);
    REQUIRE(doc.decks[0].line_to_slide == built.outputs[0].line_to_slide);
    REQUIRE(doc.decks[1].line_to_slide == built.outputs[1].line_to_slide);
    REQUIRE(doc.decks[0].slide_count() == built.outputs[0].slide_count());
    REQUIRE(doc.decks[1].slide_count() == built.outputs[1].slide_count());
    bool any_text = false;
    for (const auto& slide : doc.decks[0].slides) {
        any_text = any_text || !slide.objects.empty();
    }
    REQUIRE(any_text);
    REQUIRE(validate(doc).empty());
}

// Each slide owns a copy of the background it was built with: nothing links
// it to the theme.
TEST_CASE("document_from_build gives each slide its own copy of the background theme",
          "[freeform][convert][bg]")
{
    using seventhslide::freeform::SlideBackgroundKind;
    using seventhslide::model::ThemeEntry;
    using seventhslide::model::Transition;
    ApproxTextMeasurer measurer;
    auto in = song_input();
    in.outputs[0].bg_themes = {ThemeEntry{"dusk", "Dusk",
                                          nlohmann::json{{"background_type", "color"},
                                                         {"background_color", "#654321"},
                                                         {"bg_enable_fade", true},
                                                         {"bg_fade_duration", 700}}}};
    in.outputs[0].category_defaults = {{"song", {{"bg", "dusk"}}}};
    in.outputs[1].bg_themes = {
        ThemeEntry{"dawn", "Dawn",
                   nlohmann::json{{"background_type", "image"},
                                  {"background_image", "/static/theme_backgrounds/dawn-bg.png"}}}};
    in.outputs[1].category_defaults = {{"song", {{"bg", "dawn"}}}};
    const auto doc =
        document_from_build(build_slides(in, measurer), in.outputs, measurer, {});
    REQUIRE(doc.decks.size() == 2);
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    for (const auto& slide : doc.decks[0].slides) {
        CHECK(slide.background.kind == SlideBackgroundKind::Color);
        CHECK(slide.background.color == "#654321");
        CHECK(slide.background_transition == Transition{TransitionType::Fade, 700});
    }
    // A picture the theme owns is named as it is: the app copies it into the
    // library before saving.
    REQUIRE_FALSE(doc.decks[1].slides.empty());
    for (const auto& slide : doc.decks[1].slides) {
        CHECK(slide.background.kind == SlideBackgroundKind::Image);
        CHECK(slide.background.image == "/static/theme_backgrounds/dawn-bg.png");
        REQUIRE(slide.background_transition.has_value());
        CHECK(slide.background_transition->type == TransitionType::Cut);
    }

    // With no background theme a slide has no background of its own, nor a
    // transition: it keeps the document's for a background given it later.
    const auto plain = song_input();
    const auto bare = document_from_build(build_slides(plain, measurer), plain.outputs, measurer, {});
    for (const auto& slide : bare.decks[0].slides) {
        CHECK(slide.background.is_none());
        CHECK_FALSE(slide.background_transition.has_value());
    }
}

TEST_CASE("document_from_build gives a passage's slides the Bible background",
          "[freeform][convert][bg][bible]")
{
    using seventhslide::freeform::SlideBackgroundKind;
    using seventhslide::model::ThemeEntry;
    using seventhslide::model::Transition;
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 3;
    in.bible.verses = {{16, "For God so loved the world."}, {17, "For God sent not his Son."}};
    auto oc = make_oc("out-main", "Main", 1920, 1080);
    oc.bg_themes = {
        ThemeEntry{"songs", "Songs",
                   nlohmann::json{{"background_type", "color"}, {"background_color", "#101010"}}},
        ThemeEntry{"scripture", "Scripture",
                   nlohmann::json{{"background_type", "video"},
                                  {"background_video", "/static/theme_backgrounds/water.mp4"},
                                  {"bg_enable_fade", true},
                                  {"bg_fade_duration", 900}}}};
    oc.category_defaults = {{"song", {{"bg", "songs"}}}, {"bible", {{"bg", "scripture"}}}};
    in.outputs = {oc};

    const auto doc = document_from_build(build_slides(in, measurer), in.outputs, measurer, {});
    REQUIRE(doc.decks.size() == 1);
    REQUIRE_FALSE(doc.decks.front().slides.empty());
    for (const auto& slide : doc.decks.front().slides) {
        CHECK(slide.background.kind == SlideBackgroundKind::Video);
        CHECK(slide.background.video == "/static/theme_backgrounds/water.mp4");
        CHECK(slide.background_transition == Transition{TransitionType::Fade, 900});
    }
}

// A song's theme chips change its background part way through: each slide
// copies the background it was built with, not the output's default for all.
TEST_CASE("document_from_build gives each slide the background its theme chip picks",
          "[freeform][convert][bg][theme_chips]")
{
    using seventhslide::freeform::SlideBackgroundKind;
    using seventhslide::model::ThemeEntry;
    using seventhslide::model::Transition;
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "Chips";
    in.song.lyrics = "[V1]\nFirst verse line\n\n[V2]\nSecond verse line\n\n[V3]\nThird verse line";
    // The run before the first chip inherits the output's default.
    in.song.verse_order = "v1 TH1 v2 TH2 v3";
    in.song.theme_map = {
        {"active", "a1"},
        {"arrangements",
         nlohmann::json::array(
             {{{"id", "a1"},
               {"name", "Default"},
               {"order", "v1 TH1 v2 TH2 v3"},
               {"chips",
                {{"TH1", {{"out-main", {{"song", {{"bg", "dusk"}}}}}}},
                 {"TH2", {{"out-main", {{"song", {{"bg", "dawn"}}}}}}}}}}})}};
    auto oc = make_oc("out-main", "Main", 1920, 1080);
    oc.bg_themes = {
        ThemeEntry{"base", "Base",
                   nlohmann::json{{"background_type", "color"}, {"background_color", "#101010"}}},
        ThemeEntry{"dusk", "Dusk",
                   nlohmann::json{{"background_type", "color"},
                                  {"background_color", "#654321"},
                                  {"bg_enable_fade", true},
                                  {"bg_fade_duration", 700}}},
        ThemeEntry{"dawn", "Dawn",
                   nlohmann::json{{"background_type", "image"},
                                  {"background_image", "/static/theme_backgrounds/dawn-bg.png"}}}};
    oc.category_defaults = {{"song", {{"bg", "base"}}}};
    in.outputs = {oc};

    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    REQUIRE(built.outputs.front().segment_themes.size() == 3);
    const auto doc = document_from_build(built, in.outputs, measurer, {});
    REQUIRE(doc.decks.size() == 1);
    const auto& slides = doc.decks.front().slides;
    REQUIRE(slides.size() >= 3);

    const auto& first = slides.front();
    CHECK(first.background.kind == SlideBackgroundKind::Color);
    CHECK(first.background.color == "#101010");
    REQUIRE(first.background_transition.has_value());
    CHECK(first.background_transition->type == TransitionType::Cut);

    const auto dusk = std::ranges::find_if(slides, [](const auto& slide) {
        return slide.background.kind == SlideBackgroundKind::Color
               && slide.background.color == "#654321";
    });
    REQUIRE(dusk != slides.end());
    CHECK(dusk->background_transition == Transition{TransitionType::Fade, 700});

    // A picture the chip's theme owns is named as it is, for the app to copy in.
    const auto& last = slides.back();
    CHECK(last.background.kind == SlideBackgroundKind::Image);
    CHECK(last.background.image == "/static/theme_backgrounds/dawn-bg.png");

    // Every slide shows what the build laid it out with.
    const auto& rt = built.outputs.front();
    for (int s = 0; s < static_cast<int>(slides.size()); ++s) {
        INFO("slide " << s);
        const auto& built_bg = rt.theme_for_slide(s).background;
        const auto& slide = slides[static_cast<std::size_t>(s)];
        switch (built_bg.type) {
        case seventhslide::layout::BackgroundType::Color:
            CHECK(slide.background.kind == SlideBackgroundKind::Color);
            CHECK(slide.background.color == built_bg.color);
            break;
        case seventhslide::layout::BackgroundType::Image:
            CHECK(slide.background.kind == SlideBackgroundKind::Image);
            CHECK(slide.background.image == built_bg.image);
            break;
        case seventhslide::layout::BackgroundType::Video:
            CHECK(slide.background.kind == SlideBackgroundKind::Video);
            CHECK(slide.background.video == built_bg.video);
            break;
        case seventhslide::layout::BackgroundType::Transparent:
            CHECK(slide.background.is_none());
            break;
        }
    }
}

TEST_CASE("document_from_build stamps content fade onto lyric objects", "[freeform][convert][transition]")
{
    ApproxTextMeasurer measurer;
    auto in = song_input();
    th::set_look(in.outputs[0],
                 {{"transitions", th::every_transition({TransitionType::Fade, 275})}});
    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Faded";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    bool any_lyric_fade = false;
    for (const auto& obj : doc.decks[0].slides[0].objects) {
        if (obj.transition.plays()) {
            REQUIRE(obj.transition.duration_ms == 275);
            any_lyric_fade = true;
        }
    }
    REQUIRE(any_lyric_fade);
}

TEST_CASE("document_from_build stamps copyright element dissolve", "[freeform][convert][transition]")
{
    ApproxTextMeasurer measurer;
    auto in = song_input();
    auto design = th::design_of(in.outputs[0]);
    th::element_in(design.song, "te_copyright").object.transition =
        seventhslide::model::Transition{seventhslide::model::TransitionType::Fade, 200};
    th::use_design(in.outputs[0], design);
    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Copyright fade";
    {
        Song meta;
        meta.copyright = "Wayne Hooper  © 1962";
        meta.show_copyright = true;
        opts.copyright_vars = copyright_token_values(meta, "");
        opts.song_copyright = meta.show_copyright;
    }
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    bool saw_copyright = false;
    bool saw_lyric = false;
    for (const auto& obj : doc.decks[0].slides[0].objects) {
        if (obj.text.find("Wayne Hooper") != std::string::npos) {
            saw_copyright = true;
            REQUIRE(obj.transition.plays());
            REQUIRE(obj.transition.duration_ms == 200);
        } else if (obj.text.find("fortress") != std::string::npos
                   || obj.text.find("Fortress") != std::string::npos) {
            saw_lyric = true;
            REQUIRE_FALSE(obj.transition.plays());
        }
    }
    REQUIRE(saw_copyright);
    REQUIRE(saw_lyric);
}

TEST_CASE("document_from_build keeps default chrome Cut while lyrics dissolve", "[freeform][convert][transition]")
{
    ApproxTextMeasurer measurer;
    auto in = song_input();
    in.song.lyrics = "---[Verse:1]---\nA mighty fortress is our God\nA bulwark never failing\n";
    in.song.verse_order = "v1";
    th::set_look(in.outputs[0],
                 {{"transitions", th::every_transition({TransitionType::Fade, 275})}});
    auto design = th::design_of(in.outputs[0]);
    th::add_element(design.song, th::indicator_element());
    th::use_design(in.outputs[0], design);
    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Chrome cut";
    {
        Song meta;
        meta.copyright = "Wayne Hooper  © 1962";
        meta.show_copyright = true;
        opts.copyright_vars = copyright_token_values(meta, "");
        opts.song_copyright = meta.show_copyright;
    }
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    bool saw_lyric = false;
    bool saw_copyright = false;
    bool saw_indicator = false;
    for (const auto& obj : doc.decks[0].slides[0].objects) {
        const bool dissolves = obj.transition.plays();
        if (obj.text.find("Wayne Hooper") != std::string::npos) {
            saw_copyright = true;
            REQUIRE_FALSE(dissolves);
        } else if (obj.text.find("fortress") != std::string::npos
                   || obj.text.find("Fortress") != std::string::npos) {
            saw_lyric = true;
            REQUIRE(dissolves);
            REQUIRE(obj.transition.duration_ms == 275);
        } else if (obj.type == ObjectType::Indicator) {
            saw_indicator = true;
            REQUIRE_FALSE(dissolves);
        }
    }
    REQUIRE(saw_lyric);
    REQUIRE(saw_copyright);
    REQUIRE(saw_indicator);
}

TEST_CASE("document_from_build stamps bible_ref dissolve separately from body", "[freeform][convert][transition]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 3;
    in.bible.verses = {{16, "For God so loved the world."}};
    OutputConfig oc = make_oc("out-main", "Main", 1920, 1080);
    th::set_look(oc, {{"transitions", th::every_transition({TransitionType::Fade, 310})},
                      {"show_bible_text", true},
                      {"bible_text_box_width", 800},
                      {"bible_text_box_height", 400}});
    auto design = th::design_of(oc);
    th::element_in(design.bible, "te_bible_ref").object.transition =
        seventhslide::model::Transition{seventhslide::model::TransitionType::Fade, 180};
    th::use_design(oc, design);
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "John 3:16";
    {
        seventhslide::model::BiblePassage passage;
        passage.ref = "John 3:16";
        passage.version = "KJV";
        opts.bible_ref_vars = seventhslide::scene::bible_ref_token_values(passage);
    }
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    bool saw_body = false;
    bool saw_ref = false;
    for (const auto& obj : doc.decks[0].slides[0].objects) {
        if (obj.text.find("John 3:16") != std::string::npos) {
            saw_ref = true;
            REQUIRE(obj.transition.plays());
            REQUIRE(obj.transition.duration_ms == 180);
        } else if (obj.text.find("God so loved") != std::string::npos) {
            saw_body = true;
            REQUIRE(obj.transition.plays());
            REQUIRE(obj.transition.duration_ms == 310);
        }
        REQUIRE(obj.type != ObjectType::Indicator);
    }
    REQUIRE(saw_body);
    REQUIRE(saw_ref);
}

TEST_CASE("document_from_build keeps bible verse superscript in the text box", "[freeform][convert][bible]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 3;
    in.bible.verses = {{16, "For God so loved the world."}};
    OutputConfig oc = make_oc("out-main", "Main", 1920, 1080);
    th::set_look(oc, {{"show_bible_text", true},
                      {"show_bible_verse_numbers", true},
                      {"bible_main_font_size", 80},
                      {"bible_verse_font_size", 40},
                      {"bible_verse_valign", "super"},
                      {"bible_verse_color", "#ffcc00"},
                      {"bible_text_box_width", 1600},
                      {"bible_text_box_height", 600}});
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "John 3:16";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());

    const seventhslide::freeform::Object* body = nullptr;
    for (const auto& obj : doc.decks[0].slides[0].objects) {
        if (obj.text.find("God so loved") != std::string::npos) {
            body = &obj;
            break;
        }
    }
    REQUIRE(body != nullptr);
    REQUIRE(body->text.find("3:16") != std::string::npos);
    REQUIRE(body->text.find("<super>") != std::string::npos);
    REQUIRE(body->cue_linked);
    REQUIRE(body->verse_ref_look.find("<super>") != std::string::npos);

    const auto compiled = compile(doc, &measurer);
    REQUIRE_FALSE(compiled.decks.empty());
    REQUIRE_FALSE(compiled.decks[0].slides.empty());
    bool saw_rise = false;
    for (const auto& obj : compiled.decks[0].slides[0].objects) {
        if (!obj.text) {
            continue;
        }
        for (const auto& line : obj.text->lines) {
            for (const auto& sp : line) {
                if (sp.text.find("3:16") != std::string::npos) {
                    REQUIRE(sp.style.baseline_rise_pct == 40);
                    saw_rise = true;
                }
            }
        }
    }
    REQUIRE(saw_rise);
}

TEST_CASE("document_from_build keeps every run-on verse's superscript", "[freeform][convert][bible][fluid]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 1;
    in.bible.verses = {{1, "In the beginning."}, {2, "God said light."}, {3, "It was good."}};
    OutputConfig oc = make_oc("out-main", "Main", 1920, 1080);
    th::set_look(oc, {{"show_bible_text", true},
                      {"show_bible_verse_numbers", true},
                      {"bible_fluid_verses", true},
                      {"bible_main_font_size", 80},
                      {"bible_verse_font_size", 40},
                      {"bible_verse_valign", "super"},
                      {"bible_text_box_width", 1600},
                      {"bible_text_box_height", 600}});
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs[0].slide_texts.size() == 1);
    const auto doc = document_from_build(built, in.outputs, measurer, {});
    REQUIRE(doc.decks[0].slides.size() == 1);
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>{0, 0, 0});

    const auto compiled = compile(doc, &measurer);
    std::vector<std::string> risen;
    for (const auto& obj : compiled.decks[0].slides[0].objects) {
        if (!obj.text) {
            continue;
        }
        for (const auto& line : obj.text->lines) {
            for (const auto& sp : line) {
                if (sp.style.baseline_rise_pct > 0) {
                    risen.push_back(sp.text);
                }
            }
        }
    }
    REQUIRE(risen == std::vector<std::string>{"1:1", "1:2", "1:3"});
}

TEST_CASE("document_from_build stamps content fade per output", "[freeform][convert][transition]")
{
    ApproxTextMeasurer measurer;
    auto in = song_input();
    th::set_look(in.outputs[0],
                 {{"transitions", th::every_transition({TransitionType::Fade, 275})}});
    th::set_look(in.outputs[1], {{"transitions", th::every_transition({})}});
    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Per output";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE(doc.decks.size() == 2);

    auto lyric_dissolves = [](const seventhslide::freeform::Deck& deck) {
        for (const auto& slide : deck.slides) {
            for (const auto& obj : slide.objects) {
                if (obj.text.find("fortress") != std::string::npos
                    || obj.text.find("Fortress") != std::string::npos) {
                    return obj.transition.plays();
                }
            }
        }
        return false;
    };
    REQUIRE(lyric_dissolves(doc.decks[0]));
    REQUIRE_FALSE(lyric_dissolves(doc.decks[1]));
}

TEST_CASE("document_from_build copies verse labels onto cues", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "Tagged";
    in.song.lyrics = "---[Verse:1]---\nFirst line\nSecond line\n\n---[Chorus:1]---\nRefrain\n";
    in.song.verse_order = "v1 c1";
    in.outputs.push_back(make_oc("out-main", "Main", 1920, 1080));

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Tagged";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE(doc.line_control);
    REQUIRE(doc.cues.size() >= 3);
    bool saw_verse = false;
    bool saw_chorus = false;
    for (const auto& cue : doc.cues) {
        if (cue.verse_type == "Verse" && cue.verse_number == 1)
            saw_verse = true;
        if (cue.verse_type == "Chorus" && cue.verse_number == 1)
            saw_chorus = true;
    }
    REQUIRE(saw_verse);
    REQUIRE(saw_chorus);
}

TEST_CASE("document_from_build keeps lyric inline markup", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "Styled";
    in.song.lyrics = "[V1]\nHello <b>bold</b> world\n";
    in.song.verse_order = "v1";
    in.outputs.push_back(make_oc("out-main", "Main", 1920, 1080));

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Styled";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    REQUIRE_FALSE(doc.decks[0].slides[0].objects.empty());
    REQUIRE(doc.decks[0].slides[0].objects[0].text.find("<b>bold</b>") != std::string::npos);
}

TEST_CASE("document_from_build keeps follow-line size and dim color", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "Follow";
    in.song.lyrics = "---[Verse:1]---\nActive line\nUpcoming line\n";
    in.song.verse_order = "v1";
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    th::set_look(oc, {{"font_size", 48},
                      {"follow_lines", 1},
                      {"highlight_color", "#ffffff"},
                      {"active_line", {{"size", 72}, {"bold", true}}},
                      {"other_lines", {{"size", 36}, {"color", "#888888"}}}});
    auto design = th::design_of(oc);
    th::add_element(design.song, th::indicator_element());
    th::use_design(oc, design);
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Follow";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    REQUIRE_FALSE(doc.decks[0].slides[0].objects.empty());
    const auto& lyric_obj = doc.decks[0].slides[0].objects[0];
    const auto& lyrics = lyric_obj.text;
    REQUIRE(lyrics.find("Active line") != std::string::npos);
    REQUIRE(lyrics.find("Upcoming line") == std::string::npos);
    REQUIRE(lyric_obj.cue_linked);
    REQUIRE(lyric_obj.show_upcoming);
    REQUIRE(lyric_obj.upcoming_color == "#888888");
    // The box's type is the active line's; the following lines' size is theirs.
    REQUIRE(lyric_obj.font_size == 72);
    REQUIRE(lyric_obj.bold);
    REQUIRE(lyric_obj.upcoming_font_size == 36);
    // The other lines are not bold: the box says so for them, as a run cannot.
    REQUIRE(lyric_obj.upcoming_bold == false);
    CHECK_FALSE(lyric_obj.upcoming_italic.has_value());

    const auto built_ff = compile(doc, &measurer);
    REQUIRE_FALSE(built_ff.decks.empty());
    REQUIRE_FALSE(built_ff.decks[0].slides.empty());
    REQUIRE_FALSE(built_ff.decks[0].slides[0].objects.empty());
    const auto& painted = built_ff.decks[0].slides[0].objects[0].text;
    REQUIRE(painted);
    REQUIRE(painted->lines.size() >= 1);
    REQUIRE(seventhslide::model::plain_text(painted->lines[0]).find("Active") != std::string::npos);
    bool saw_upcoming = false;
    for (const auto& line : painted->lines) {
        if (seventhslide::model::plain_text(line).find("Upcoming") != std::string::npos) {
            saw_upcoming = true;
            REQUIRE(line.front().style.color == "#888888");
            CHECK_FALSE(line.front().style.bold);
        }
    }
    REQUIRE(saw_upcoming);
    REQUIRE(built_ff.decks[0].line_to_slide.size() >= 2);
    REQUIRE(built_ff.decks[0].line_to_slide[0] == 0);
    REQUIRE(built_ff.decks[0].line_to_slide[1] != 0);

    bool found_indicator = false;
    for (const auto& obj : doc.decks[0].slides[0].objects) {
        if (obj.type == ObjectType::Indicator) {
            found_indicator = true;
            REQUIRE(obj.text.empty());
        }
    }
    REQUIRE(found_indicator);
}

TEST_CASE("document_from_build owns only active lines in the cue map", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "Four";
    in.song.lyrics = "---[Verse:1]---\nOne\nTwo\nThree\nFour\n";
    in.song.verse_order = "v1";
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    th::set_look(oc, {{"font_size", 48},
                      {"follow_lines", 1},
                      {"highlight_color", "#ffffff"},
                      {"other_lines", {{"color", "#888888"}}},
                      {"active_line", {{"size", 72}}}});
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Four";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE(doc.line_control);
    REQUIRE(doc.cues.size() == 4);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE(doc.decks[0].line_to_slide.size() == 4);
    REQUIRE(doc.decks[0].line_to_slide[0] == 0);
    REQUIRE(doc.decks[0].line_to_slide[1] == 1);
    REQUIRE(doc.decks[0].line_to_slide[2] == 2);
    REQUIRE(doc.decks[0].line_to_slide[3] == 3);

    const auto spans = deck_spans(doc, doc.decks[0]);
    REQUIRE_FALSE(spans.empty());
    REQUIRE(spans[0].first_cue == 0);
    REQUIRE(spans[0].last_cue == 0);

    const auto rows = deck_list_rows(doc, doc.decks[0]);
    int cues_on_slide0 = 0;
    for (const auto& row : rows) {
        if (row.kind == ListRowKind::Cue && row.slide_index == 0) {
            ++cues_on_slide0;
            REQUIRE(row.cue_index == 0);
        }
    }
    REQUIRE(cues_on_slide0 == 1);

    REQUIRE_FALSE(doc.decks[0].slides.empty());
    REQUIRE_FALSE(doc.decks[0].slides[0].objects.empty());
    const auto& lyric_obj = doc.decks[0].slides[0].objects[0];
    REQUIRE(lyric_obj.cue_linked);
    REQUIRE(lyric_obj.show_upcoming);
    REQUIRE(lyric_obj.text.find("One") != std::string::npos);
    REQUIRE(lyric_obj.text.find("Two") == std::string::npos);
    REQUIRE(lyric_obj.text.find("Three") == std::string::npos);
    REQUIRE(lyric_obj.text.find("Four") == std::string::npos);

    const auto built_ff = compile(doc, &measurer);
    REQUIRE_FALSE(built_ff.decks.empty());
    REQUIRE_FALSE(built_ff.decks[0].slides.empty());
    REQUIRE_FALSE(built_ff.decks[0].slides[0].objects.empty());
    const auto& painted = built_ff.decks[0].slides[0].objects[0].text;
    REQUIRE(painted);
    REQUIRE_FALSE(painted->lines.empty());
    REQUIRE(seventhslide::model::plain_text(painted->lines[0]).find("One") != std::string::npos);
    REQUIRE(painted->lines[0].front().style.color.empty());
    int upcoming = 0;
    for (std::size_t i = 1; i < painted->lines.size(); ++i) {
        REQUIRE(painted->lines[i].front().style.color == "#888888");
        ++upcoming;
    }
    REQUIRE(upcoming >= 1);
}

TEST_CASE("document_from_build joins a wrapped active line in a cue-linked box", "[freeform][convert]")
{
    // With line control on, the long lyric line becomes cues of its own, and a
    // cue-linked box stores only the cue being sung. That cue still wraps in
    // the narrow box; the stored text is its words on one line.
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "Wrapped cue";
    in.song.lyrics =
        "---[Verse:1]---\nHello wrapping world today and also tomorrow forevermore\nSecond line\n";
    in.song.verse_order = "v1";
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    th::set_look(oc, {{"width_px", 200},
                      {"font_size", 48},
                      {"follow_lines", 1},
                      {"highlight_color", "#ffffff"},
                      {"other_lines", {{"color", "#888888"}}}});
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Wrapped cue";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE(doc.line_control);
    REQUIRE(doc.cues.size() >= 2);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    REQUIRE_FALSE(doc.decks[0].slides[0].objects.empty());
    const auto& lyrics = doc.decks[0].slides[0].objects[0];
    REQUIRE(lyrics.cue_linked);
    CHECK(lyrics.text == doc.cues[0].text);
    CHECK(lyrics.text.find('\n') == std::string::npos);
    CHECK(lyrics.text.find("  ") == std::string::npos);
    CHECK(lyrics.text.find("Second") == std::string::npos);
}

TEST_CASE("document_from_build stamps Clock objects for a theme clock", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    auto in = song_input();
    auto design = th::design_of(in.outputs[0]);
    auto clock = th::clock_element("te_clock", 20);
    clock.object.clock_24h = true;
    clock.object.font_color = "#ffcc00";
    clock.object.bold = true;
    clock.object.italic = true;
    clock.object.text_align = "right";
    clock.object.vertical_align = "bottom";
    clock.object.flip_h = true;
    th::add_element(design.song, clock);
    th::use_design(in.outputs[0], design);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Clocked";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    int clocks = 0;
    for (const auto& slide : doc.decks[0].slides) {
        bool slide_clock = false;
        for (const auto& obj : slide.objects) {
            if (obj.type == ObjectType::Clock) {
                slide_clock = true;
                REQUIRE(obj.text.empty());
                REQUIRE(obj.clock_24h);
                REQUIRE_FALSE(obj.clock_seconds);
                REQUIRE(obj.x == 20);
                REQUIRE(obj.font_color == "#ffcc00");
                // The converted object keeps the theme clock's whole look, so
                // it paints where and how it did.
                REQUIRE(obj.bold);
                REQUIRE(obj.italic);
                REQUIRE(obj.text_align == "right");
                REQUIRE(obj.vertical_align == "bottom");
                REQUIRE(obj.flip_h);
            }
        }
        REQUIRE(slide_clock);
        ++clocks;
    }
    REQUIRE(clocks == doc.decks[0].slide_count());
    REQUIRE(doc.decks.size() >= 2);
    for (const auto& obj : doc.decks[1].slides[0].objects) {
        REQUIRE(obj.type != ObjectType::Clock);
    }
}

TEST_CASE("document_from_build keeps a theme shape's frame", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    auto in = song_input();
    auto design = th::design_of(in.outputs[0]);
    seventhslide::theme::Element bar;
    bar.object.id = "te_bar";
    bar.object.type = ObjectType::Shape;
    bar.object.x = 40;
    bar.object.y = 900;
    bar.object.w = 600;
    bar.object.h = 80;
    bar.object.rotation = 5;
    bar.object.opacity = 0.5;
    bar.object.flip_h = true;
    bar.object.flip_v = true;
    bar.object.fill.color = "#ff0000";
    th::add_element(design.song, bar);
    th::use_design(in.outputs[0], design);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Barred";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    int shapes = 0;
    for (const auto& obj : doc.decks[0].slides.at(0).objects) {
        if (obj.type != ObjectType::Shape) {
            continue;
        }
        ++shapes;
        CHECK(obj.x == 40);
        CHECK(obj.w == 600);
        CHECK(obj.rotation == 5);
        CHECK(obj.opacity == 0.5);
        CHECK(obj.flip_h);
        CHECK(obj.flip_v);
        CHECK(obj.fill.color == "#ff0000");
    }
    REQUIRE(shapes == 1);
}

namespace {

seventhslide::theme::Element band(std::string id, int z, std::string color)
{
    seventhslide::theme::Element el;
    el.object.id = std::move(id);
    el.object.type = ObjectType::Shape;
    el.object.x = 0;
    el.object.y = 0;
    el.object.w = 1920;
    el.object.h = 1080;
    el.object.z = z;
    el.object.fill.color = std::move(color);
    return el;
}

/// The z of the one object on ``slide`` that ``is`` picks.
template <class Pred>
int z_of(const seventhslide::freeform::Slide& slide, Pred is)
{
    std::optional<int> z;
    for (const auto& obj : slide.objects) {
        if (is(obj)) {
            REQUIRE_FALSE(z.has_value());
            z = obj.z;
        }
    }
    REQUIRE(z.has_value());
    return *z;
}

bool has_fill(const seventhslide::freeform::Object& obj, std::string_view color)
{
    return obj.type == ObjectType::Shape && obj.fill.color == color;
}

} // namespace

TEST_CASE("Saved as Freeform, the lyrics and a title keep their place among a theme's shapes",
          "[freeform][convert][paint_order]")
{
    using seventhslide::theme::ElementBinding;
    ApproxTextMeasurer measurer;
    auto in = song_input();
    in.song.verse_order = "t1 v1 v2";
    auto design = th::design_of(in.outputs[0]);
    for (auto& el : design.song) {
        if (el.binding == ElementBinding::Lyrics) {
            el.object.z = 20;
        }
    }
    th::add_element(design.song, band("te_back", 10, "#ff0000"));
    th::add_element(design.song, band("te_front", 30, "#0000ff"));
    auto title = th::title_text_element();
    title.object.id = "te_title";
    title.object.z = 20;
    th::add_element(design.titles, title);
    th::add_element(design.titles, band("te_title_back", 10, "#00ff00"));
    th::use_design(in.outputs[0], design);

    const auto built = build_slides(in, measurer);
    const auto doc = document_from_build(built, in.outputs, measurer, FreeformConvertOptions{});
    REQUIRE_FALSE(doc.decks.empty());
    const auto& slides = doc.decks[0].slides;
    REQUIRE(slides.size() >= 2);

    // The title slide: the band its theme puts behind the title stays behind it.
    const int title_z = z_of(slides[0], [](const auto& obj) {
        return obj.type == ObjectType::Text
            && obj.text.find("Mighty Fortress") != std::string::npos;
    });
    CHECK(z_of(slides[0], [](const auto& obj) { return has_fill(obj, "#00ff00"); }) < title_z);

    // A verse: one band behind the lyrics and one over them.
    const int lyrics_z = z_of(slides[1], [](const auto& obj) {
        return obj.type == ObjectType::Text
            && obj.text.find("mighty fortress") != std::string::npos;
    });
    CHECK(z_of(slides[1], [](const auto& obj) { return has_fill(obj, "#ff0000"); }) < lyrics_z);
    CHECK(z_of(slides[1], [](const auto& obj) { return has_fill(obj, "#0000ff"); }) > lyrics_z);
}

TEST_CASE("document_from_build keeps a text element's frame, effects and paragraphs",
          "[freeform][convert][parts]")
{
    ApproxTextMeasurer measurer;
    auto in = song_input();
    auto design = th::design_of(in.outputs[0]);
    seventhslide::theme::Element note;
    note.object.id = "te_note";
    note.object.type = ObjectType::Text;
    note.object.text = "Offering";
    note.object.x = 40;
    note.object.y = 900;
    note.object.w = 600;
    note.object.h = 80;
    note.object.fill.color = "#202020";
    note.object.line.color = "#ffffff";
    note.object.line.width = 3;
    note.object.effects.shadow.on = true;
    note.object.effects.text_outline.width = 2;
    note.object.paragraph.bullets = seventhslide::canvas::Bullets::Disc;
    note.object.paragraph.margins.left = 12;
    th::add_element(design.song, note);
    th::use_design(in.outputs[0], design);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Noted";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    int notes = 0;
    for (const auto& obj : doc.decks[0].slides.at(0).objects) {
        if (obj.type != ObjectType::Text || obj.text.find("Offering") == std::string::npos) {
            continue;
        }
        ++notes;
        CHECK(obj.fill.color == "#202020");
        CHECK(obj.line.width == 3);
        CHECK(obj.effects == note.object.effects);
        CHECK(obj.paragraph == note.object.paragraph);
    }
    REQUIRE(notes == 1);
}

TEST_CASE("document_from_build keeps the body box's margins and frame", "[freeform][convert][parts]")
{
    ApproxTextMeasurer measurer;
    auto in = song_input();
    auto design = th::design_of(in.outputs[0]);
    auto& lyrics = th::element_in(design.song, "te_lyrics").object;
    const double x = lyrics.x;
    const double w = lyrics.w;
    lyrics.paragraph.margins = {20, 10, 30, 40};
    lyrics.fill.color = "#40000000";
    th::use_design(in.outputs[0], design);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Framed";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    int boxes = 0;
    for (const auto& obj : doc.decks[0].slides.at(0).objects) {
        if (obj.type != ObjectType::Text || !obj.cue_linked) {
            continue;
        }
        ++boxes;
        // The element's rectangle, not the words': the margins stay margins.
        CHECK(obj.x == x);
        CHECK(obj.w == w);
        CHECK(obj.paragraph.margins.left == 20);
        CHECK(obj.paragraph.margins.bottom == 40);
        CHECK(obj.fill.color == "#40000000");
    }
    REQUIRE(boxes >= 1);
}

TEST_CASE("document_from_build joins soft-wrap fragments into one line", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "Wrap";
    in.song.lyrics =
        "---[Verse:1]---\nHello wrapping world today and also tomorrow forevermore\n";
    in.song.verse_order = "v1";
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    th::set_look(oc, {{"width_px", 200}, {"font_size", 48}});
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Wrap";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    REQUIRE_FALSE(doc.decks[0].slides[0].objects.empty());
    const auto& lyrics = doc.decks[0].slides[0].objects[0];
    REQUIRE(lyrics.line_height == Catch::Approx(1.2));
    REQUIRE(lyrics.text.find("Hello wrapping world today") != std::string::npos);
    REQUIRE(lyrics.text.find("  ") == std::string::npos);
    REQUIRE(lyrics.text.find('\n') == std::string::npos);
}

TEST_CASE("document_from_build copies theme line_height", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "LH";
    in.song.lyrics = "---[Verse:1]---\nHello\n";
    in.song.verse_order = "v1";
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    th::set_look(oc, {{"font_size", 48}, {"line_height", 1.5}});
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "LH";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    REQUIRE_FALSE(doc.decks[0].slides[0].objects.empty());
    REQUIRE(doc.decks[0].slides[0].objects[0].line_height == Catch::Approx(1.5));
}

TEST_CASE("document_from_build copies theme letter_spacing", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "LS";
    in.song.lyrics = "---[Verse:1]---\nHello\n";
    in.song.verse_order = "v1";
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    th::set_look(oc, {{"font_size", 48}, {"letter_spacing", 4.5}});
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "LS";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    REQUIRE_FALSE(doc.decks[0].slides[0].objects.empty());
    REQUIRE(doc.decks[0].slides[0].objects[0].letter_spacing == Catch::Approx(4.5));
}

TEST_CASE("A deck stored as a mirror of another opens as its own", "[freeform][edit]")
{
    auto doc = make_document("x", {"out-main", "out-stage"});
    seventhslide::freeform::Object obj;
    obj.id = "o2@out-stage";
    obj.text = "Hello";
    seventhslide::freeform::find_deck(doc, "out-stage")->slides[0].objects.push_back(obj);
    auto stored = doc.to_json();
    stored["decks"][1]["mirror_of"] = "out-main";

    const auto read = seventhslide::freeform::Document::from_json(stored);
    const auto* stage = seventhslide::freeform::find_deck(read, "out-stage");
    REQUIRE(stage);
    REQUIRE(stage->slides[0].objects.size() == 1);
    CHECK(stage->slides[0].objects[0].text == "Hello");
    CHECK(seventhslide::freeform::find_deck(read, "out-main")->slides[0].objects.empty());
    CHECK_FALSE(read.to_json()["decks"][1].contains("mirror_of"));
}

TEST_CASE("document_from_build emits live Indicator objects not baked V1 text", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    auto in = song_input();
    in.song.lyrics = "---[Verse:1]---\nHello\n\n---[Chorus:1]---\nWorld\n";
    in.song.verse_order = "v1 c1";
    auto design = th::design_of(in.outputs[0]);
    th::add_element(design.song, th::indicator_element());
    th::use_design(in.outputs[0], design);
    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Live indicator";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE_FALSE(doc.decks.empty());
    int indicator_slides = 0;
    for (const auto& slide : doc.decks[0].slides) {
        bool saw = false;
        for (const auto& obj : slide.objects) {
            if (obj.type == ObjectType::Indicator) {
                saw = true;
                REQUIRE(obj.text.empty());
                REQUIRE(obj.indicator_fallback
                        == seventhslide::freeform::IndicatorFallback::Slide);
            }
        }
        if (saw) {
            ++indicator_slides;
        }
    }
    REQUIRE(indicator_slides == static_cast<int>(doc.decks[0].slides.size()));
    REQUIRE(indicator_slides >= 1);
}

TEST_CASE("document_from_build keeps a verse played twice running two sections", "[freeform][convert][sections]")
{
    ApproxTextMeasurer measurer;
    auto in = song_input();
    in.song.lyrics = "---[Verse:1]---\nHello\n\n---[Verse:3]---\nOne\nTwo\n";
    in.song.verse_order = "V1 V3 V3";
    const auto built = build_slides(in, measurer);
    const auto doc = document_from_build(built, in.outputs, measurer, FreeformConvertOptions{});
    const auto sections = seventhslide::freeform::cue_sections(doc.cues);
    CHECK(sections.codes == std::vector<std::string>{"v1", "v3", "v3"});
    REQUIRE(doc.cues.size() == 5);
    CHECK(sections.cue_section == std::vector<int>{0, 1, 1, 2, 2});
    CHECK(doc.cues[3].section_start);
    // A change of label needs no mark.
    CHECK_FALSE(doc.cues[1].section_start);
}

TEST_CASE("document_from_build keeps a theme indicator's label gap", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    auto in = song_input();
    in.song.lyrics = "---[Verse:1]---\nHello\n\n---[Chorus:1]---\nWorld\n";
    in.song.verse_order = "v1 c1";
    auto design = th::design_of(in.outputs[0]);
    auto indicator = th::indicator_element();
    indicator.object.indicator_gap = 0.4;
    th::add_element(design.song, indicator);
    th::use_design(in.outputs[0], design);
    const auto built = build_slides(in, measurer);
    const auto doc =
        document_from_build(built, in.outputs, measurer, FreeformConvertOptions{});
    REQUIRE_FALSE(doc.decks.empty());
    int indicators = 0;
    for (const auto& slide : doc.decks[0].slides) {
        for (const auto& obj : slide.objects) {
            if (obj.type == ObjectType::Indicator) {
                ++indicators;
                CHECK(obj.indicator_gap == 0.4);
            }
        }
    }
    REQUIRE(indicators >= 1);
}

TEST_CASE("document_from_build keeps the title cue on the title slide", "[freeform][convert][title]")
{
    // Follow-lines outputs borrow the first lyric window onto the title slide
    // (``active_count == 0``), so the title line owns no active slot. It must
    // still map to its own slide: a cue map that is not non-decreasing gets
    // clamped forward by ``normalize`` and drags every later cue with it.
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "Fluid Fortress";
    in.song.lyrics =
        "---[Verse:1]---\nLine one\nLine two\nLine three\nLine four\n"
        "---[Verse:2]---\nLine five\nLine six\nLine seven\nLine eight\n"
        "---[Chorus:1]---\nLine nine\nLine ten\nLine eleven\nLine twelve\n";
    in.song.verse_order = "t1 v1 v2 c1";
    OutputConfig oc;
    oc.name = "Foldback";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    th::set_look(oc, {{"width_px", 1800},
                      {"height_px", 600},
                      {"font_size", 80},
                      {"fluid_slides", true},
                      {"follow_lines", 2},
                      {"prevent_mixed_active", true}});
    in.outputs.push_back(oc);

    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    REQUIRE(built.outputs[0].layout.groups.size() > 2);
    REQUIRE(built.outputs[0].layout.groups.front().is_title);
    REQUIRE(built.outputs[0].layout.groups.front().active_count == 0);

    FreeformConvertOptions opts;
    opts.name = "Fluid Fortress";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE(doc.decks.size() == 1);
    const auto& deck = doc.decks[0];
    const auto& map = deck.line_to_slide;
    REQUIRE(map.size() == doc.cues.size());
    REQUIRE(static_cast<int>(deck.slides.size()) == built.outputs[0].slide_count());
    // The title cue selects the title slide, and the map still walks forward.
    REQUIRE(map.front() == 0);
    REQUIRE(std::is_sorted(map.begin(), map.end()));
    REQUIRE(map.back() == deck.slide_count() - 1);
    // Every slide is reachable: cues are spread over the deck, not piled on one.
    const auto spans = deck_spans(doc, deck);
    REQUIRE(static_cast<int>(spans.size()) == deck.slide_count());
    for (const auto& span : spans) {
        INFO("slide " << span.slide_index);
        REQUIRE(span.last_cue >= span.first_cue);
    }
}

TEST_CASE("document_from_build maps trailing master cues to the last slide", "[freeform][convert]")
{
    // A narrow output splits long lines, so the master output can own more cues
    // than this deck has lines. Those extras belong on the final slide.
    ApproxTextMeasurer measurer;
    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    in.song.title = "Two Outputs";
    in.song.lyrics =
        "---[Verse:1]---\nA line that is quite a lot longer than the narrow box\nShort\n";
    in.song.verse_order = "v1";
    OutputConfig wide = make_oc("out-main", "Main", 1920, 1080);
    th::set_look(wide, {{"font_size", 40}, {"width_px", 1800}, {"height_px", 600}});
    OutputConfig narrow = make_oc("out-lt", "Lower Third", 1920, 240);
    th::set_look(narrow, {{"font_size", 40}, {"width_px", 300}, {"height_px", 120}});
    in.outputs.push_back(wide);
    in.outputs.push_back(narrow);

    const auto built = build_slides(in, measurer);
    FreeformConvertOptions opts;
    opts.name = "Two Outputs";
    const auto doc = document_from_build(built, in.outputs, measurer, opts);
    REQUIRE(doc.decks.size() == 2);
    for (const auto& deck : doc.decks) {
        INFO(deck.output_id);
        REQUIRE(deck.line_to_slide.size() == doc.cues.size());
        REQUIRE(std::is_sorted(deck.line_to_slide.begin(), deck.line_to_slide.end()));
        REQUIRE(deck.line_to_slide.back() == deck.slide_count() - 1);
    }
}

TEST_CASE("document_from_build gives a passage's cues their verses", "[freeform][convert][bible]")
{
    ApproxTextMeasurer measurer;
    for (const bool numbers : {true, false}) {
        SlideBuildInput in;
        in.kind = BuildKind::Bible;
        in.selection.kind = ContentKind::Bible;
        in.bible.chapter = 3;
        in.bible.verses = {{16, "For God so loved the world."}, {17, "For God sent not his Son."}};
        OutputConfig oc = make_oc("out-main", "Main", 1920, 1080);
        th::set_look(oc, {{"show_bible_text", true},
                          {"show_bible_verse_numbers", numbers},
                          {"bible_text_box_width", 1600},
                          {"bible_text_box_height", 600}});
        in.outputs.push_back(oc);

        const auto built = build_slides(in, measurer);
        const auto doc = document_from_build(built, in.outputs, measurer, {});
        REQUIRE(doc.cues.size() == 2);
        // The verse is the cue's, not its text's; it shows where the theme showed it.
        CHECK(doc.cues[0].text == "For God so loved the world.");
        CHECK((doc.cues[0].chapter == 3 && doc.cues[0].verse == 16));
        CHECK(doc.cues[0].show_ref == numbers);
        CHECK(doc.cues[1].text == "For God sent not his Son.");
        CHECK((doc.cues[1].chapter == 3 && doc.cues[1].verse == 17));

        const auto compiled = compile(doc, &measurer);
        CHECK(compiled.line_labels == std::vector<std::string>{"3:16", "3:17"});
        std::string painted;
        for (const auto& obj : compiled.decks[0].slides[0].objects) {
            if (obj.text) {
                for (const auto& line : obj.text->lines) {
                    painted += seventhslide::model::plain_text(line) + '\n';
                }
            }
        }
        CHECK((painted.find("3:16 For God so loved") != std::string::npos) == numbers);
        CHECK(painted.find("For God so loved") != std::string::npos);
    }
}

namespace {

[[nodiscard]] OutputConfig passage_output(const char* id, nlohmann::json look)
{
    OutputConfig oc = make_oc(id, id, 1920, 1080);
    look["font_family"] = "Helvetica";
    look["show_bible_text"] = true;
    look["bible_main_font_family"] = "Helvetica";
    look["bible_main_font_size"] = 40;
    look["bible_verse_font_family"] = "Helvetica";
    look["bible_verse_font_size"] = 20;
    if (!look.contains("bible_text_box_width")) {
        look["bible_text_box_width"] = 1600;
    }
    look["bible_text_box_height"] = 800;
    th::set_look(oc, look);
    return oc;
}

[[nodiscard]] SlideBuildInput passage_on(std::vector<OutputConfig> outputs)
{
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 3;
    in.bible.verses = {{16, "For God so loved the world, and gave his only begotten Son, that "
                            "whosoever believeth should not perish but have everlasting life."},
                       {17, "For God sent not his Son."}};
    in.outputs = std::move(outputs);
    return in;
}

[[nodiscard]] std::vector<std::string> painted_lines(const seventhslide::freeform::Document& doc,
                                                     std::size_t deck_index,
                                                     ApproxTextMeasurer& measurer)
{
    std::vector<std::string> out;
    const auto& deck = doc.decks[deck_index];
    for (std::size_t s = 0; s < deck.slides.size(); ++s) {
        for (const auto& obj : deck.slides[s].objects) {
            if (obj.cue_linked) {
                const auto box = seventhslide::freeform::compile_text_object(
                    doc, deck, static_cast<int>(s), obj, std::map<std::string, std::string>{},
                    measurer);
                for (const auto& line : box.lines) {
                    out.push_back(seventhslide::model::plain_text(line));
                }
            }
        }
    }
    return out;
}

}  // namespace

[[nodiscard]] static std::size_t model_plain_size(const seventhslide::layout::OverlayBox& box)
{
    return seventhslide::model::plain_text(box.lines[0]).size();
}

TEST_CASE("Saved as Freeform, verse numbers stay on the screens that showed them",
          "[freeform][convert][bible]")
{
    ApproxTextMeasurer measurer;
    const auto in = passage_on({passage_output("numbered", {{"show_bible_verse_numbers", true}}),
                                passage_output("plain", {{"show_bible_verse_numbers", false}})});
    const auto built = build_slides(in, measurer);
    auto doc = document_from_build(built, in.outputs, measurer, {});
    CHECK_FALSE(doc.decks[0].hide_verse_refs);
    CHECK(doc.decks[1].hide_verse_refs);
    const std::string first = in.bible.verses[0].text;
    const std::string second = in.bible.verses[1].text;
    CHECK(painted_lines(doc, 0, measurer) == std::vector<std::string>{"3:16 " + first, "3:17 " + second});
    CHECK(painted_lines(doc, 1, measurer) == std::vector<std::string>{first, second});

    // It lasts.
    const auto again = seventhslide::freeform::Document::from_json(doc.to_json());
    CHECK(again.decks[1].hide_verse_refs);
    CHECK_FALSE(again.decks[0].hide_verse_refs);
}

TEST_CASE("Asking for a verse number on a screen without them turns them on there",
          "[freeform][edit][bible]")
{
    ApproxTextMeasurer measurer;
    const auto in = passage_on({passage_output("numbered", {{"show_bible_verse_numbers", true}}),
                                passage_output("plain", {{"show_bible_verse_numbers", false}})});
    const auto built = build_slides(in, measurer);
    seventhslide::freeform::Editor editor;
    editor.load(document_from_build(built, in.outputs, measurer, {}), "plain");
    editor.select_cue(0);
    REQUIRE(editor.set_cue_ref_shown(true));
    CHECK_FALSE(editor.document().decks[1].hide_verse_refs);
}

TEST_CASE("Saved as Freeform, run-on verses stay one paragraph with their numbers' look",
          "[freeform][convert][bible]")
{
    ApproxTextMeasurer measurer;
    for (const char* valign : {"super", "baseline", "middle"}) {
        INFO(valign);
        const auto in = passage_on({passage_output("main", {{"show_bible_verse_numbers", true},
                                                            {"bible_fluid_verses", true},
                                                            {"bible_verse_color", "#ffcc00"},
                                                            {"bible_verse_valign", valign},
                                                            {"bible_verse_margin_right_pct", 60}})});
        const auto built = build_slides(in, measurer);
        const auto doc = document_from_build(built, in.outputs, measurer, {});
        REQUIRE(built.outputs[0].slide_texts.size() == 1);
        const auto lines = painted_lines(doc, 0, measurer);
        REQUIRE(lines.size() == 1);
        CHECK(lines[0]
              == "3:16 " + in.bible.verses[0].text + " 3:17 " + in.bible.verses[1].text);

        const auto& obj = *std::ranges::find_if(doc.decks[0].slides[0].objects,
                                                [](const auto& o) { return o.cue_linked; });
        const auto box = seventhslide::freeform::compile_text_object(
            doc, doc.decks[0], 0, obj, std::map<std::string, std::string>{}, measurer);
        int numbers = 0;
        for (const auto& run : box.lines[0]) {
            if (run.text.find(':') != std::string::npos) {
                ++numbers;
                CHECK(run.style.color == "#ffcc00");
                CHECK(run.style.size_pct == 50);
                CHECK(run.style.margin_right_pct == 60);
                CHECK(run.style.margin_for_space);
                CHECK(run.style.baseline_rise_pct
                      == (std::string_view(valign) == "super"     ? seventhslide::model::k_super_rise_pct
                          : std::string_view(valign) == "middle" ? 25
                                                                 : 0));
            }
        }
        CHECK(numbers == 2);
    }
}

TEST_CASE("Saved as Freeform, a bold passage with verse numbers stays bold",
          "[freeform][convert][bible]")
{
    ApproxTextMeasurer measurer;
    const auto in = passage_on({passage_output("main", {{"show_bible_verse_numbers", true},
                                                        {"bible_main_font_bold", true}})});
    const auto built = build_slides(in, measurer);
    const auto doc = document_from_build(built, in.outputs, measurer, {});
    const auto& obj = *std::ranges::find_if(doc.decks[0].slides[0].objects,
                                            [](const auto& o) { return o.cue_linked; });
    const auto box = seventhslide::freeform::compile_text_object(
        doc, doc.decks[0], 0, obj, std::map<std::string, std::string>{}, measurer);
    // Rows that wrap, with no style of their own, are as bold as the first.
    REQUIRE_FALSE(box.lines.empty());
    REQUIRE(box.lines[0].size() >= 2);
    for (const auto& run : box.lines[0]) {
        INFO(run.text);
        CHECK(run.style.bold);
    }
    CHECK(model_plain_size(box) > 80);
}

TEST_CASE("A verse number a deck hides, alone in its cue, leaves one space", "[freeform][bible]")
{
    ApproxTextMeasurer measurer;
    auto doc = make_document("Passage", {"out-main"});
    doc.line_control = true;
    using seventhslide::freeform::Cue;
    doc.cues = {Cue{.id = "c1", .text = "Before."},
                Cue{.id = "c2", .text = "", .chapter = 3, .verse = 17, .show_ref = true},
                Cue{.id = "c3", .text = "After."}};
    auto& deck = doc.decks.front();
    deck.line_to_slide = {0, 0, 0};
    deck.hide_verse_refs = true;
    deck.cue_flow = {{.id = "c2", .runs_on = true}, {.id = "c3", .runs_on = true}};
    seventhslide::freeform::Object box;
    box.id = "t1";
    box.type = ObjectType::Text;
    box.w = 1600;
    box.h = 900;
    box.cue_linked = true;
    deck.slides[0].objects.push_back(box);
    seventhslide::freeform::normalize(doc);
    const auto& obj = doc.decks.front().slides[0].objects[0];

    const auto hidden = seventhslide::freeform::compile_text_object(
        doc, doc.decks.front(), 0, obj, std::map<std::string, std::string>{}, measurer);
    REQUIRE(hidden.lines.size() == 1);
    CHECK(seventhslide::model::plain_text(hidden.lines[0]) == "Before. After.");
    CHECK(seventhslide::freeform::linked_snapshot_markup(doc, doc.decks.front(), 0, obj)
          == "Before. After.");

    // Where it shows, the number is one word between them.
    doc.decks.front().hide_verse_refs = false;
    const auto shown = seventhslide::freeform::compile_text_object(
        doc, doc.decks.front(), 0, obj, std::map<std::string, std::string>{}, measurer);
    REQUIRE(shown.lines.size() == 1);
    CHECK(seventhslide::model::plain_text(shown.lines[0]) == "Before. 3:17 After.");
}

TEST_CASE("Saved as Freeform, a verse number alone in its cue keeps its look",
          "[freeform][convert][bible]")
{
    // Too big to share a row with a word, in a box of one row.
    ApproxTextMeasurer measurer;
    OutputConfig oc = make_oc("main", "main", 1920, 1080);
    th::set_look(oc, {{"font_family", "Helvetica"},
                      {"show_bible_text", true},
                      {"show_bible_verse_numbers", true},
                      {"bible_main_font_family", "Helvetica"},
                      {"bible_main_font_size", 40},
                      {"bible_verse_font_size", 80},
                      {"bible_verse_color", "#ffcc00"},
                      {"bible_verse_margin_right_pct", 60},
                      {"bible_text_box_width", 300},
                      {"bible_text_box_height", 100}});
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 3;
    in.bible.verses = {{16, "Understanding comes."}};
    in.outputs = {oc};
    const auto built = build_slides(in, measurer);
    REQUIRE(built.player.all_lines.front() == "3:16");
    const auto doc = document_from_build(built, in.outputs, measurer, {});
    CHECK(doc.cues[0].text.empty());
    CHECK(doc.cues[0].show_ref);
    const auto& obj = *std::ranges::find_if(doc.decks[0].slides[0].objects,
                                            [](const auto& o) { return o.cue_linked; });
    const auto box = seventhslide::freeform::compile_text_object(
        doc, doc.decks[0], 0, obj, std::map<std::string, std::string>{}, measurer);
    REQUIRE(box.lines.size() == 1);
    REQUIRE(box.lines[0].size() == 1);
    CHECK(box.lines[0][0].text == "3:16");
    CHECK(box.lines[0][0].style.color == "#ffcc00");
    CHECK(box.lines[0][0].style.margin_right_pct == 60);
}

namespace {

using seventhslide::freeform::Editor;

[[nodiscard]] seventhslide::layout::OverlayBox box_of(const Editor& editor,
                                                      ApproxTextMeasurer& measurer)
{
    const auto& deck = *editor.current_deck();
    const auto& obj = *std::ranges::find_if(deck.slides[0].objects,
                                            [](const auto& o) { return o.cue_linked; });
    return seventhslide::freeform::compile_text_object(
        editor.document(), deck, 0, obj, std::map<std::string, std::string>{}, measurer);
}

/// Every run of the box's first line, as text / bold.
[[nodiscard]] std::vector<std::pair<std::string, bool>> runs_of(const Editor& editor,
                                                                ApproxTextMeasurer& measurer)
{
    std::vector<std::pair<std::string, bool>> out;
    const auto box = box_of(editor, measurer);
    for (const auto& run : box.lines.at(0)) {
        out.emplace_back(run.text, run.style.bold);
    }
    return out;
}

[[nodiscard]] seventhslide::freeform::Document short_passage(std::vector<OutputConfig> outputs,
                                                             ApproxTextMeasurer& measurer)
{
    auto in = passage_on(std::move(outputs));
    in.bible.verses = {{16, "For God so loved the world."}, {17, "For God sent not his Son."}};
    return document_from_build(build_slides(in, measurer), in.outputs, measurer, {});
}

/// The box selected, with its text as ``from`` replaced by ``to``, as selecting
/// words and styling them does.
void style_words(Editor& editor, ApproxTextMeasurer& measurer, const std::string& from,
                 const std::string& to)
{
    const auto& deck = *editor.current_deck();
    const auto& obj = *std::ranges::find_if(deck.slides[0].objects,
                                            [](const auto& o) { return o.cue_linked; });
    editor.select(obj.id);
    auto edit = seventhslide::freeform::linked_edit(editor.document(), deck, 0, obj, measurer);
    const auto at = edit.markup.find(from);
    REQUIRE(at != std::string::npos);
    edit.markup.replace(at, from.size(), to);
    REQUIRE(editor.set_linked_edit(edit.markup, edit.upcoming, {}));
}

}  // namespace

TEST_CASE("Changing a cue's words keeps the styling of those that did not change",
          "[freeform][edit]")
{
    ApproxTextMeasurer measurer;
    Editor editor;
    editor.load(short_passage({passage_output("main", {{"show_bible_verse_numbers", false},
                                                       {"bible_fluid_verses", false}})},
                              measurer),
                "main");
    style_words(editor, measurer, "For God so loved the world.",
                "For God so <b>loved</b> the world.");
    editor.select_cue(0);
    REQUIRE(editor.rename_cue("For God so loved the whole world."));
    using Runs = std::vector<std::pair<std::string, bool>>;
    CHECK(runs_of(editor, measurer)
          == Runs{{"For God so ", false}, {"loved", true}, {" the whole world.", false}});

    // A word typed takes the style beside it, and one styled keeps going.
    REQUIRE(editor.rename_cue("For God so loved dearly the whole world."));
    CHECK(runs_of(editor, measurer)
          == Runs{{"For God so ", false}, {"loved", true}, {" dearly the whole world.", false}});

    // A line break made in the box stays where its words are.
    style_words(editor, measurer, "<b>loved</b> dearly", "<b>loved</b>\n dearly");
    editor.select_cue(0);
    REQUIRE(editor.rename_cue("So God so loved dearly the whole world."));
    const auto box = box_of(editor, measurer);
    REQUIRE(box.lines.size() == 2);
    CHECK(seventhslide::model::plain_text(box.lines[0]) == "So God so loved");
}

TEST_CASE("A converted bold verse stays bold as its words change", "[freeform][edit][bible]")
{
    ApproxTextMeasurer measurer;
    Editor editor;
    editor.load(short_passage({passage_output("main", {{"show_bible_verse_numbers", true},
                                                       {"bible_main_font_bold", true},
                                                       {"bible_fluid_verses", false}})},
                              measurer),
                "main");
    editor.select_cue(0);
    REQUIRE(editor.rename_cue("For God so loved the whole world."));
    for (const auto& [text, bold] : runs_of(editor, measurer)) {
        INFO(text);
        CHECK(bold);
    }
}

TEST_CASE("Turning verse numbers on and off keeps what is styled", "[freeform][edit][bible]")
{
    ApproxTextMeasurer measurer;
    Editor editor;
    editor.load(short_passage({passage_output("main", {{"show_bible_verse_numbers", true},
                                                       {"bible_main_font_bold", false},
                                                       {"bible_verse_color", "#ffcc00"},
                                                       {"bible_fluid_verses", false}})},
                              measurer),
                "main");
    style_words(editor, measurer, "For God so loved", "For God so <b>loved</b>");
    editor.select_cue(0);
    const auto with_number = box_of(editor, measurer).lines;

    REQUIRE(editor.set_cue_ref_shown(false));
    const auto without = box_of(editor, measurer).lines.at(0);
    CHECK(seventhslide::model::plain_text(without) == "For God so loved the world.");
    CHECK(std::ranges::any_of(without, [](const auto& run) {
        return run.text == "loved" && run.style.bold;
    }));

    REQUIRE(editor.set_cue_ref_shown(true));
    CHECK(box_of(editor, measurer).lines == with_number);
}

TEST_CASE("A number styled by hand comes back as it was", "[freeform][edit][bible]")
{
    ApproxTextMeasurer measurer;
    Editor editor;
    editor.load(short_passage({passage_output("main", {{"show_bible_verse_numbers", true},
                                                       {"bible_verse_color", "#ffcc00"},
                                                       {"bible_fluid_verses", false}})},
                              measurer),
                "main");
    style_words(editor, measurer, "#ffcc00", "#00ff00");
    editor.select_cue(0);
    REQUIRE(editor.set_cue_ref_shown(false));
    REQUIRE(editor.set_cue_ref_shown(true));
    CHECK(box_of(editor, measurer).lines.at(0).front().style.color == "#00ff00");
}

TEST_CASE("Showing numbers on a screen without them keeps its look and takes the theme's",
          "[freeform][edit][bible]")
{
    ApproxTextMeasurer measurer;
    Editor editor;
    editor.load(short_passage({passage_output("numbered", {{"show_bible_verse_numbers", true}}),
                               passage_output("plain", {{"show_bible_verse_numbers", false},
                                                        {"bible_main_font_bold", true},
                                                        {"bible_verse_color", "#ffcc00"}})},
                              measurer),
                "plain");
    // Words unbolded there by hand, in a box that is bold.
    style_words(editor, measurer, "For God so loved", "For God so <u>loved</u>");
    editor.select_cue(0);
    REQUIRE(editor.set_cue_ref_shown(true));
    const auto line = box_of(editor, measurer).lines.at(0);
    REQUIRE(line.size() >= 2);
    CHECK(line.front().text == "3:16");
    CHECK(line.front().style.color == "#ffcc00");
    CHECK(line.front().style.margin_for_space);
    CHECK(line.front().style.bold);
    CHECK(std::ranges::any_of(line, [](const auto& run) {
        return run.text == "loved" && run.style.underline;
    }));
}

TEST_CASE("The box's bold reaches every word of a linked box", "[freeform][edit]")
{
    ApproxTextMeasurer measurer;
    Editor editor;
    editor.load(short_passage({passage_output("main", {{"show_bible_verse_numbers", true},
                                                       {"bible_main_font_bold", true}})},
                              measurer),
                "main");
    // One word picked out, so this box has styling of its own.
    style_words(editor, measurer, "For God so loved", "For God so <u>loved</u>");
    REQUIRE(editor.set_property("bold", false));
    for (const auto& [text, bold] : runs_of(editor, measurer)) {
        INFO(text);
        CHECK_FALSE(bold);
    }
    REQUIRE(editor.set_property("bold", true));
    for (const auto& [text, bold] : runs_of(editor, measurer)) {
        INFO(text);
        CHECK(bold);
    }
}

TEST_CASE("A converted passage with nothing picked out follows its box", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    const auto doc = short_passage({passage_output("main", {{"show_bible_verse_numbers", true},
                                                            {"bible_main_font_bold", true}})},
                                   measurer);
    for (const auto& slide : doc.decks[0].slides) {
        for (const auto& obj : slide.objects) {
            if (obj.cue_linked) {
                CHECK(obj.cue_overlays.empty());
                CHECK_FALSE(obj.verse_ref_look.empty());
            }
        }
    }
    CHECK(seventhslide::freeform::Document::from_json(doc.to_json()) == doc);
}

TEST_CASE("Spaces in a verse's source do not cost it its look", "[freeform][convert][bible]")
{
    ApproxTextMeasurer measurer;
    auto in = passage_on({passage_output("main", {{"show_bible_verse_numbers", true},
                                                  {"bible_verse_color", "#ffcc00"}})});
    in.bible.verses = {{16, "For God so loved the world, and gave his only begotten Son.  That "
                            "whosoever believeth should not perish but have everlasting life, and "
                            "that the world through him might be saved."}};
    const auto built = build_slides(in, measurer);
    CHECK(built.player.all_lines.front().find("  ") == std::string::npos);
    const auto doc = document_from_build(built, in.outputs, measurer, {});
    Editor editor;
    editor.load(doc, "main");
    CHECK(box_of(editor, measurer).lines.at(0).front().style.color == "#ffcc00");
}

TEST_CASE("A theme's gap past what text can hold is held to it everywhere",
          "[freeform][convert][bible]")
{
    ApproxTextMeasurer measurer;
    for (const int asked : {500, 1000}) {
        auto in = passage_on({passage_output("main", {{"show_bible_verse_numbers", true},
                                                      {"bible_verse_color", "#ffcc00"},
                                                      {"bible_verse_margin_right_pct", asked}})});
        const auto built = build_slides(in, measurer);
        CHECK(seventhslide::build::bible_citation_style(built.outputs[0].theme).margin_right_pct
              == seventhslide::model::k_max_gap_pct);
        const auto doc = document_from_build(built, in.outputs, measurer, {});
        Editor editor;
        editor.load(doc, "main");
        const auto number = box_of(editor, measurer).lines.at(0).front();
        CHECK(number.style.margin_right_pct == seventhslide::model::k_max_gap_pct);
        CHECK(number.style.color == "#ffcc00");
    }
}

TEST_CASE("A verse number alone in its cue is listed as itself", "[freeform][convert][bible]")
{
    ApproxTextMeasurer measurer;
    OutputConfig oc = make_oc("main", "main", 1920, 1080);
    th::set_look(oc, {{"font_family", "Helvetica"},
                      {"show_bible_text", true},
                      {"show_bible_verse_numbers", true},
                      {"bible_main_font_family", "Helvetica"},
                      {"bible_main_font_size", 40},
                      {"bible_verse_font_size", 80},
                      {"bible_verse_margin_right_pct", 60},
                      {"bible_text_box_width", 300},
                      {"bible_text_box_height", 100}});
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 3;
    in.bible.verses = {{16, "Understanding comes."}};
    in.outputs = {oc};
    const auto built = build_slides(in, measurer);
    const auto compiled =
        compile(document_from_build(built, in.outputs, measurer, {}), &measurer);
    CHECK(compiled.lines == built.player.all_lines);
}

namespace {

/// One slide of a run-on passage, its box selected, and what editing it shows.
struct RunOnEdit {
    Editor editor;
    seventhslide::freeform::LinkedEdit edit;
};

void open_run_on(RunOnEdit& open, ApproxTextMeasurer& measurer, bool numbers)
{
    open.editor.load(short_passage({passage_output("main", {{"show_bible_verse_numbers", numbers},
                                                            {"bible_verse_color", "#ffcc00"},
                                                            {"bible_fluid_verses", true}})},
                                   measurer),
                     "main");
    const auto& deck = *open.editor.current_deck();
    const auto& obj = *std::ranges::find_if(deck.slides[0].objects,
                                            [](const auto& o) { return o.cue_linked; });
    open.editor.select(obj.id);
    open.edit = seventhslide::freeform::linked_edit(open.editor.document(), deck, 0, obj, measurer);
}

}  // namespace

TEST_CASE("Editing verses that run on shows them as the paragraph they paint as",
          "[freeform][edit][bible]")
{
    ApproxTextMeasurer measurer;
    RunOnEdit open;
    open_run_on(open, measurer, true);
    const std::string whole = "3:16 For God so loved the world. 3:17 For God sent not his Son.";
    CHECK(seventhslide::model::plain_text_lines(seventhslide::model::parse_markup_lines(
              open.edit.markup))
          == whole);
    CHECK(open.edit.spaces_before == std::vector<std::size_t>{0, 1});

    // Changing nothing changes nothing, and the words cannot be changed.
    CHECK(open.editor.linked_edit_allowed(whole, 0));
    CHECK_FALSE(open.editor.linked_edit_allowed(
        "3:16 For God so loved the world.3:17 For God sent not his Son.", 0));
    CHECK_FALSE(open.editor.linked_edit_allowed(
        "3:16 For God so loved the world. 3:17 For God sent not his Sun.", 0));
    REQUIRE(open.editor.set_linked_edit(open.edit.markup, open.edit.upcoming, {}));
    CHECK(open.editor.selected_item()->cue_overlays.empty());
}

TEST_CASE("A line break typed between verses that run on is kept", "[freeform][edit][bible]")
{
    ApproxTextMeasurer measurer;
    for (const char* typed : {"world.\n ", "world. \n"}) {
        INFO(typed);
        RunOnEdit open;
        open_run_on(open, measurer, false);
        const std::string plain = "For God so loved the world. For God sent not his Son.";
        REQUIRE(open.edit.markup == plain);
        std::string markup = plain;
        markup.replace(markup.find("world. "), 7, typed);
        REQUIRE(open.editor.linked_edit_allowed(markup, 0));
        REQUIRE(open.editor.set_linked_edit(markup, open.edit.upcoming, {}));
        const auto box = box_of(open.editor, measurer);
        std::vector<std::string> lines;
        for (const auto& line : box.lines) {
            lines.push_back(seventhslide::model::plain_text(line));
        }
        CHECK(lines
              == std::vector<std::string>{"For God so loved the world.",
                                          "For God sent not his Son."});
    }
}

TEST_CASE("Editing a box's follow cues keeps them on their own lines", "[freeform][edit]")
{
    ApproxTextMeasurer measurer;
    auto doc = make_document("Song", {"out-main"});
    doc.line_control = true;
    using seventhslide::freeform::Cue;
    doc.cues = {Cue{.id = "c1", .text = "One"}, Cue{.id = "c2", .text = "two"},
                Cue{.id = "c3", .text = "Three"}, Cue{.id = "c4", .text = "fo"},
                Cue{.id = "c5", .text = "ur"}};
    auto& deck = doc.decks.front();
    deck.slides.resize(2);
    deck.slides[0].id = "s1";
    deck.slides[1].id = "s2";
    deck.line_to_slide = {0, 0, 1, 1, 1};
    deck.cue_flow = {{.id = "c2", .runs_on = true},
                     {.id = "c3", .runs_on = true},
                     {.id = "c4", .runs_on = true},
                     {.id = "c5", .runs_on = true, .in_word = true}};
    seventhslide::freeform::Object text;
    text.id = "t1";
    text.type = ObjectType::Text;
    text.w = 1600;
    text.h = 900;
    text.cue_linked = true;
    text.show_upcoming = true;
    deck.slides[0].objects.push_back(text);
    seventhslide::freeform::normalize(doc);

    Editor editor;
    editor.load(doc, "out-main");
    editor.select("t1");
    const auto edit = seventhslide::freeform::linked_edit(
        editor.document(), *editor.current_deck(), 0, *editor.selected_item(), measurer);
    REQUIRE(edit.upcoming == 3);
    CHECK(edit.spaces_before == std::vector<std::size_t>{0, 1, 0, 1, 0});
    const std::string shown = "One two\nThree four";
    CHECK(seventhslide::model::plain_text_lines(seventhslide::model::parse_markup_lines(edit.markup))
          == shown);
    CHECK(editor.linked_edit_allowed(shown, edit.upcoming));
    CHECK_FALSE(editor.linked_edit_allowed("One two\nThree fou", edit.upcoming));
    REQUIRE(editor.set_linked_edit(edit.markup, edit.upcoming, {}));
    CHECK(editor.selected_item()->cue_overlays.empty());
}

TEST_CASE("A row of chords alone inside a line does not stop its box being edited",
          "[freeform][edit][chords]")
{
    // The space is owed by the words after the chords, not by the chords.
    ApproxTextMeasurer measurer;
    auto doc = make_document("Song", {"out-main"});
    doc.line_control = true;
    using seventhslide::freeform::Cue;
    doc.cues = {Cue{.id = "c1", .text = "foo"}, Cue{.id = "c2", .text = "[A]"},
                Cue{.id = "c3", .text = "bar"}};
    auto& deck = doc.decks.front();
    deck.line_to_slide = {0, 0, 0};
    deck.cue_flow = {{.id = "c2", .runs_on = true, .in_word = true}, {.id = "c3", .runs_on = true}};
    seventhslide::freeform::Object text;
    text.id = "t1";
    text.type = ObjectType::Text;
    text.w = 1600;
    text.h = 900;
    text.cue_linked = true;
    deck.slides[0].objects.push_back(text);
    seventhslide::freeform::normalize(doc);

    Editor editor;
    editor.load(doc, "out-main");
    editor.select("t1");
    const auto edit = seventhslide::freeform::linked_edit(
        editor.document(), *editor.current_deck(), 0, *editor.selected_item(), measurer);
    CHECK(edit.markup == "foo bar");
    CHECK(editor.linked_edit_allowed("foo bar", 0));
    CHECK_FALSE(editor.linked_edit_allowed("foobar", 0));
    REQUIRE(editor.set_linked_edit(edit.markup, edit.upcoming, {}));
    CHECK(editor.selected_item()->cue_overlays.empty());
}
