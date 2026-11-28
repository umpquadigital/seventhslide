#include <catch2/catch_test_macros.hpp>

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/scene/chrome_text.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/theme/theme_elements.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using seventhslide::model::Song;
using seventhslide::model::BiblePassage;
using seventhslide::scene::bible_ref_token_values;
using seventhslide::scene::build_indicator_items;
using seventhslide::scene::copyright_token_values;
using seventhslide::scene::format_wall_clock_hms;
using seventhslide::scene::SceneExtras;
using seventhslide::build::OutputRuntime;
using seventhslide::theme::ElementContext;

namespace {

/// What the stock theme's ``element_id`` shows in ``ctx`` for these tokens,
/// through the element path playback paints. ``text`` replaces the stock
/// template when given. Empty when the element does not show.
[[nodiscard]] std::string stock_element_text(ElementContext ctx,
                                             std::string_view element_id,
                                             const SceneExtras& extras,
                                             std::string_view text = {})
{
    auto elements = seventhslide::theme::default_elements().at(ctx);
    for (auto& el : elements) {
        if (el.object.id == element_id && !text.empty()) {
            el.object.text = std::string(text);
        }
    }
    seventhslide::theme::ElementConditions conditions;
    conditions.song_copyright = extras.song_copyright;
    const auto objects = seventhslide::scene::build_theme_element_objects(
        elements, 0, 1, seventhslide::scene::frame_token_pool(extras), {}, false, {},
        conditions);
    for (const auto& so : objects) {
        if (so.id != element_id || !so.text) {
            continue;
        }
        std::string out;
        for (const auto& line : so.text->lines) {
            if (!out.empty()) {
                out.push_back('\n');
            }
            out += seventhslide::model::plain_text(line);
        }
        return out;
    }
    return {};
}

[[nodiscard]] SceneExtras song_extras(const Song& song, std::string_view ccli_licence)
{
    SceneExtras extras;
    extras.copyright_vars = copyright_token_values(song, ccli_licence);
    extras.song_copyright = song.show_copyright;
    return extras;
}

[[nodiscard]] std::string copyright_text(const Song& song,
                                         std::string_view ccli_licence,
                                         std::string_view text = {})
{
    return stock_element_text(ElementContext::Song, "te_copyright",
                              song_extras(song, ccli_licence), text);
}

[[nodiscard]] std::string reference_text(const BiblePassage& passage, std::string_view text = {})
{
    SceneExtras extras;
    extras.bible_ref_vars = bible_ref_token_values(passage);
    return stock_element_text(ElementContext::Bible, "te_bible_ref", extras, text);
}

}  // namespace

TEST_CASE("The stock copyright notice shows the song's credits", "[scene][copyright]")
{
    Song song;
    song.show_copyright = true;
    song.authors = {"Alice", "Bob"};
    song.copyright = "© 2020";
    REQUIRE(copyright_text(song, "12345") == "Alice, Bob\n© 2020\nCCLI License #12345");

    // The song's own toggle hides the notice.
    song.show_copyright = false;
    REQUIRE(copyright_text(song, "12345").empty());
}

TEST_CASE("A copyright notice fills its tokens and drops empty lines", "[scene][copyright]")
{
    Song song;
    song.show_copyright = true;
    song.authors = {"Alice"};
    song.copyright = "© 2020";
    song.ccli_song_number = "7201044";
    REQUIRE(copyright_text(song, "12345") == "Alice\n© 2020\nCCLI License #12345");
    REQUIRE(copyright_text(song, "12345", "{-authors} — {-copyright}") == "Alice — © 2020");
    REQUIRE(copyright_text(song, "12345", "CCLI {ccli-number}") == "CCLI 7201044");

    Song empty_authors;
    empty_authors.show_copyright = true;
    empty_authors.copyright = "© 1962";
    REQUIRE(copyright_text(empty_authors, "") == "© 1962");
}

TEST_CASE("The stock Bible reference fills its tokens", "[scene][bible_ref]")
{
    BiblePassage passage;
    passage.book = "John";
    passage.chapter = 3;
    passage.verse_start = 16;
    passage.verse_end = 18;
    passage.version = "KJV";
    const auto vars = bible_ref_token_values(passage);

    REQUIRE(vars.at("book") == "John");
    REQUIRE(vars.at("chapter") == "3");
    REQUIRE(vars.at("verse-start") == "16");
    REQUIRE(vars.at("verse-end") == "18");
    REQUIRE(vars.at("verses") == "16-18");
    REQUIRE(vars.at("reference") == "John 3:16-18");
    REQUIRE(vars.at("ref") == "John 3:16-18");
    REQUIRE(vars.at("version") == "KJV");

    REQUIRE(reference_text(passage) == "John 3:16-18\nKJV");
    REQUIRE(reference_text(passage, "{book} {chapter}:{verses}") == "John 3:16-18");

    BiblePassage no_version;
    no_version.ref = "John 3:16";
    REQUIRE(reference_text(no_version) == "John 3:16");

    BiblePassage built;
    built.book = "Revelation";
    built.chapter = 14;
    built.verse_start = 6;
    built.verse_end = 6;
    built.version = "NKJV";
    REQUIRE(reference_text(built) == "Revelation 14:6\nNKJV");
    REQUIRE(reference_text(built, "{-ref} ({-version})") == "Revelation 14:6 (NKJV)");
}

namespace {

[[nodiscard]] std::vector<std::string> labels_of(
    const std::vector<seventhslide::scene::IndicatorItem>& items)
{
    std::vector<std::string> labels;
    labels.reserve(items.size());
    for (const auto& item : items) {
        labels.push_back(item.label);
    }
    return labels;
}

[[nodiscard]] std::vector<int> active_of(
    const std::vector<seventhslide::scene::IndicatorItem>& items)
{
    std::vector<int> active;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].active) {
            active.push_back(static_cast<int>(i));
        }
    }
    return active;
}

}  // namespace

TEST_CASE("indicator shows a verse played twice running twice", "[scene][indicator]")
{
    // A song ordered V1 V2 V3 V3 V4: one section each, two lines each, a slide
    // a section.
    OutputRuntime rt;
    rt.verse_codes = {"v1", "v2", "v3", "v3", "v4"};
    rt.verse_indices = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4};
    rt.line_to_slide = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4};

    const auto first = build_indicator_items(rt, 2);
    REQUIRE(labels_of(first) == std::vector<std::string>{"V1", "V2", "V3", "V3", "V4"});
    REQUIRE(active_of(first) == std::vector<int>{2});

    const auto again = build_indicator_items(rt, 3);
    REQUIRE(labels_of(again) == labels_of(first));
    REQUIRE(active_of(again) == std::vector<int>{3});
}

TEST_CASE("indicator marks the active section", "[scene][indicator]")
{
    OutputRuntime rt;
    rt.verse_codes = {"v1", "c1", "b1"};
    rt.verse_indices = {0, 0, 1, 2};
    rt.line_to_slide = {0, 0, 1, 2};

    const auto items0 = build_indicator_items(rt, 0);
    REQUIRE(labels_of(items0) == std::vector<std::string>{"V1", "C1", "B1"});
    REQUIRE(active_of(items0) == std::vector<int>{0});
    REQUIRE(active_of(build_indicator_items(rt, 1)) == std::vector<int>{1});
}

TEST_CASE("indicator skips empty verse codes", "[scene]")
{
    OutputRuntime rt;
    rt.verse_codes = {"v1", "", "", "c1"};
    rt.verse_indices = {0, 1, 2, 3};
    rt.line_to_slide = {0, 0, 0, 1};

    const auto items = build_indicator_items(rt, 0);
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].label == "V1");
    REQUIRE(items[0].active);
    REQUIRE(items[1].label == "C1");
    REQUIRE_FALSE(items[1].active);
}

TEST_CASE("resolve_indicator_items follows the current line to its section", "[scene][indicator]")
{
    using seventhslide::scene::resolve_indicator_items;

    // Freeform cues V3 V3 | V3 V3 (two sections), one cue a slide.
    const std::vector<std::string> codes{"v3", "v3"};
    const std::vector<int> sections{0, 0, 1, 1};
    const std::vector<int> map{0, 1, 2, 3};
    const seventhslide::scene::IndicatorSections play{
        .codes = codes, .line_section = sections, .line_to_slide = map};
    const auto second = resolve_indicator_items(play, 2, 2, 4, false);
    REQUIRE(labels_of(second) == std::vector<std::string>{"V3", "V3"});
    REQUIRE(active_of(second) == std::vector<int>{1});
    // No current line: the first line on the slide.
    const auto first = resolve_indicator_items(play, -1, 1, 4, false);
    REQUIRE(active_of(first) == std::vector<int>{0});
}

TEST_CASE("resolve_indicator_items uses slide numbers when unlabeled", "[scene]")
{
    using seventhslide::scene::resolve_indicator_items;
    using seventhslide::scene::slide_number_indicator;

    const auto numbered = slide_number_indicator(4, 10);
    REQUIRE(numbered.size() == 1);
    REQUIRE(numbered[0].label == "5/10");
    REQUIRE(numbered[0].active);

    const std::vector<std::string> codes{"v1", "", "c1"};
    const std::vector<int> sections{0, 1, 2};
    const std::vector<int> map{0, 0, 1};
    const seventhslide::scene::IndicatorSections play{
        .codes = codes, .line_section = sections, .line_to_slide = map};
    const auto verse = resolve_indicator_items(play, 0, 0, 2, false);
    REQUIRE(verse.size() == 2);
    REQUIRE(verse[0].label == "V1");
    REQUIRE(verse[0].active);
    REQUIRE(verse[1].label == "C1");
    REQUIRE_FALSE(verse[1].active);

    const auto unlabeled = resolve_indicator_items(play, 1, 0, 2, false);
    REQUIRE(unlabeled.size() == 1);
    REQUIRE(unlabeled[0].label == "1/2");

    const auto blank = resolve_indicator_items(play, 1, 0, 2, true);
    REQUIRE(blank.empty());

    const auto simple = resolve_indicator_items({}, -1, 2, 8, false);
    REQUIRE(simple.size() == 1);
    REQUIRE(simple[0].label == "3/8");
}

TEST_CASE("format_indicator_markup bolds the active code", "[scene]")
{
    using seventhslide::scene::format_indicator_markup;
    using seventhslide::scene::IndicatorItem;
    const std::vector<IndicatorItem> items{{"V1", true}, {"C1", false}};
    const auto markup = format_indicator_markup(items);
    REQUIRE(markup.find("<b>") != std::string::npos);
    REQUIRE(markup.find("V1") != std::string::npos);
    REQUIRE(markup.find("C1") != std::string::npos);
    REQUIRE(markup.find("#808080") != std::string::npos);
}

TEST_CASE("indicator_line keeps one label-styled space at the default gap", "[scene][indicator]")
{
    using seventhslide::scene::indicator_line;
    using seventhslide::scene::IndicatorItem;
    const std::vector<IndicatorItem> items{{"V1", false}, {"C1", true}, {"B1", false}};
    const auto line = indicator_line(items);
    REQUIRE(line.size() == 3);
    CHECK(line[0].text == "V1 ");
    CHECK(line[0].style.color == "#808080");
    CHECK(line[1].text == "C1 ");
    CHECK(line[1].style.bold);
    CHECK(line[2].text == "B1");
    CHECK(indicator_line(items, 1.0) == line);
}

TEST_CASE("indicator_line sets labels apart by the gap in spaces", "[scene][indicator]")
{
    using seventhslide::model::RichLine;
    using seventhslide::scene::indicator_line;
    using seventhslide::scene::IndicatorItem;
    const std::vector<IndicatorItem> items{{"V1", true}, {"C1", false}};

    // Plain separators between the labels; nothing after the last one.
    const auto texts = [](const RichLine& line) {
        std::vector<std::pair<std::string, int>> out;
        for (const auto& span : line) {
            out.emplace_back(span.text, span.style.size_pct);
        }
        return out;
    };
    using Runs = std::vector<std::pair<std::string, int>>;
    CHECK(texts(indicator_line(items, 0.5)) == Runs{{"V1", 100}, {" ", 50}, {"C1", 100}});
    CHECK(texts(indicator_line(items, 2.0)) == Runs{{"V1", 100}, {"  ", 100}, {"C1", 100}});
    CHECK(texts(indicator_line(items, 2.3))
          == Runs{{"V1", 100}, {"  ", 100}, {" ", 30}, {"C1", 100}});
    // Out of range reads as the nearest end.
    CHECK(texts(indicator_line(items, 0.0)) == Runs{{"V1", 100}, {" ", 10}, {"C1", 100}});
    CHECK(texts(indicator_line(items, 9.0)) == Runs{{"V1", 100}, {"    ", 100}, {"C1", 100}});

    const auto gap = indicator_line(items, 0.5)[1].style;
    CHECK_FALSE(gap.bold);
    CHECK(gap.color.empty());

    REQUIRE(indicator_line({{"5/10", true}}, 0.5).size() == 1);
    CHECK(indicator_line({}, 0.5).empty());
}

TEST_CASE("format_indicator_markup carries the gap through markup", "[scene][indicator]")
{
    using seventhslide::model::parse_inline_markup;
    using seventhslide::scene::format_indicator_markup;
    using seventhslide::scene::indicator_line;
    using seventhslide::scene::IndicatorItem;
    const std::vector<IndicatorItem> items{{"V1", true}, {"C1", false}, {"B1", false}};
    // The Freeform canvas reads the markup back; it has to arrive as built.
    for (const double gap : {0.4, 2.5}) {
        CHECK(parse_inline_markup(format_indicator_markup(items, gap))
              == indicator_line(items, gap));
    }
    // The default gap is the markup indicators have always had.
    CHECK(format_indicator_markup(items, 1.0)
          == "<b>V1 </b><color=#808080>C1 </color><color=#808080>B1</color>");
}

TEST_CASE("format_wall_clock_hms formats 12 and 24 hour clocks", "[scene]")
{
    REQUIRE(format_wall_clock_hms(14, 5, 9, true, false) == "14:05");
    REQUIRE(format_wall_clock_hms(14, 5, 9, true, true) == "14:05:09");
    REQUIRE(format_wall_clock_hms(14, 5, 9, false, false) == "2:05 PM");
    REQUIRE(format_wall_clock_hms(0, 0, 0, false, true) == "12:00:00 AM");
    REQUIRE(format_wall_clock_hms(12, 0, 0, false, false) == "12:00 PM");
}

TEST_CASE("indicator labels Bible verses by number, with the chapter where it changes", "[scene][indicator][bible]")
{
    const std::vector<std::string> codes{"3:35", "3:36", "4:1", "4:2"};
    const auto items = seventhslide::scene::build_indicator_items_from_sections(codes, 1);
    REQUIRE(labels_of(items) == std::vector<std::string>{"35", "36", "4:1", "2"});
    REQUIRE(items[1].active);
    REQUIRE_FALSE(items[0].active);
}
