// Bible verse text must fit the box its theme defines.
//
// The fit pass (layout::bible_layout) and the paint pass (scene_builder ->
// text_painter) measure independently; when they disagree the painter clips the
// verse mid-glyph, because ``paint_text_content`` clips to the box rect. These
// tests pin the invariant end to end: every visual row the scene builder emits
// fits inside ``bible_text_box_*``.

#include <catch2/catch_test_macros.hpp>

#include "theme_test_helpers.hpp"

#include <seventhslide/build/even_wrap.hpp>
#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/scene/scene_builder.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using nlohmann::json;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::layout::FontStyle;
using seventhslide::layout::ITextMeasurer;
using seventhslide::model::OutputConfig;
using seventhslide::model::ThemeEntry;
using seventhslide::scene::SceneFrameRequest;
using seventhslide::scene::SceneMode;
using seventhslide::scene::SceneTextBoxStyle;
using seventhslide::scene::build_scene_frame;
using seventhslide::build::BuildKind;
using seventhslide::build::SlideBuildInput;
using seventhslide::build::build_slides;
using seventhslide::model::ContentKind;

namespace {

constexpr const char* k_psalm23 =
    "The Lord is my shepherd; I shall not want. He maketh me to lie down in green "
    "pastures: he leadeth me beside the still waters. He restoreth my soul: he "
    "leadeth me in the paths of righteousness for his name's sake.";

/// Bake the load-time fallbacks the way ``OutputConfig::from_dict`` does, so the
/// config starts in the same state a database-loaded output is in.
[[nodiscard]] OutputConfig loaded(OutputConfig oc)
{
    return oc;
}

/// Width of one painted row - mirrors ``render::draw_runs`` (per-span size and
/// right margin), which is what the citation span actually costs.
[[nodiscard]] double row_width_px(const seventhslide::model::RichLine& runs,
                                  const SceneTextBoxStyle& box,
                                  ITextMeasurer& measurer)
{
    double w = 0;
    for (const auto& span : runs) {
        FontStyle st;
        st.family = span.style.font_family.empty() ? box.font_family : span.style.font_family;
        st.size_px = box.font_size * (std::max(10, span.style.size_pct) / 100.0);
        st.bold = box.bold || span.style.bold;
        st.italic = box.italic || span.style.italic;
        st.letter_spacing = box.letter_spacing;
        w += measurer.measure_width(span.text, st);
        w += st.size_px * (std::max(0, span.style.margin_right_pct) / 100.0);
    }
    return w;
}

/// Assert every bible slide of every output paints inside its own box.
void require_every_slide_fits(const seventhslide::build::SlideBuildResult& built,
                              const std::vector<OutputConfig>& outputs,
                              ITextMeasurer& measurer)
{
    for (const auto& rt : built.outputs) {
        const auto oc = std::find_if(outputs.begin(), outputs.end(),
                                      [&](const OutputConfig& o) { return o.name == rt.name; });
        REQUIRE(oc != outputs.end());
        for (int i = 0; i < static_cast<int>(rt.slide_texts.size()); ++i) {
            SceneFrameRequest req{.runtime = rt};
            req.mode = SceneMode::Bible;
            req.canvas_width = oc->canvas_width;
            req.canvas_height = oc->canvas_height;
            req.slide_index = i;
            req.measurer = &measurer;
            const auto frame = build_scene_frame(req);
            if (!frame.text.has_value()) {
                continue;
            }
            const auto& text = *frame.text;
            const double lh = std::ceil(text.box.font_size * text.box.line_height);
            INFO("output=" << rt.name << " slide=" << i << " rows=" << text.lines.size()
                 << " size=" << text.box.font_size << " box=" << text.box.width << "x"
                 << text.box.height);
            CHECK(static_cast<double>(text.lines.size()) * lh <= text.box.height);
            // A row with a citation larger than the text paints taller.
            double painted = 0.0;
            for (const auto& line : text.lines) {
                REQUIRE(line.metrics.has_value());
                painted += line.metrics->height;
            }
            CHECK(painted <= text.box.height);
            for (const auto& line : text.lines) {
                CHECK(row_width_px(line.runs, text.box, measurer) <= text.box.width + 0.5);
            }
        }
    }
}

[[nodiscard]] seventhslide::build::SlideBuildResult build_psalm(
    const std::vector<OutputConfig>& outputs, ITextMeasurer& measurer)
{
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 23;
    in.bible.verses = {{1, k_psalm23}};
    in.outputs = outputs;
    return build_slides(in, measurer);
}

}  // namespace

TEST_CASE("bible verse fits a theme that leaves the verse font on inherit", "[layout][bible][fit]")
{
    // A text theme stores 0 / "" for every field its author never touched. Those
    // sentinels used to survive the cascade, so the fit pass measured the verse at
    // 0 px (one slide, always) while paint drew it at ``font_size``.
    OutputConfig oc;
    oc.name = "Main";
    oc.bible_themes = {ThemeEntry{"t1", "Verse",
                                 seventhslide::test::flat_style(
                                     json{{"font_size", 60},
                                          {"font_family", "Helvetica"},
                                          {"bible_text_box_width", 900},
                                          {"bible_text_box_height", 300},
                                          {"bible_main_font_size", 0},
                                          {"bible_main_font_family", ""}})}};
    oc.category_defaults = json{{"bible", {{"theme", "t1"}}}};

    const std::vector<OutputConfig> outputs{loaded(oc)};
    ApproxTextMeasurer measurer;
    const auto built = build_psalm(outputs, measurer);

    REQUIRE(built.outputs[0].theme.bible.font_size == 60);
    REQUIRE(built.outputs[0].slide_texts.size() > 1);
    require_every_slide_fits(built, outputs, measurer);
}

TEST_CASE("bible chunks fit every output, not just the one with fewest lines", "[layout][bible][fit]")
{
    // Chunks are shared across outputs, so they must satisfy the tightest box in
    // every dimension. Main is short but wide; Foldback is tall but narrow, so it
    // wins on line count while losing badly on width.
    OutputConfig main;
    main.name = "Main";
    seventhslide::test::set_look(main, {{"font_family", "Helvetica"},
                                        {"show_bible_text", true},
                                        {"bible_main_font_size", 40},
                                        {"bible_main_font_family", "Helvetica"},
                                        {"bible_text_box_width", 1600},
                                        {"bible_text_box_height", 200}});

    OutputConfig foldback = main;
    foldback.name = "Foldback";
    seventhslide::test::set_look(foldback, {{"bible_text_box_width", 420},
                                            {"bible_text_box_height", 400}});

    const std::vector<OutputConfig> outputs{loaded(main), loaded(foldback)};
    ApproxTextMeasurer measurer;
    const auto built = build_psalm(outputs, measurer);
    require_every_slide_fits(built, outputs, measurer);
}

TEST_CASE("bible verse fits when the theme's citation font is larger than the body", "[layout][bible][fit]")
{
    // Paint draws the leading ``ch:v`` as its own span plus a right margin. When
    // that is wider than the plain prefix the fit pass measured, the first row
    // needs the extra width budgeted or it runs past the box edge.
    OutputConfig oc;
    oc.name = "Main";
    seventhslide::test::set_look(oc, {{"font_family", "Helvetica"},
                                      {"show_bible_text", true},
                                      {"show_bible_verse_numbers", true},
                                      {"bible_main_font_size", 40},
                                      {"bible_main_font_family", "Helvetica"},
                                      {"bible_verse_font_size", 80},
                                      {"bible_verse_margin_right_pct", 60},
                                      {"bible_text_box_width", 700},
                                      {"bible_text_box_height", 300}});

    const std::vector<OutputConfig> outputs{loaded(oc)};
    ApproxTextMeasurer measurer;
    const auto built = build_psalm(outputs, measurer);
    require_every_slide_fits(built, outputs, measurer);
}

TEST_CASE("bible verse wraps after the citation instead of fusing it to the first word", "[layout][bible][fit]")
{
    // Layout's marker is ``"23:1 "``. Fusing to ``"23:1The"`` made that first
    // token wider than a tight box, so paint clipped it mid-glyph.
    OutputConfig oc;
    oc.name = "Main";
    seventhslide::test::set_look(oc, {{"font_family", "Helvetica"},
                                      {"show_bible_text", true},
                                      {"show_bible_verse_numbers", true},
                                      {"bible_main_font_size", 40},
                                      {"bible_main_font_family", "Helvetica"},
                                      {"bible_verse_font_size", 80},
                                      {"bible_verse_margin_right_pct", 60},
                                      {"bible_text_box_width", 270},
                                      {"bible_text_box_height", 400}});

    const std::vector<OutputConfig> outputs{loaded(oc)};
    ApproxTextMeasurer measurer;
    const auto built = build_psalm(outputs, measurer);
    REQUIRE_FALSE(built.outputs[0].slide_texts.empty());
    REQUIRE(built.outputs[0].slide_texts[0].find("23:1 ") == 0);
    require_every_slide_fits(built, outputs, measurer);
}

TEST_CASE("run-on bible verses fit every output's box", "[layout][bible][fit][fluid]")
{
    // Several verses share a slide, so citations land mid-row; each still has to
    // be budgeted at its painted width on every output.
    OutputConfig main;
    main.name = "Main";
    seventhslide::test::set_look(main, {{"font_family", "Helvetica"},
                                        {"show_bible_text", true},
                                        {"show_bible_verse_numbers", true},
                                        {"bible_fluid_verses", true},
                                        {"bible_main_font_size", 40},
                                        {"bible_main_font_family", "Helvetica"},
                                        {"bible_verse_font_size", 80},
                                        {"bible_verse_margin_right_pct", 60},
                                        {"bible_text_box_width", 900},
                                        {"bible_text_box_height", 300}});

    OutputConfig foldback = main;
    foldback.name = "Foldback";
    seventhslide::test::set_look(foldback, {{"bible_text_box_width", 420},
                                            {"bible_text_box_height", 400}});

    const std::vector<OutputConfig> outputs{loaded(main), loaded(foldback)};
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 23;
    in.bible.verses = {
        {1, "The Lord is my shepherd; I shall not want."},
        {2, "He maketh me to lie down in green pastures: he leadeth me beside the still waters."},
        {3, "He restoreth my soul: he leadeth me in the paths of righteousness for his name's sake."},
    };
    in.outputs = outputs;
    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);

    const auto& rt = built.outputs[0];
    REQUIRE(rt.slide_citations.size() == rt.slide_texts.size());
    REQUIRE(std::any_of(rt.slide_citations.begin(), rt.slide_citations.end(),
                        [](const auto& c) { return c.size() > 1; }));
    for (const auto& out : built.outputs) {
        REQUIRE(static_cast<int>(out.line_to_slide.size()) == built.player.total_lines);
    }
    require_every_slide_fits(built, outputs, measurer);
}

TEST_CASE("A word wider than the passage box is cut to fit it", "[layout][bible][fit]")
{
    OutputConfig main;
    main.name = "Main";
    seventhslide::test::set_look(main, {{"font_family", "Helvetica"},
                                        {"show_bible_text", true},
                                        {"bible_main_font_size", 40},
                                        {"bible_main_font_family", "Helvetica"},
                                        {"bible_text_box_width", 200},
                                        {"bible_text_box_height", 400}});
    const std::vector<OutputConfig> outputs{loaded(main)};
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 8;
    in.bible.verses = {{3, "And they called his name Mahershalalhashbaz in that day."}};
    in.outputs = outputs;
    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    require_every_slide_fits(built, outputs, measurer);
}

TEST_CASE("A slide counts the rows a word too wide for its box is cut into", "[layout][bible][fit]")
{
    // The name alone takes all four rows the box holds, so the words around it
    // cannot share its slide.
    OutputConfig main;
    main.name = "Main";
    seventhslide::test::set_look(main, {{"font_family", "Helvetica"},
                                        {"show_bible_text", true},
                                        {"bible_main_font_size", 40},
                                        {"bible_main_font_family", "Helvetica"},
                                        {"bible_text_box_width", 200},
                                        {"bible_text_box_height", 200}});
    const std::vector<OutputConfig> outputs{loaded(main)};
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 8;
    in.bible.verses = {{3, "They called his name Mahershalalhashbazmahershalalhashbaz in that day."}};
    in.outputs = outputs;
    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    require_every_slide_fits(built, outputs, measurer);
}

TEST_CASE("A verse number cut off alone stays off a screen without verse numbers",
          "[layout][bible][fit]")
{
    // A huge number cannot share a row with a word, and the box holds one row.
    OutputConfig numbered;
    numbered.name = "Numbered";
    seventhslide::test::set_look(numbered, {{"font_family", "Helvetica"},
                                            {"show_bible_text", true},
                                            {"show_bible_verse_numbers", true},
                                            {"bible_main_font_size", 40},
                                            {"bible_main_font_family", "Helvetica"},
                                            {"bible_verse_font_size", 80},
                                            {"bible_verse_margin_right_pct", 60},
                                            {"bible_text_box_width", 300},
                                            {"bible_text_box_height", 100}});
    OutputConfig plain = numbered;
    plain.name = "Plain";
    seventhslide::test::set_look(plain, {{"show_bible_text", true},
                                         {"show_bible_verse_numbers", false}});
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 3;
    in.bible.verses = {{16, "Understanding comes."}, {17, "Understanding stays."}};
    in.outputs = {loaded(numbered), loaded(plain)};
    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    REQUIRE(std::ranges::find(built.player.all_lines, "3:16") != built.player.all_lines.end());

    const auto& rt = built.outputs[1];
    REQUIRE(static_cast<int>(rt.line_to_slide.size()) == built.player.total_lines);
    for (const auto& text : rt.slide_texts) {
        INFO(text);
        CHECK(text.find("3:") == std::string::npos);
        CHECK_FALSE(text.empty());
    }
}

TEST_CASE("A verse number wider than its box stays whole and looks like one",
          "[layout][bible][fit]")
{
    OutputConfig main;
    main.name = "Main";
    seventhslide::test::set_look(main, {{"font_family", "Helvetica"},
                                        {"show_bible_text", true},
                                        {"show_bible_verse_numbers", true},
                                        {"bible_main_font_size", 40},
                                        {"bible_main_font_family", "Helvetica"},
                                        {"bible_verse_font_size", 120},
                                        {"bible_verse_margin_right_pct", 60},
                                        {"bible_text_box_width", 200},
                                        {"bible_text_box_height", 800}});
    SlideBuildInput in;
    in.kind = BuildKind::Bible;
    in.selection.kind = ContentKind::Bible;
    in.bible.chapter = 3;
    in.bible.verses = {{16, "For God so loved."}};
    in.outputs = {loaded(main)};
    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    SceneFrameRequest req{.runtime = built.outputs[0]};
    req.mode = SceneMode::Bible;
    req.canvas_width = 1920;
    req.canvas_height = 1080;
    req.slide_index = 0;
    req.measurer = &measurer;
    const auto frame = build_scene_frame(req);
    REQUIRE(frame.text.has_value());
    REQUIRE_FALSE(frame.text->lines.empty());
    const auto& first = frame.text->lines.front().runs.front();
    CHECK(first.text == "3:16");
    CHECK(first.style.size_pct == 300);
}
