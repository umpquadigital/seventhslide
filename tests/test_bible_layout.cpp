#include <catch2/catch_test_macros.hpp>

#include "scene_test_helpers.hpp"

#include "theme_test_helpers.hpp"

#include <seventhslide/build/bible_layout.hpp>
#include <seventhslide/layout/text_measurer.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;
using seventhslide::build::BibleChunkPlan;
using seventhslide::build::BibleCitationStyle;
using seventhslide::build::BibleOutputSlides;
using seventhslide::build::BibleVerseChunks;
using seventhslide::build::BibleVerseInput;
using seventhslide::layout::ITextMeasurer;
using seventhslide::layout::FontStyle;
using seventhslide::build::bible_text_measurer;
using seventhslide::build::build_bible_slides_for_output;
using seventhslide::build::chunk_bible_verse;
using seventhslide::build::find_bible_citation_markers;
using seventhslide::build::plan_bible_chunks;
using seventhslide::theme::ResolvedTheme;

namespace {

using seventhslide::test::LenTextMeasurer;

std::filesystem::path fixtures_dir()
{
    return seventhslide::test::fixtures_dir("layout");
}

using seventhslide::test::load_json;

ResolvedTheme theme_from_geom(const json& g, bool show_nums = false)
{
    return seventhslide::test::resolved_from_flat({
        {"bible_main_font_family", "X"},
        {"bible_main_font_size", g.at("size").get<int>()},
        {"bible_text_box_width", g.at("w").get<int>()},
        {"bible_text_box_height", g.at("h").get<int>()},
        {"show_bible_verse_numbers", show_nums},
    });
}

/// A plan from hand-picked chunks (first chunk of a verse carries ``"ch:v "``).
BibleChunkPlan plan_of(int chapter,
                       const std::vector<std::pair<int, std::vector<std::string>>>& verses)
{
    BibleChunkPlan plan;
    plan.chapter = chapter;
    for (const auto& [num, chunks] : verses) {
        const std::string label = std::to_string(chapter) + ':' + std::to_string(num);
        for (const auto& chunk : chunks) {
            plan.plain_lines.push_back(chunk);
            plan.line_labels.push_back(label);
        }
        plan.verses.push_back(BibleVerseChunks{.verse_num = num, .chunks = chunks});
    }
    return plan;
}

std::vector<std::string> slide_texts(const BibleOutputSlides& built)
{
    std::vector<std::string> out;
    for (const auto& s : built.slides) {
        out.push_back(s.text);
    }
    return out;
}

}  // namespace

TEST_CASE("chunk_bible_verse matches the golden fixture", "[layout][bible]")
{
    const auto doc = load_json(fixtures_dir() / "bible_chunk.expected.json");
    LenTextMeasurer measurer;

    for (const auto& c : doc.at("chunk_verse")) {
        INFO(c.at("id").get<std::string>());
        std::vector<ResolvedTheme> ocs;
        std::vector<const ResolvedTheme*> ptrs;
        for (const auto& g : c.at("outputs")) {
            ocs.push_back(theme_from_geom(g));
        }
        for (const auto& oc : ocs) {
            ptrs.push_back(&oc);
        }
        const auto chunks = chunk_bible_verse(
            c.at("chapter").get<int>(),
            c.at("v").get<int>(),
            c.at("text").get<std::string>(),
            ptrs,
            measurer);
        REQUIRE(chunks == c.at("chunks").get<std::vector<std::string>>());
    }
}

TEST_CASE("build_bible_slides_for_output matches the golden fixture", "[layout][bible]")
{
    const auto doc = load_json(fixtures_dir() / "bible_chunk.expected.json");
    LenTextMeasurer measurer;

    for (const auto& c : doc.at("build_slides")) {
        INFO(c.at("id").get<std::string>());
        ResolvedTheme oc = theme_from_geom(c.at("oc"), c.at("show_verse_numbers").get<bool>());
        BibleChunkPlan plan;
        plan.chapter = c.at("chapter").get<int>();
        for (const auto& vc : c.at("verse_chunks")) {
            BibleVerseChunks v;
            v.verse_num = vc.at("verse_num").get<int>();
            v.chunks = vc.at("chunks").get<std::vector<std::string>>();
            for (const auto& chunk : v.chunks) {
                plan.plain_lines.push_back(chunk);
                plan.line_labels.push_back(
                    std::to_string(plan.chapter) + ':' + std::to_string(v.verse_num));
            }
            plan.verses.push_back(std::move(v));
        }

        const auto built = build_bible_slides_for_output(oc, plan, measurer);
        std::vector<std::string> slides;
        for (const auto& s : built.slides) {
            slides.push_back(s.text);
        }
        REQUIRE(slides == c.at("slides").get<std::vector<std::string>>());
        REQUIRE(built.line_to_slide == c.at("line_to_slide").get<std::vector<int>>());
    }
}

TEST_CASE("plan_bible_chunks matches the golden fixture", "[layout][bible]")
{
    const auto doc = load_json(fixtures_dir() / "bible_chunk.expected.json");
    LenTextMeasurer measurer;

    for (const auto& c : doc.at("plan")) {
        INFO(c.at("id").get<std::string>());
        std::vector<ResolvedTheme> ocs;
        for (const auto& g : c.at("outputs")) {
            ocs.push_back(theme_from_geom(g));
        }
        std::vector<const ResolvedTheme*> ptrs;
        for (const auto& oc : ocs) {
            ptrs.push_back(&oc);
        }
        std::vector<BibleVerseInput> verses;
        for (const auto& v : c.at("verses")) {
            verses.push_back(BibleVerseInput{
                .verse_num = v.at("verse_num").get<int>(),
                .text = v.at("text").get<std::string>(),
            });
        }

        const auto plan = plan_bible_chunks(
            c.at("chapter").get<int>(), verses, ptrs, measurer);
        REQUIRE(plan.plain_lines == c.at("plain_lines").get<std::vector<std::string>>());
        REQUIRE(plan.line_labels == c.at("line_labels").get<std::vector<std::string>>());
        REQUIRE(plan.verses.size() == c.at("verse_chunks").size());
        for (std::size_t i = 0; i < plan.verses.size(); ++i) {
            REQUIRE(plan.verses[i].chunks
                    == c.at("verse_chunks")[i].at("chunks").get<std::vector<std::string>>());
        }

        ResolvedTheme oc = ocs.front();
        oc.bible.show_verse_numbers = false;
        const auto built = build_bible_slides_for_output(oc, plan, measurer);
        std::vector<std::string> slides;
        for (const auto& s : built.slides) {
            slides.push_back(s.text);
        }
        REQUIRE(slides == c.at("slides").get<std::vector<std::string>>());
        REQUIRE(built.line_to_slide == c.at("line_to_slide").get<std::vector<int>>());
    }
}

TEST_CASE("A theme's passage box shows Bible text only while it is on", "[layout][bible]")
{
    const auto shown = seventhslide::test::resolved_from_flat({{"show_bible_text", true}});
    const auto hidden = seventhslide::test::resolved_from_flat({{"show_bible_text", false}});
    REQUIRE(shown.bible.shown);
    REQUIRE_FALSE(hidden.bible.shown);
}

TEST_CASE("build_bible_slides empty plan yields blank slide", "[layout][bible]")
{
    LenTextMeasurer measurer;
    ResolvedTheme oc = theme_from_geom(json{{"w", 40}, {"h", 40}, {"size", 10}, {"pad", 0}});
    BibleChunkPlan plan;
    plan.chapter = 1;
    const auto built = build_bible_slides_for_output(oc, plan, measurer);
    REQUIRE(built.slides.size() == 1);
    REQUIRE(built.slides[0].text.empty());
    REQUIRE(built.line_to_slide.empty());
}

TEST_CASE("run-on bible verses join a slide only when the whole verse fits", "[layout][bible][fluid]")
{
    LenTextMeasurer measurer;
    // A 10px font at 1.2 line height makes 12px rows, so the box holds one 20-wide row.
    ResolvedTheme oc = theme_from_geom(json{{"w", 20}, {"h", 12}, {"size", 10}});
    const auto plan = plan_of(1, {
                                     {1, {"1:1 aa bb"}},
                                     {2, {"1:2 cc dd"}},
                                     {3, {"1:3 ee ff", "gg hh ii jj"}},
                                     {4, {"1:4 k"}},
                                 });

    SECTION("off: every verse starts a slide")
    {
        const auto built = build_bible_slides_for_output(oc, plan, measurer);
        REQUIRE(slide_texts(built)
                == std::vector<std::string>{"aa bb", "cc dd", "ee ff gg hh ii jj", "k"});
        REQUIRE(built.line_to_slide == std::vector<int>{0, 1, 2, 2, 3});
    }

    SECTION("on: verses run on; a verse is never split to fill leftover room")
    {
        oc.behavior.bible_fluid_verses = true;
        const auto built = build_bible_slides_for_output(oc, plan, measurer);
        // "aa bb cc dd ee ff" would fit, but verse 3 as a whole does not, so it
        // opens the next slide - and verse 4 then joins it.
        REQUIRE(slide_texts(built)
                == std::vector<std::string>{"aa bb cc dd", "ee ff gg hh ii jj k"});
        REQUIRE(built.line_to_slide == std::vector<int>{0, 0, 1, 1, 1});
        for (const auto& slide : built.slides) {
            // Hidden citations: every verse's marker is stripped, none listed.
            REQUIRE(slide.text.find(':') == std::string::npos);
            REQUIRE(slide.citations.empty());
        }
    }
}

TEST_CASE("run-on bible verses fill the tail of a split verse", "[layout][bible][fluid]")
{
    LenTextMeasurer measurer;
    ResolvedTheme oc = theme_from_geom(json{{"w", 20}, {"h", 12}, {"size", 10}});
    oc.behavior.bible_fluid_verses = true;
    const auto plan = plan_of(1, {
                                     {1, {"1:1 aaaa bbbb cccc dd", "eeee"}},
                                     {2, {"1:2 ff gg"}},
                                     {3, {"1:3 hhhh iiii jjjj"}},
                                 });
    const auto built = build_bible_slides_for_output(oc, plan, measurer);
    REQUIRE(slide_texts(built)
            == std::vector<std::string>{"aaaa bbbb cccc dd", "eeee ff gg", "hhhh iiii jjjj"});
    REQUIRE(built.line_to_slide == std::vector<int>{0, 1, 1, 2});
}

TEST_CASE("run-on bible slides list the citations they carry", "[layout][bible][fluid]")
{
    LenTextMeasurer measurer;
    ResolvedTheme oc = theme_from_geom(json{{"w", 40}, {"h", 12}, {"size", 10}}, true);
    // Citation at body size with no margin costs exactly its marker.
    oc.bible.verse_font_size = 10;
    oc.bible.verse_margin_right_pct = 0;
    oc.behavior.bible_fluid_verses = true;
    const auto plan = plan_of(1, {{1, {"1:1 aa"}}, {2, {"1:2 bb"}}, {3, {"1:3 cc"}}});

    const auto built = build_bible_slides_for_output(oc, plan, measurer);
    REQUIRE(built.slides.size() == 1);
    REQUIRE(built.slides[0].text == "1:1 aa 1:2 bb 1:3 cc");
    REQUIRE(built.slides[0].citations == std::vector<std::string>{"1:1", "1:2", "1:3"});
    REQUIRE(built.line_to_slide == std::vector<int>{0, 0, 0});
}

TEST_CASE("bible citation markers are whole listed words", "[layout][bible][citation]")
{
    const std::string text = "1:1 In 1:10 ab1:1 3:16 x 1:1";
    const auto markers = find_bible_citation_markers(text, {"1:1"});
    REQUIRE(markers.size() == 2);
    REQUIRE(markers[0].offset == 0);
    REQUIRE(markers[0].length == 3);
    REQUIRE(markers[0].trailing_space);
    REQUIRE(markers[1].offset == text.size() - 3);
    REQUIRE_FALSE(markers[1].trailing_space);
    REQUIRE(find_bible_citation_markers(text, {}).empty());
}

TEST_CASE("bible text measurer budgets every citation in a row", "[layout][bible][citation]")
{
    LenTextMeasurer measurer;
    FontStyle box;
    box.size_px = 10;
    BibleCitationStyle cite;
    cite.shown = true;
    cite.font_size_px = 10;
    cite.margin_right_pct = 100;

    // Painted citation: 3 ("1:2") + 10 margin = 13. The marker it replaces is
    // "1:2 " (4) mid-row, or "1:2" (3) at a row's end.
    const auto measure = bible_text_measurer(box, cite, {"1:1", "1:2"}, measurer);
    REQUIRE(measure("x y") == 3.0);
    REQUIRE(measure("x 1:2 y") == 7.0 + 9.0);
    REQUIRE(measure("x 1:2") == 5.0 + 10.0);
    REQUIRE(measure("1:1 a 1:2 b") == 11.0 + 9.0 + 9.0);
    REQUIRE(measure("x 1:3 y") == 7.0);

    // No more than it paints: a number smaller than its marker leaves room.
    cite.font_size_px = 5;
    cite.margin_right_pct = 0;
    const auto small = bible_text_measurer(box, cite, {"1:2"}, measurer);
    REQUIRE(small("x 1:2 y") < measure("x 1:2 y"));
    REQUIRE(small("x 1:2 y") < 7.0);

    cite.shown = false;
    const auto hidden = bible_text_measurer(box, cite, {"1:2"}, measurer);
    REQUIRE(hidden("x 1:2 y") == 7.0);
}
