// A passage in a Bible box, audited with real fonts across many boxes and looks,
// verse numbers on and off, run-on verses on and off: a slide fits its box unless
// it is a single row, no row is wider than the box but for one character or
// verse number that is, and no word or verse number is lost or repeated. And a passage saved as
// Freeform paints as it did: the same rows on every slide, in the same place.
//
// The everyday looks run with the rest of the tests. The full audit is hidden
// ([.audit]). Run it after a change to Bible layout, Freeform conversion or text
// paint.

#include <catch2/catch_test_macros.hpp>

#include "font_fixtures.hpp"
#include "test_qt_app.hpp"
#include "theme_test_helpers.hpp"

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/fonts/font_registry.hpp>
#include <seventhslide/freeform/compile.hpp>
#include <seventhslide/layout/overlay_layout.hpp>
#include <seventhslide/layout/qt_text_measurer.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/scene/freeform_convert.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/util/strings.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

using Verses = std::vector<seventhslide::build::BibleVerseInput>;

[[nodiscard]] std::vector<Verses> passages()
{
    return {
        // Short verses, more than a slide of them.
        {{1, "In the beginning God created the heaven and the earth."},
         {2, "And the earth was without form, and void; and darkness was upon the face of the "
             "deep."},
         {3, "And God said, Let there be light: and there was light."},
         {4, "And God saw the light, that it was good."},
         {5, "And God called the light Day, and the darkness he called Night."}},
        // One long verse, a verse of one word, and a verse number past 99.
        {{9, "Then was the king's scribe called at that time in the third month, that is, the "
             "month Sivan, on the three and twentieth day thereof; and it was written according "
             "to all that Mordecai commanded unto the Jews, and to the lieutenants, and the "
             "deputies and rulers of the provinces which are from India unto Ethiopia, an "
             "hundred twenty and seven provinces."},
         {35, "Jesus wept."},
         {176, "I have gone astray like a lost sheep; seek thy servant."}},
        // A word wider than most of the boxes here, and a line of colons.
        {{1, "And they called his name Mahershalalhashbaz in that day: for: thus: said: the "
             "Lord: 3:16 and no more."}},
    };
}

struct Look {
    const char* family;
    int font_size;
    int width;
    int height;
    bool numbers;
    int number_size;  // 0: half the text's
    const char* valign;
    int margin_pct;
    bool fluid;
    double line_height = 1.2;
    bool bold = false;
};

[[nodiscard]] std::vector<Look> looks()
{
    std::vector<Look> out;
    for (const char* family : {"EB Garamond", "Liberation Sans"}) {
        for (const int size : {36, 60, 110}) {
            for (const int width : {260, 700, 1690}) {
                for (const int height : {200, 500, 900}) {
                    for (const bool fluid : {false, true}) {
                        out.push_back({family, size, width, height, false, 0, "super", 0, fluid});
                        out.push_back({family, size, width, height, true, 0, "super", 20, fluid});
                        out.push_back({family, size, width, height, true, size * 2, "middle", 60,
                                       fluid});
                        out.push_back({family, size, width, height, true, size / 2, "baseline", 0,
                                       fluid});
                    }
                }
            }
        }
    }
    // Tight and loose lines, bold.
    for (const double line_height : {0.9, 1.6}) {
        out.push_back({"EB Garamond", 60, 700, 500, true, 0, "super", 20, true, line_height, true});
        out.push_back({"Liberation Sans", 60, 700, 500, false, 0, "super", 0, false, line_height,
                       true});
    }
    return out;
}

[[nodiscard]] std::vector<Look> everyday_looks()
{
    return {
        {"Helvetica", 60, 1600, 800, true, 0, "super", 20, false},
        {"Helvetica", 60, 1600, 800, true, 0, "super", 20, true},
        {"Helvetica", 60, 1600, 800, false, 0, "super", 0, true},
        {"Helvetica", 60, 900, 400, true, 30, "baseline", 0, false},
        {"Helvetica", 40, 900, 300, true, 80, "middle", 60, true},
    };
}

[[nodiscard]] nlohmann::json flat_of(const Look& look)
{
    return {
        {"font_family", look.family},
        {"show_bible_text", true},
        {"show_bible_verse_numbers", look.numbers},
        {"bible_fluid_verses", look.fluid},
        {"bible_main_font_family", look.family},
        {"bible_main_font_size", look.font_size},
        {"bible_main_font_bold", look.bold},
        {"line_height", look.line_height},
        {"bible_verse_font_size", look.number_size > 0 ? look.number_size : look.font_size / 2},
        {"bible_verse_font_family", look.family},
        {"bible_verse_valign", look.valign},
        {"bible_verse_margin_right_pct", look.margin_pct},
        {"bible_text_box_width", look.width},
        {"bible_text_box_height", look.height},
    };
}

[[nodiscard]] seventhslide::model::OutputConfig output_of(const char* id, const Look& look)
{
    seventhslide::model::OutputConfig out;
    out.id = id;
    out.name = id;
    out.canvas_width = 1920;
    out.canvas_height = 1080;
    seventhslide::test::set_look(out, flat_of(look));
    return out;
}

[[nodiscard]] std::string words_only(std::string_view text)
{
    std::string out;
    for (const char c : text) {
        if (!seventhslide::util::is_space(c)) {
            out.push_back(c);
        }
    }
    return out;
}

[[nodiscard]] std::optional<seventhslide::scene::SceneTextContent> scene_text_of(
    const seventhslide::build::OutputRuntime& rt,
    int slide,
    seventhslide::layout::ITextMeasurer& measurer)
{
    seventhslide::scene::SceneFrameRequest req{.runtime = rt};
    req.mode = seventhslide::scene::SceneMode::Bible;
    req.canvas_width = 1920;
    req.canvas_height = 1080;
    req.slide_index = slide;
    req.measurer = &measurer;
    return seventhslide::scene::build_scene_frame(req).text;
}

struct Totals {
    int slides = 0;
    int too_tall = 0;  // one word that needs more than the box holds, tiny boxes only
};

/// Every slide of ``rt`` inside its box, and the passage whole across them.
void check_passage_fits(const seventhslide::build::OutputRuntime& rt,
                        const Verses& verses,
                        int chapter,
                        int cues,
                        bool numbers,
                        seventhslide::layout::ITextMeasurer& measurer,
                        Totals& totals)
{
    REQUIRE(static_cast<int>(rt.line_to_slide.size()) == cues);
    CHECK(std::ranges::is_sorted(rt.line_to_slide));

    std::string painted;
    for (int s = 0; s < static_cast<int>(rt.slide_texts.size()); ++s) {
        const auto text = scene_text_of(rt, s, measurer);
        REQUIRE(text.has_value());
        ++totals.slides;
        double height = 0.0;
        for (const auto& row : text->lines) {
            REQUIRE(row.metrics.has_value());
            height += row.metrics->height;
            std::string shown;
            for (const auto& run : row.runs) {
                shown += run.text;
            }
            painted += words_only(shown);
            INFO("slide " << s << " row: " << shown << " is " << row.metrics->width);
            // One character or one verse number can be wider than a box;
            // nothing else can.
            const bool number_alone = row.runs.size() == 1 && row.runs[0].style.margin_for_space;
            if (words_only(shown).size() > 1 && !number_alone) {
                CHECK(row.metrics->width <= text->box.width + 1.0);
            }
        }
        INFO("slide " << s << ": " << text->lines.size() << " rows " << height << " px");
        // One row, or one word cut into rows, can be taller than a box; nothing more can.
        const auto words =
            seventhslide::util::split_words(rt.slide_texts[static_cast<std::size_t>(s)]);
        if (text->lines.size() > 1 && words.size() > (numbers ? 2U : 1U)) {
            CHECK(height <= text->box.height + 1.0);
        } else if (height > text->box.height) {
            ++totals.too_tall;
        }
    }

    std::string expected;
    for (const auto& verse : verses) {
        if (numbers) {
            expected += std::to_string(chapter) + ":" + std::to_string(verse.verse_num);
        }
        expected += words_only(verse.text);
    }
    CHECK(painted == expected);
}

/// Each slide of a converted output against the slide it was made from.
void check_converted_passage(const seventhslide::build::OutputRuntime& rt,
                             const seventhslide::freeform::Document& doc,
                             std::size_t o,
                             seventhslide::layout::ITextMeasurer& measurer,
                             Totals& totals)
{
    const auto& deck = doc.decks[o];
    REQUIRE(deck.slides.size() == rt.slide_texts.size());
    const std::map<std::string, std::string> values;
    for (int s = 0; s < static_cast<int>(deck.slides.size()); ++s) {
        const auto text = scene_text_of(rt, s, measurer);
        REQUIRE(text.has_value());
        const auto& objects = deck.slides[static_cast<std::size_t>(s)].objects;
        const auto obj = std::ranges::find_if(objects, [](const auto& candidate) {
            return candidate.cue_linked;
        });
        REQUIRE(obj != objects.end());
        const auto box =
            seventhslide::freeform::compile_text_object(doc, deck, s, *obj, values, measurer);
        const auto laid = seventhslide::layout::lay_out_overlay(box, measurer);
        ++totals.slides;

        std::vector<std::string> scene_rows;
        double scene_height = 0.0;
        std::string read_scene;
        for (const auto& row : text->lines) {
            std::string shown;
            for (const auto& run : row.runs) {
                shown += run.text;
            }
            scene_rows.push_back(words_only(shown));
            scene_height += row.metrics->height;
            read_scene += shown + " | ";
        }
        std::vector<std::string> freeform_rows;
        std::string read_freeform;
        for (const auto& row : laid.rows) {
            std::string shown;
            for (const auto& run : row.runs) {
                shown += run.text;
            }
            freeform_rows.push_back(words_only(shown));
            read_freeform += shown + " | ";
        }
        INFO("output " << o << ", slide " << s << "\nbible    " << scene_height
                       << " px: " << read_scene << "\nfreeform " << laid.height
                       << " px: " << read_freeform);
        CHECK(scene_rows == freeform_rows);
        CHECK(std::abs(laid.height - scene_height) <= 1.0);
        if (scene_rows.size() != freeform_rows.size()) {
            continue;
        }
        double y = 0.0;
        for (std::size_t r = 0; r < laid.rows.size(); ++r) {
            const auto& row = text->lines[r];
            INFO("row " << r << ": " << scene_rows[r]);
            CHECK(std::abs(laid.rows[r].width - row.metrics->width) <= 1.0);
            CHECK(std::abs(laid.rows[r].y - y) <= 1.0);
            // Every run in its place: a verse number's rise and margin included.
            CHECK(laid.rows[r].runs.size() == row.runs.size());
            REQUIRE(row.metrics->x.size() == row.runs.size());
            if (laid.rows[r].runs.size() != row.runs.size()) {
                y += row.metrics->height;
                continue;
            }
            for (std::size_t k = 0; k < row.runs.size(); ++k) {
                INFO("run " << k << ": " << row.runs[k].text);
                CHECK(std::abs(laid.rows[r].runs[k].x - row.metrics->x[k]) <= 1.0);
                CHECK(laid.rows[r].runs[k].style.size_pct == row.runs[k].style.size_pct);
                CHECK(laid.rows[r].runs[k].style.baseline_rise_pct
                      == row.runs[k].style.baseline_rise_pct);
                CHECK(laid.rows[r].runs[k].style.color == row.runs[k].style.color);
                CHECK(laid.rows[r].runs[k].style.margin_right_pct
                      == row.runs[k].style.margin_right_pct);
            }
            y += row.metrics->height;
        }
    }
}

void audit_passages(const std::vector<Look>& audited,
                    seventhslide::layout::ITextMeasurer& measurer,
                    bool convert)
{
    Totals fit;
    Totals converted;
    for (const auto& look : audited) {
        for (const auto& verses : passages()) {
            INFO(look.family << " " << look.font_size << "px, box " << look.width << "x"
                             << look.height << ", numbers " << look.numbers << " at "
                             << look.number_size << " " << look.valign << " margin "
                             << look.margin_pct << ", fluid " << look.fluid << ", line height "
                             << look.line_height << ", verse " << verses.front().verse_num);
            seventhslide::build::SlideBuildInput in;
            in.kind = seventhslide::build::BuildKind::Bible;
            in.selection.kind = seventhslide::model::ContentKind::Bible;
            in.bible.chapter = 3;
            in.bible.verses = verses;
            in.outputs.push_back(output_of("main", look));
            // A second screen: roomier, its verse numbers the other way round.
            auto second = look;
            second.width = std::min(1800, look.width * 2);
            second.height = 1000;
            second.numbers = !look.numbers;
            second.number_size = 0;
            second.valign = "super";
            in.outputs.push_back(output_of("second", second));

            const auto built = seventhslide::build::build_slides(in, measurer);
            const bool numbers[] = {look.numbers, second.numbers};
            for (std::size_t o = 0; o < built.outputs.size(); ++o) {
                INFO("output " << o);
                check_passage_fits(built.outputs[o], verses, in.bible.chapter,
                                   built.player.total_lines, numbers[o], measurer, fit);
            }
            if (!convert) {
                continue;
            }
            const auto doc = seventhslide::scene::document_from_build(
                built, in.outputs, measurer, {.name = "Passage"});
            for (std::size_t o = 0; o < built.outputs.size(); ++o) {
                check_converted_passage(built.outputs[o], doc, o, measurer, converted);
            }
        }
    }
    CHECK(fit.slides > 0);
    WARN(fit.slides << " slides; one word taller than its box on " << fit.too_tall);
    if (convert) {
        CHECK(converted.slides > 0);
    }
}

}  // namespace

TEST_CASE("Passages paint inside their box, with every word", "[bible][fit]")
{
    seventhslide::layout::ApproxTextMeasurer measurer;
    audit_passages(everyday_looks(), measurer, false);
}

TEST_CASE("Passages paint inside their box with real fonts in every look",
          "[.audit][bible][fit]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    audit_passages(looks(), measurer, false);
}

TEST_CASE("A passage saved as Freeform paints as it did on its output",
          "[bible][freeform][convert]")
{
    seventhslide::layout::ApproxTextMeasurer measurer;
    audit_passages(everyday_looks(), measurer, true);
}

TEST_CASE("A passage saved as Freeform paints as it did, with real fonts in every look",
          "[.audit][bible][freeform][convert]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    audit_passages(looks(), measurer, true);
}
