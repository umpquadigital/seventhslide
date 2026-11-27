#include <catch2/catch_test_macros.hpp>

#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/parse/lyrics_blocks.hpp>

#include <string>
#include <vector>

using seventhslide::model::format_inline_markup;
using seventhslide::model::k_forced_split;
using seventhslide::model::plain_text;
using seventhslide::parse::base_verse_code;
using seventhslide::parse::is_theme_code;
using seventhslide::parse::is_title_code;
using seventhslide::parse::join_labelled_verses;
using seventhslide::parse::label_to_code;
using seventhslide::parse::labelled_verses;
using seventhslide::parse::LabelledVerse;
using seventhslide::parse::parse_verses;
using seventhslide::parse::same_verse_sections;
using seventhslide::parse::validate_no_blank_lines_in_verse;
using seventhslide::parse::validate_verse_order;

TEST_CASE("label_to_code maps OpenLyrics labels", "[parse][lyrics]")
{
    REQUIRE(label_to_code("Verse:1") == "v1");
    REQUIRE(label_to_code("Verse:1a") == "v1");
    REQUIRE(label_to_code("Chorus:2") == "c2");
    REQUIRE(label_to_code("Title:1") == "t1");
    REQUIRE(label_to_code("Pre-Chorus:1") == "p1");
    REQUIRE(label_to_code("BRIDGE:10b") == "b10");
    REQUIRE(label_to_code("Misc") == "misc");
    REQUIRE(label_to_code("verse 3") == "misc");
    REQUIRE(label_to_code("Ending:1") == "e1");
    REQUIRE(label_to_code("Intro:1") == "i1");
    REQUIRE(label_to_code("Other:1") == "o1");
}

TEST_CASE("verse label/code round-trip for editor types", "[parse][lyrics]")
{
    using seventhslide::parse::canonicalize_verse_type;
    using seventhslide::parse::clamp_verse_number;
    using seventhslide::parse::verse_code_from_label;
    using seventhslide::parse::verse_indicator_text;
    using seventhslide::parse::verse_label_from_code;
    using seventhslide::parse::verse_type_names;

    REQUIRE(canonicalize_verse_type("verse") == "Verse");
    REQUIRE(canonicalize_verse_type("Pre-Chorus") == "Pre-Chorus");
    REQUIRE(canonicalize_verse_type("None").empty());
    REQUIRE(canonicalize_verse_type("").empty());
    REQUIRE(clamp_verse_number(0) == 1);
    REQUIRE(clamp_verse_number(100) == 99);

    REQUIRE(verse_code_from_label("Verse", 1) == "v1");
    REQUIRE(verse_code_from_label("Bridge", 2) == "b2");
    REQUIRE(verse_code_from_label("Pre-Chorus", 1) == "p1");
    REQUIRE(verse_code_from_label("", 3).empty());
    REQUIRE(verse_indicator_text("Verse", 1) == "V1");
    REQUIRE(verse_indicator_text("Chorus", 2) == "C2");
    REQUIRE(verse_indicator_text("", 1).empty());

    const auto v1 = verse_label_from_code("v1");
    REQUIRE(v1.type == "Verse");
    REQUIRE(v1.number == 1);
    const auto b = verse_label_from_code("b");
    REQUIRE(b.type == "Bridge");
    REQUIRE(b.number == 1);
    const auto v1a = verse_label_from_code("v1a");
    REQUIRE(v1a.type == "Verse");
    REQUIRE(v1a.number == 1);
    REQUIRE(verse_label_from_code("").type.empty());
    REQUIRE(verse_label_from_code("misc").type.empty());
    // A run of digits longer than any int holds is the largest verse.
    const auto huge = verse_label_from_code("v12345678901234567890");
    REQUIRE(huge.type == "Verse");
    REQUIRE(huge.number == 99);
    REQUIRE(verse_label_from_code("c99999999999999999999").number == 99);

    REQUIRE(verse_type_names().size() >= 7);
    REQUIRE(verse_type_names().front() == "Verse");
}

TEST_CASE("title / section helpers", "[parse][lyrics]")
{
    REQUIRE(is_title_code("t1"));
    REQUIRE(is_title_code("t12"));
    REQUIRE(is_title_code("t1a"));
    REQUIRE_FALSE(is_title_code("T1"));
    REQUIRE_FALSE(is_title_code("v1"));
    REQUIRE_FALSE(is_title_code("t"));

    REQUIRE(is_theme_code("th1"));
    REQUIRE(is_theme_code("th12"));
    REQUIRE_FALSE(is_theme_code("TH1"));
    REQUIRE_FALSE(is_theme_code("t1"));
    REQUIRE_FALSE(is_theme_code("th"));
    REQUIRE_FALSE(is_theme_code("th1a"));

    REQUIRE(base_verse_code("v1a") == "v1");
    REQUIRE(base_verse_code("v1") == "v1");
    REQUIRE(base_verse_code("c2b") == "c2");

    REQUIRE(same_verse_sections("v1a", "v1b"));
    REQUIRE_FALSE(same_verse_sections("v1a", "v2a"));
    REQUIRE_FALSE(same_verse_sections("v1", "v1a"));
}

TEST_CASE("parse_verses structured appearance order", "[parse][lyrics]")
{
    const auto blocks = parse_verses(
        "---[Verse:1]---\nA\nB\n\n---[Chorus:1]---\nC\n");
    REQUIRE(blocks.verse_codes == std::vector<std::string>{"v1", "c1"});
    REQUIRE(blocks.verses == std::vector<std::string>{"A\nB", "C"});
}

TEST_CASE("parse_verses applies verse_order and repeats", "[parse][lyrics]")
{
    const auto blocks = parse_verses(
        "---[Verse:1]---\nA\n\n---[Chorus:1]---\nC\n", "v1 c1 v1");
    REQUIRE(blocks.verse_codes == std::vector<std::string>{"v1", "c1", "v1"});
    REQUIRE(blocks.verses == std::vector<std::string>{"A", "C", "A"});
}

TEST_CASE("parse_verses merges lettered sections into one verse", "[parse][lyrics]")
{
    const auto all = parse_verses(
        "---[Verse:1a]---\nA\n\n---[Verse:1b]---\nB\n\n---[Chorus:1]---\nC\n",
        "v1 c1");
    REQUIRE(all.verse_codes == std::vector<std::string>{"v1", "c1"});
    REQUIRE(all.verses.size() == 2);
    REQUIRE(all.verses[0].find("A") != std::string::npos);
    REQUIRE(all.verses[0].find("B") != std::string::npos);
    REQUIRE(all.verses[0].find(std::string(k_forced_split)) != std::string::npos);
    REQUIRE(all.verses[1] == "C");

    const auto exact = parse_verses(
        "---[Verse:1a]---\nA\n\n---[Verse:1b]---\nB\n", "v1a");
    REQUIRE(exact.verse_codes == std::vector<std::string>{"v1"});
    REQUIRE(exact.verses.size() == 1);
    REQUIRE(exact.verses[0].find("A") != std::string::npos);
    REQUIRE(exact.verses[0].find("B") != std::string::npos);

    const auto collapsed = parse_verses(
        "---[Verse:1a]---\nA\n\n---[Verse:1b]---\nB\n", "v1a v1b");
    REQUIRE(collapsed.verse_codes == std::vector<std::string>{"v1"});
}

TEST_CASE("parse_verses picks a verse by its whole number", "[parse][lyrics]")
{
    const auto blocks = parse_verses("---[Verse:1]---\nA\n\n---[Verse:10]---\nJ\n", "v10a v1");
    REQUIRE(blocks.verse_codes == std::vector<std::string>{"v10", "v1"});
    REQUIRE(blocks.verses == std::vector<std::string>{"J", "A"});
}

TEST_CASE("parse_verses skips theme chips in verse order", "[parse][lyrics]")
{
    const auto blocks = parse_verses(
        "---[Verse:1]---\nA\n\n---[Chorus:1]---\nC\n", "TH1 v1 th2 c1");
    REQUIRE(blocks.verse_codes == std::vector<std::string>{"v1", "c1"});
    REQUIRE(blocks.verses == std::vector<std::string>{"A", "C"});

    const auto only_theme = parse_verses(
        "---[Verse:1]---\nA\n", "TH1");
    REQUIRE(only_theme.verses.empty());
}

TEST_CASE("validate_verse_order allows theme chips", "[parse][lyrics]")
{
    const std::string lyrics = "---[Verse:1]---\nA\n\n---[Chorus:1]---\nC\n";
    REQUIRE_FALSE(validate_verse_order("TH1 v1 c1", lyrics).has_value());
    REQUIRE_FALSE(validate_verse_order("th2 t1 v1", lyrics).has_value());
}

TEST_CASE("parse_verses suppress_titles virtual and baked", "[parse][lyrics]")
{
    const std::string lyrics = "---[Title:1]---\nOld\n\n---[Verse:1]---\nA\n";

    const auto kept = parse_verses(lyrics);
    REQUIRE(kept.verse_codes == std::vector<std::string>{"t1", "v1"});
    REQUIRE(kept.verses == std::vector<std::string>{"Old", "A"});

    const auto suppressed = parse_verses(lyrics, "t1 v1", true);
    REQUIRE(suppressed.verse_codes == std::vector<std::string>{"t1", "v1"});
    REQUIRE(suppressed.verses == std::vector<std::string>{"", "A"});
}

TEST_CASE("parse_verses plain blank-line split", "[parse][lyrics]")
{
    const auto plain = parse_verses("Para one\n\nPara two");
    REQUIRE(plain.verse_codes == std::vector<std::string>{"", ""});
    REQUIRE(plain.verses == std::vector<std::string>{"Para one", "Para two"});

    const auto with_title = parse_verses("Para one\n\nPara two", "t1 v1", true);
    REQUIRE(with_title.verse_codes == std::vector<std::string>{"t1", "", ""});
    REQUIRE(with_title.verses.size() == 3);
    REQUIRE(with_title.verses[0].empty());
    REQUIRE(with_title.verses[1] == "Para one");
    REQUIRE(with_title.verses[2] == "Para two");

    // Without suppress_titles, t1 tokens are ignored for plain lyrics.
    const auto no_suppress = parse_verses("Para one", "t1 v1", false);
    REQUIRE(no_suppress.verse_codes == std::vector<std::string>{""});
    REQUIRE(no_suppress.verses == std::vector<std::string>{"Para one"});
}

TEST_CASE("parse_verses order miss falls back to appearance", "[parse][lyrics]")
{
    const auto blocks = parse_verses("---[Verse:1]---\nA\n", "v9");
    REQUIRE(blocks.verse_codes == std::vector<std::string>{"v1"});
    REQUIRE(blocks.verses == std::vector<std::string>{"A"});
}

TEST_CASE("parse_verses empty and type map coverage", "[parse][lyrics]")
{
    REQUIRE(parse_verses("").empty());

    const auto types = parse_verses(
        "---[Bridge:1]---\nB\n\n---[Ending:1]---\nE\n\n---[Intro:1]---\nI\n\n"
        "---[Pre-Chorus:1]---\nP\n\n---[Other:1]---\nO\n");
    REQUIRE(types.verse_codes
            == std::vector<std::string>{"b1", "e1", "i1", "p1", "o1"});
}

TEST_CASE("parse_verses keeps a forced break inside its verse", "[parse][lyrics]")
{
    const auto blocks = parse_verses(
        "---[Verse:1]---\n[G]Hello <b>x</b> <size=80>y</size>\n"
            + std::string(k_forced_split) + "\n"
            + "Line B\n",
        "v1");
    REQUIRE(blocks.verses.size() == 1);
    REQUIRE(blocks.verses[0].find(std::string(k_forced_split)) != std::string::npos);
}

TEST_CASE("validate_verse_order and blank lines", "[parse][lyrics]")
{
    const std::string lyrics = "---[Verse:1]---\nA\n\n---[Chorus:1]---\nC\n";
    REQUIRE_FALSE(validate_verse_order("v1 c1", lyrics).has_value());
    REQUIRE_FALSE(validate_verse_order("t1 v1", lyrics).has_value());
    // A section letter selects its verse.
    REQUIRE_FALSE(validate_verse_order("v1a c1b", lyrics).has_value());
    const auto bad = validate_verse_order("v1 v9", lyrics);
    REQUIRE(bad.has_value());
    REQUIRE(bad->find("v9") != std::string::npos);
    // The codes the song has are named as such (it used to say "val:").
    REQUIRE(*bad == "Invalid codes: v9\nValid codes: c1, v1");

    REQUIRE_FALSE(validate_no_blank_lines_in_verse(lyrics).has_value());
    const auto mid_blank = validate_no_blank_lines_in_verse(
        "---[Verse:1]---\nA\n\nB\n");
    REQUIRE(mid_blank.has_value());
    REQUIRE(mid_blank->find("blank line") != std::string::npos);

    // Plain lyrics: blank lines are verse separators - no error.
    REQUIRE_FALSE(validate_no_blank_lines_in_verse("A\n\nB").has_value());

    // OpenSong / plain lyrics: no --- headers -> order check is a no-op.
    REQUIRE_FALSE(validate_verse_order("v1 v9", "[V1]\nOne").has_value());
}

TEST_CASE("verse type table drives codes and labels", "[parse][lyrics]")
{
    namespace parse = seventhslide::parse;
    REQUIRE(parse::verse_type_names().size() == parse::k_verse_types.size());
    for (const auto& type : parse::k_verse_types) {
        REQUIRE(parse::verse_type_for_code(type.code) == &type);
        const auto code = parse::verse_code_from_label(type.name, 3);
        REQUIRE(code == std::string(1, type.code) + "3");
        REQUIRE(parse::label_to_code(std::string(type.name) + ":3") == code);
        REQUIRE(parse::verse_label_from_code(code).type == type.name);
    }
    REQUIRE(parse::verse_code_from_label("PRE-chorus", 1) == "p1");
    REQUIRE(parse::verse_type_for_code('x') == nullptr);
    REQUIRE(parse::verse_code_from_label(" chorus ", 0) == "c1");
    REQUIRE(parse::verse_code_from_label("Bridge", 250) == "b99");
    REQUIRE(parse::verse_indicator_text("Pre-Chorus", 2) == "P2");
}

TEST_CASE("labelled_verses splits lyrics the way the song editor edits them", "[parse][lyrics]")
{
    const std::string joined_break = std::string("\n") + std::string(k_forced_split) + "\n";
    SECTION("headed sections, preamble dropped, bodies trimmed")
    {
        CHECK(labelled_verses("intro text\n---[Verse:1]---\n  Amazing grace\n\n---[Chorus:1]---\nGone\n")
              == std::vector<LabelledVerse>{{"Verse:1", "Amazing grace"}, {"Chorus:1", "Gone"}});
    }
    SECTION("lettered sections of one verse join with a forced break")
    {
        CHECK(labelled_verses("---[Verse:1a]---\nOne\n---[verse:1b]---\nTwo\n")
              == std::vector<LabelledVerse>{{"Verse:1", "One" + joined_break + "Two"}});
    }
    SECTION("a section letter is one letter after the number")
    {
        CHECK(labelled_verses("---[Chorus:12B]---\nA\n")
              == std::vector<LabelledVerse>{{"Chorus:12", "A"}});
        CHECK(labelled_verses("---[Verse:a]---\nB\n") == std::vector<LabelledVerse>{{"Verse:a", "B"}});
        CHECK(labelled_verses("---[Verse1a]---\nC\n") == std::vector<LabelledVerse>{{"Verse1a", "C"}});
        CHECK(labelled_verses("---[:1a]---\nD\n") == std::vector<LabelledVerse>{{":1a", "D"}});
    }
    SECTION("title sections never join")
    {
        CHECK(labelled_verses("---[Title:1a]---\nA\n---[Title:1b]---\nB\n")
              == std::vector<LabelledVerse>{{"Title:1", "A"}, {"Title:1", "B"}});
    }
    SECTION("without headers, blank-line blocks become numbered verses")
    {
        CHECK(labelled_verses("One\nline\n\n\n\n  Two  \n\n")
              == std::vector<LabelledVerse>{{"Verse:1", "One\nline"}, {"Verse:2", "Two"}});
        CHECK(labelled_verses("   \n").empty());
    }
}

TEST_CASE("labelled_verses shows the verses playback plays", "[parse][lyrics]")
{
    // The editor and the slide builder read one parse, so what is edited as a
    // verse is what shows as one.
    for (const char* lyrics : {
             "---[Verse:1]---\nA\n---[Chorus:1]---\nB\n",
             "---[Verse:1a]---\nA\n---[Verse:1b]---\nB\n---[Chorus:1]---\nC\n",
             "---[Verse:1a]---\nA\n---[VERSE:1B]---\nB\n",
             "---[Title:1a]---\nT\n---[Title:1b]---\nU\n---[Verse:2]---\nV\n",
             "---[Verse:1a]---\nA\n---[Chorus:1]---\nC\n---[Verse:1b]---\nB\n",
         }) {
        INFO(lyrics);
        const auto played = parse_verses(lyrics);
        const auto edited = labelled_verses(lyrics);
        REQUIRE(edited.size() == played.verses.size());
        for (std::size_t i = 0; i < edited.size(); ++i) {
            CHECK(edited[i].content == played.verses[i]);
            CHECK(label_to_code(edited[i].label) == played.verse_codes[i]);
        }
    }
}

TEST_CASE("join_labelled_verses writes what labelled_verses reads", "[parse][lyrics]")
{
    const std::vector<LabelledVerse> verses{{"Verse:1", "A\nB"}, {"Chorus:1", "C"}};
    const auto lyrics = join_labelled_verses(verses);
    CHECK(lyrics == "---[Verse:1]---\nA\nB\n\n---[Chorus:1]---\nC\n");
    CHECK(labelled_verses(lyrics) == verses);
    CHECK(join_labelled_verses({}).empty());
}

// The song editor's label grammar. Each case matches what the editor's own
// JavaScript did with that label before it moved here.
TEST_CASE("Verse labels read the way the song editor reads them", "[parse][lyrics][labels]")
{
    namespace parse = seventhslide::parse;
    CHECK(parse::verse_display_name("Verse:2") == "Verse 2");
    CHECK(parse::verse_display_name("Verse:12b") == "Verse 12");
    CHECK(parse::verse_display_name("a:1:2") == "a:1 2");
    CHECK(parse::verse_display_name(":4") == ":4");
    CHECK(parse::verse_display_name("Chorus") == "Chorus");

    CHECK(parse::editor_verse_type("chorus:1") == "Chorus");
    CHECK(parse::editor_verse_type("PRE-CHORUS:2") == "Pre-Chorus");
    CHECK(parse::editor_verse_type("Title:1") == "Verse");
    CHECK(parse::editor_verse_type("Misc:3") == "Verse");
    CHECK(parse::editor_verse_type("") == "Verse");

    CHECK(parse::verse_number("Verse:3a") == 3);
    CHECK(parse::verse_number("Verse") == 1);
    CHECK(parse::verse_number("Bridge:0") == 1);
    CHECK(parse::verse_number("Bridge:100") == 99);
    CHECK(parse::verse_number("Bridge:99999999999") == 99);
    CHECK(parse::verse_number("Verse::3") == 3); // the first ":" with digits

    CHECK(parse::make_verse_label("Chorus", 2) == "Chorus:2");
    CHECK(parse::make_verse_label("", 0) == "Verse:1");
    CHECK(parse::make_verse_label("Verse", 100) == "Verse:99");
}

TEST_CASE("Verse labels find a verse and the next free number", "[parse][lyrics][labels]")
{
    namespace parse = seventhslide::parse;
    const std::vector<std::string> labels{"verse:1a", "VERSE:1b", "Verse:3", "Chorus:1"};
    CHECK(parse::find_verse_label(labels, "Verse", 1) == 0);
    CHECK(parse::find_verse_label(labels, "Verse", 1, 0) == 1);
    CHECK(parse::find_verse_label(labels, "Verse", 2) == -1);
    CHECK(parse::find_verse_label(labels, "Chorus", 1) == 3);

    CHECK(parse::next_free_verse_number(labels, "Verse", 1) == 2);
    CHECK(parse::next_free_verse_number(labels, "Verse", 3) == 4);
    CHECK(parse::next_free_verse_number(labels, "Verse", 3, 2) == 3);
    CHECK(parse::next_free_verse_number(labels, "Bridge", 0) == 1);

    std::vector<std::string> full;
    for (int n = 2; n <= 99; ++n) {
        full.push_back("Verse:" + std::to_string(n));
    }
    // Nothing free from 50 up: wrap round to the one free number.
    CHECK(parse::next_free_verse_number(full, "Verse", 50) == 1);
    // None at all: say so, rather than a number another verse has.
    full.emplace_back("Verse:1");
    CHECK(parse::next_free_verse_number(full, "Verse", 50) == -1);
    CHECK(parse::next_free_verse_number(full, "Verse", 50, 0) == 2);
}

TEST_CASE("A typed verse order splits into upper-cased codes", "[parse][lyrics][labels]")
{
    namespace parse = seventhslide::parse;
    CHECK(parse::verse_order_tokens("  v1\tc1\nv2  ") == std::vector<std::string>{"V1", "C1", "V2"});
    CHECK(parse::verse_order_tokens("th1 T1 v1a") == std::vector<std::string>{"TH1", "T1", "V1A"});
    CHECK(parse::verse_order_tokens("").empty());
}
