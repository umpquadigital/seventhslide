#include <catch2/catch_test_macros.hpp>

#include "layout_test_helpers.hpp"
#include <seventhslide/layout/word_wrap.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/layout/text_measurer.hpp>

#include <string>
#include <type_traits>
#include <vector>

using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::layout::FontStyle;
using seventhslide::layout::bind_measurer;
using seventhslide::test::split_marked_line_to_fit;
using seventhslide::layout::split_line_to_fit;
using seventhslide::layout::wrap_plain_text_to_width;

namespace {

/// Character-length measurer - matches Python tests/test_split_by_words.py.
double len_measure(std::string_view s)
{
    return static_cast<double>(s.size());
}

/// ``text``'s words cut at ``delimiters`` (``chunk_word_runs``), as text.
std::vector<std::string> chunks(std::string_view text, std::string_view delimiters)
{
    const auto words = seventhslide::layout::PlainWords(text);
    std::vector<std::string> out;
    for (const auto& run : seventhslide::layout::chunk_word_runs(words, {0, words.size()},
                                                                 delimiters)) {
        out.push_back(words.text(run));
    }
    return out;
}

/// ``text``'s words split to fit ``max_rows`` rows. ``text`` has no sentence or
/// clause ends, so this is ``split_word_runs_to_fit``'s last resort: packing.
std::vector<std::string> packed(std::string_view text, double max_w, int max_rows)
{
    const auto words = seventhslide::layout::PlainWords(text);
    std::vector<std::string> out;
    for (const auto& run : seventhslide::layout::split_word_runs_to_fit(
             words, {0, words.size()}, len_measure, 1, max_w, max_rows)) {
        out.push_back(words.text(run));
    }
    return out;
}

/// ``len_measure`` for runs of marked-up text (tags are never measured).
double marked_len(std::string_view s, const seventhslide::model::TextStyle& /*style*/)
{
    return static_cast<double>(s.size());
}

// Plain words are measured whole and styled words run by run: neither kind of
// line can be handed to the other's measurer.
template <class Words>
concept wraps_by_runs = requires(const Words& words,
                                 const seventhslide::layout::RunMeasurer& measure) {
    seventhslide::layout::wrap_word_runs(words, 0, 1, measure, 1.0);
};
template <class Words>
concept wraps_to_styled_rows = requires(const Words& words,
                                        const seventhslide::layout::RunMeasurer& measure) {
    seventhslide::layout::wrap_styled_rows(words, {}, measure, 1.0);
    seventhslide::layout::wrap_styled_rows(words, {}, measure, 1.0, 1.0);
};
template <class Words>
concept counts_rows = requires(const Words& words,
                               const seventhslide::layout::RunMeasurer& measure) {
    seventhslide::layout::wrapped_row_count(words, {}, measure, 1.0);
};
template <class Words>
concept finds_wide_words = requires(const Words& words,
                                    const seventhslide::layout::RunMeasurer& measure) {
    seventhslide::layout::has_word_wider_than(words, {}, measure, 1.0);
};
template <class Words>
concept wraps_whole = requires(const Words& words,
                               const seventhslide::layout::WidthMeasurer& measure) {
    seventhslide::layout::wrap_word_runs(words, 0, 1, measure, 1.0);
};
template <class Words>
concept splits_whole = requires(const Words& words,
                                const seventhslide::layout::WidthMeasurer& measure) {
    seventhslide::layout::split_word_runs_to_fit(words, {}, measure, 1, 1.0, 1.0);
};

static_assert(wraps_by_runs<seventhslide::layout::StyledWords>);
static_assert(!wraps_by_runs<seventhslide::layout::PlainWords>);
static_assert(wraps_to_styled_rows<seventhslide::layout::StyledWords>);
static_assert(!wraps_to_styled_rows<seventhslide::layout::PlainWords>);
static_assert(counts_rows<seventhslide::layout::StyledWords>);
static_assert(!counts_rows<seventhslide::layout::PlainWords>);
static_assert(finds_wide_words<seventhslide::layout::StyledWords>);
static_assert(!finds_wide_words<seventhslide::layout::PlainWords>);
static_assert(wraps_whole<seventhslide::layout::PlainWords>);
static_assert(!wraps_whole<seventhslide::layout::StyledWords>);
// The shared base is made only as one of the two, so neither is cut down to it.
static_assert(!std::is_default_constructible_v<seventhslide::layout::LineWords>);
static_assert(!std::is_constructible_v<seventhslide::layout::LineWords,
                                       const seventhslide::layout::StyledWords&>);
static_assert(splits_whole<seventhslide::layout::PlainWords>);
static_assert(!splits_whole<seventhslide::layout::StyledWords>);

}  // namespace

TEST_CASE("wrap_plain_text_to_width matches the golden fixture", "[layout][wrap]")
{
    REQUIRE(wrap_plain_text_to_width("", len_measure, 10) == std::vector<std::string>{""});
    REQUIRE(wrap_plain_text_to_width("   ", len_measure, 10) == std::vector<std::string>{""});
    REQUIRE(wrap_plain_text_to_width("hello", len_measure, 10)
            == std::vector<std::string>{"hello"});
    REQUIRE(wrap_plain_text_to_width("aa bb", len_measure, 10)
            == std::vector<std::string>{"aa bb"});
    REQUIRE(wrap_plain_text_to_width("aaaa bbbb cccc", len_measure, 5)
            == std::vector<std::string>{"aaaa", "bbbb", "cccc"});
    REQUIRE(wrap_plain_text_to_width("one two three four five six", len_measure, 10)
            == std::vector<std::string>{"one two", "three four", "five six"});
    REQUIRE(wrap_plain_text_to_width("abcdefghij next", len_measure, 5)
            == std::vector<std::string>{"abcde", "fghij", "next"});
}

TEST_CASE("chunk_word_runs splits at sentence and clause ends", "[layout][wrap]")
{
    REQUIRE(chunks("Hello world. How are you? Fine!", ".!?")
            == std::vector<std::string>{"Hello world.", "How are you?", "Fine!"});
    // Python heuristic still splits "Mr." when followed by space.
    REQUIRE(chunks("Mr. Smith went home. Yes.", ".!?")
            == std::vector<std::string>{"Mr.", "Smith went home.", "Yes."});
    REQUIRE(chunks("One, two; three, four", ";,")
            == std::vector<std::string>{"One,", "two;", "three,", "four"});
    REQUIRE(chunks("Say \"hi.\" Then go.", ".!?")
            == std::vector<std::string>{"Say \"hi.\"", "Then go."});
}

TEST_CASE("split_word_runs_to_fit packs words into pieces of whole rows", "[layout][wrap]")
{
    // Nothing to split is one empty piece.
    REQUIRE(packed("", 10, 2) == std::vector<std::string>{""});
    REQUIRE(packed("   ", 10, 2) == std::vector<std::string>{""});
    REQUIRE(packed("aa bb", 10, 1) == std::vector<std::string>{"aa bb"});
    REQUIRE(packed("aaaa bbbb cccc", 5, 1)
            == std::vector<std::string>{"aaaa", "bbbb", "cccc"});
    REQUIRE(packed("aaaa bbbb cccc", 5, 2) == std::vector<std::string>{"aaaa bbbb", "cccc"});
    REQUIRE(packed("abcdefghij next", 5, 1) == std::vector<std::string>{"abcdefghij", "next"});

    const std::string text = "one two three four five six seven eight nine ten";
    constexpr double max_w = 12;
    constexpr int max_vl = 2;
    for (const auto& sub : packed(text, max_w, max_vl)) {
        const auto wrapped = wrap_plain_text_to_width(sub, len_measure, max_w);
        REQUIRE(static_cast<int>(wrapped.size()) <= max_vl);
    }
}

TEST_CASE("split_word_runs_to_fit splits by what a caller says fits", "[layout][wrap]")
{
    // Asking after the words' own wrap gives the pieces a row builder gives.
    for (const char* text : {"one two three four five six seven eight nine ten",
                             "One two. Three four five, six seven; eight nine ten!",
                             "abcdefghij next word", "solo"}) {
        const auto words = seventhslide::layout::PlainWords(text);
        for (const int max_rows : {1, 2, 3}) {
            const seventhslide::layout::RunFits fits = [&](seventhslide::layout::WrapRun run) {
                return static_cast<int>(seventhslide::layout::wrap_word_runs(words, run.start,
                                                                             run.end, len_measure,
                                                                             12)
                                            .size())
                    <= max_rows;
            };
            INFO(text << " / " << max_rows);
            CHECK(seventhslide::layout::split_word_runs_to_fit(words, {0, words.size()}, fits)
                  == seventhslide::layout::split_word_runs_to_fit(words, {0, words.size()},
                                                                  len_measure, 1, 12, max_rows));
        }
    }
}

TEST_CASE("A word wider than the box is cut across rows", "[layout][wrap]")
{
    // Plain text: a character a pixel.
    REQUIRE(wrap_plain_text_to_width("ab cdefghijklmno pq", len_measure, 5)
            == std::vector<std::string>{"ab", "cdefg", "hijkl", "mno", "pq"});
    // A cut word's last piece shares its row with the next word.
    REQUIRE(wrap_plain_text_to_width("abcdefg h", len_measure, 5)
            == std::vector<std::string>{"abcde", "fg h"});

    // Marked text keeps its styles across the cut, and one wide character stays.
    using seventhslide::layout::wrap_marked_text_to_width;
    REQUIRE(wrap_marked_text_to_width("<b>abcdefgh</b> i", marked_len, 5)
            == std::vector<std::string>{"<b>abcde</b>", "<b>fgh</b> i"});
    REQUIRE(wrap_marked_text_to_width("abc", marked_len, 0.5)
            == std::vector<std::string>{"a", "b", "c"});
    // Words that fit are not touched, and neither is the only row of one.
    REQUIRE(wrap_marked_text_to_width("<b>abc</b> de", marked_len, 6)
            == std::vector<std::string>{"<b>abc</b> de"});
}

TEST_CASE("split_line_to_fit tiers match the golden fixture", "[layout][wrap]")
{
    REQUIRE(split_line_to_fit("aa bb", len_measure, 12, 10, 100)
            == std::vector<std::string>{"aa bb"});
    // A marked line is measured without its tags, and every piece keeps its
    // styles (the stripped pieces the Python version returned lost them).
    REQUIRE(split_marked_line_to_fit("<b>aa</b> bb", marked_len, 12, 10, 100)
            == std::vector<std::string>{"<b>aa</b> bb"});
    REQUIRE(split_marked_line_to_fit("<b>aaaa</b> bbbb cccc dddd", marked_len, 10, 5, 10)
            == std::vector<std::string>{"<b>aaaa</b>", "bbbb", "cccc", "dddd"});
    REQUIRE(split_line_to_fit("aaaa bbbb cccc dddd", len_measure, 10, 5, 10)
            == std::vector<std::string>{"aaaa", "bbbb", "cccc", "dddd"});
    REQUIRE(
        split_line_to_fit("Hello there friend. More words here now.", len_measure, 10, 12, 10)
        == std::vector<std::string>{"Hello there", "friend.", "More words", "here now."});
    REQUIRE(
        split_line_to_fit(
            "Hello there friend, more words here now and again.", len_measure, 10, 15, 10)
        == std::vector<std::string>{
            "Hello there", "friend,", "more words here", "now and again."});
}

TEST_CASE("bind_measurer works with ApproxTextMeasurer", "[layout][wrap]")
{
    ApproxTextMeasurer approx;
    FontStyle style{.family = "x", .size_px = 20.0};  // 10 px per char
    auto measure = bind_measurer(approx, style);
    // "aaaa" = 40, "bbbb" = 40 -> each alone under width 45
    REQUIRE(wrap_plain_text_to_width("aaaa bbbb", measure, 45.0)
            == std::vector<std::string>{"aaaa", "bbbb"});
}

TEST_CASE("A margin is the space after its run, and a row may break there", "[layout][wrap]")
{
    using seventhslide::model::parse_inline_markup;
    const auto measure = [](std::string_view text, const seventhslide::model::TextStyle& style) {
        return static_cast<double>(text.size()) * 10.0 + style.margin_right_pct;
    };
    const auto words = seventhslide::layout::StyledWords::spans(
        parse_inline_markup("<gap=5>3:1</gap> In"));
    REQUIRE(words.size() == 2);
    // 35 for the number and its margin, no space, 20 for "In".
    CHECK(words.width(0, 2, measure) == 55.0);

    const auto text = [](const seventhslide::model::RichLine& row) {
        return seventhslide::model::plain_text(row);
    };
    const auto rows = seventhslide::layout::wrap_styled_rows(words, {0, 2}, measure, 40.0);
    REQUIRE(rows.size() == 2);
    CHECK(text(rows[0]) == "3:1");
    CHECK(text(rows[1]) == "In");

    // A gap of nothing is no space either.
    const auto touching = seventhslide::layout::StyledWords::spans(
        parse_inline_markup("<gap=0>3:1</gap> In"));
    CHECK(touching.width(0, 2, measure) == 50.0);
}

TEST_CASE("A run whose margin is its space is never cut", "[layout][wrap]")
{
    const auto measure = [](std::string_view text, const seventhslide::model::TextStyle& style) {
        return static_cast<double>(text.size()) * 10.0 + style.margin_right_pct;
    };
    const auto words = seventhslide::layout::StyledWords::spans(
        seventhslide::model::parse_inline_markup("<gap=5>3:176</gap> In"));
    const auto rows = seventhslide::layout::wrap_styled_rows(words, {0, 2}, measure, 30.0);
    REQUIRE(rows.size() == 2);
    CHECK(seventhslide::model::plain_text(rows[0]) == "3:176");
    CHECK(seventhslide::model::plain_text(rows[1]) == "In");
}

TEST_CASE("Splitting re-wraps a run only for a word too wide to count as a row", "[layout][wrap]")
{
    std::string line = "1:1";
    for (int i = 0; i < 60; ++i) {
        line += " word";
    }
    std::size_t calls = 0;
    const seventhslide::layout::WidthMeasurer measure = [&](std::string_view text) {
        ++calls;
        return static_cast<double>(text.size()) * 10.0;
    };
    const auto pieces = split_line_to_fit(line, measure, 40, 200.0, 200.0);
    CHECK(pieces.size() > 1);
    CHECK(calls < 300);  // about one for each word, twice, not one for each word per row
}
