#include <catch2/catch_test_macros.hpp>

#include <seventhslide/model/rich_text.hpp>

using seventhslide::model::RichLine;
using seventhslide::model::TextSpan;
using seventhslide::model::TextStyle;
using seventhslide::model::clamp_rise_pct;
using seventhslide::model::clamp_size_pct;
using seventhslide::model::format_inline_markup;
using seventhslide::model::format_markup_lines;
using seventhslide::model::is_forced_break_line;
using seventhslide::model::k_forced_split;
using seventhslide::model::normalize_imported_markup;
using seventhslide::model::parse_inline_markup;
using seventhslide::model::parse_markup_lines;
using seventhslide::model::plain_text;
using seventhslide::model::reconcile_rich_line;
using seventhslide::model::reconcile_rich_lines;
using seventhslide::model::escape_markup_text;
using seventhslide::model::match_markup_tag;
using seventhslide::model::strip_markup_tags;

namespace {

TextSpan span(std::string text, TextStyle style = {})
{
    return TextSpan{std::move(text), style};
}

} // namespace

TEST_CASE("clamp_size_pct matches render_safe bounds", "[model][rich_text]")
{
    REQUIRE(clamp_size_pct(80) == 80);
    REQUIRE(clamp_size_pct(5) == 10);
    REQUIRE(clamp_size_pct(500) == 400);
    REQUIRE(clamp_size_pct(100) == 100);
}

// Test names stay ASCII: on Windows, Catch2's discovery reads the list of
// names in the console's code page, so a "–" comes back as another name and
// ctest runs nothing.
TEST_CASE("clamp_rise_pct stays in 0 to 100", "[model][rich_text]")
{
    REQUIRE(clamp_rise_pct(40) == 40);
    REQUIRE(clamp_rise_pct(-1) == 0);
    REQUIRE(clamp_rise_pct(140) == 100);
}

TEST_CASE("match_markup_tag accepts exactly the tag set", "[model][rich_text]")
{
    const auto name_of = [](std::string_view s) {
        const auto tag = match_markup_tag(s);
        return tag ? (tag->close ? "/" : "") + tag->name : std::string("-");
    };
    REQUIRE(name_of("<b>Hi") == "b");
    REQUIRE(name_of("<B>Hi") == "b");
    REQUIRE(name_of("</b>") == "/b");
    REQUIRE(name_of("<strong>") == "b");
    REQUIRE(name_of("<em>") == "i");
    REQUIRE(name_of("<u>") == "u");
    REQUIRE(name_of("<SUP>") == "super");
    REQUIRE(name_of("<super>") == "super");
    REQUIRE(name_of("<SIZE=120>") == "size");
    REQUIRE(name_of("</size>") == "/size");
    REQUIRE(name_of("<rise=40>") == "rise");
    REQUIRE(name_of("<color=#AABBCC>") == "color");
    REQUIRE(name_of("<font=\"Times New Roman\">") == "font");
    REQUIRE(match_markup_tag("<size=120>")->number == 120);
    REQUIRE(match_markup_tag("<COLOR=#AABBCC>")->value == "#aabbcc");
    REQUIRE(match_markup_tag("<FONT=\"Times New Roman\">")->value == "Times New Roman");
    // Padding around the name and inside its quotes is not part of it.
    REQUIRE(match_markup_tag("<font=  ' Open Sans '  >")->value == "Open Sans");
    REQUIRE(match_markup_tag("<font=Open Sans>")->value == "Open Sans");

    // Ordinary text, not tags.
    REQUIRE(name_of("< b and c > d") == "-");
    REQUIRE(name_of("<b and c> d") == "-");
    REQUIRE(name_of("<B onclick=\"x\">") == "-");
    REQUIRE(name_of("<3 you>") == "-");
    REQUIRE(name_of("<img src=x>") == "-");
    REQUIRE(name_of("<br>") == "-");
    REQUIRE(name_of("<font=>") == "-");
    REQUIRE(name_of("<color=#gg>") == "-");
    REQUIRE(name_of("<size=1234>") == "-");
    REQUIRE(name_of("<b") == "-");
    // A '>' too far away to close any tag leaves the '<' as text.
    REQUIRE(name_of("<b" + std::string(200, ' ') + ">") == "-");
}

TEST_CASE("lyrics with angle brackets keep their text", "[model][rich_text]")
{
    // Regression: text between '<' and '>' used to be read as a tag and
    // deleted, or stored HTML-escaped and shown as "&lt;".
    REQUIRE(plain_text(parse_inline_markup("if a < b and c > d then"))
            == "if a < b and c > d then");
    REQUIRE(plain_text(parse_inline_markup("Tom <3 you> yes")) == "Tom <3 you> yes");
    REQUIRE(parse_inline_markup("if a < b and c > d then").size() == 1);
    REQUIRE(strip_markup_tags("x<script>y</script>") == "x<script>y</script>");

    // Formatting around such text still works.
    const RichLine mixed = parse_inline_markup("<b>I</b> <3 you");
    REQUIRE(mixed.size() == 2);
    REQUIRE(mixed[0] == span("I", TextStyle{.bold = true}));
    REQUIRE(mixed[1] == span(" <3 you"));
}

TEST_CASE("markup escaping is minimal and lossless", "[model][rich_text]")
{
    // Only text that would read as a tag or an entity is escaped.
    REQUIRE(escape_markup_text("a < b & c") == "a < b & c");
    REQUIRE(escape_markup_text("Tom <3 you> yes") == "Tom <3 you> yes");
    REQUIRE(escape_markup_text("say <b> out loud") == "say &lt;b> out loud");
    REQUIRE(escape_markup_text("write &lt; for <") == "write &amp;lt; for <");

    const char* literals[] = {
        "say <b> out loud",
        "write &lt; for <",
        "a < b and c > d",
        "&amp;&amp;",
        "</size> <font=X> <color=#fff>",
    };
    for (const char* literal : literals) {
        CAPTURE(literal);
        const RichLine line{span(literal, TextStyle{.italic = true})};
        const RichLine back = parse_inline_markup(format_inline_markup(line));
        REQUIRE(back == line);
    }

    // Text an older build stored HTML-escaped now reads back correctly.
    REQUIRE(plain_text(parse_inline_markup("Tom &lt;3 you&gt; yes")) == "Tom <3 you> yes");
}

TEST_CASE("normalize_imported_markup keeps a paragraph's settings", "[model][rich_text]")
{
    REQUIRE(normalize_imported_markup("<p=align:center>Line") == "<p=align:center>Line");
}

TEST_CASE("normalize_imported_markup maps HTML formatting and keeps other text", "[model][rich_text]")
{
    REQUIRE(normalize_imported_markup("a<br>b") == "a\nb");
    REQUIRE(normalize_imported_markup("a<br/>b") == "a\nb");
    REQUIRE(normalize_imported_markup("a&amp;b") == "a&b");
    REQUIRE(normalize_imported_markup("<b>Hi</b>") == "<b>Hi</b>");
    REQUIRE(normalize_imported_markup("<B onclick=\"x\">Hi</B>") == "<b>Hi</b>");
    REQUIRE(normalize_imported_markup("<i style=\"color:red\">y</i>") == "<i>y</i>");
    REQUIRE(normalize_imported_markup("<strong>x</strong>") == "<b>x</b>");
    REQUIRE(normalize_imported_markup("<SIZE=120>A</SIZE>") == "<size=120>A</size>");
    REQUIRE(normalize_imported_markup("<img src=x>") == "<img src=x>");
    REQUIRE(normalize_imported_markup("if a < b and c > d") == "if a < b and c > d");
    REQUIRE(normalize_imported_markup("a&amp;lt;b") == "a&amp;lt;b");
    REQUIRE(plain_text(parse_inline_markup(normalize_imported_markup("a&amp;lt;b")))
            == "a&lt;b");
}

// A double-escaped file (``&amp;#8217;``) leaves numeric entities behind
// the XML parser; only ASCII ones used to be decoded.
TEST_CASE("normalize_imported_markup decodes numeric entities to UTF-8", "[model][rich_text]")
{
    REQUIRE(normalize_imported_markup("Don&#8217;t") == "Don\u2019t");
    REQUIRE(normalize_imported_markup("Don&#x2019;t") == "Don\u2019t");
    REQUIRE(normalize_imported_markup("caf&#233;") == "caf\u00e9");
    REQUIRE(normalize_imported_markup("&#x1F600;") == "\U0001F600");
    REQUIRE(normalize_imported_markup("&#65;") == "A");
    // Not a character a text can hold, or not a number: left as typed.
    REQUIRE(normalize_imported_markup("&#55296;") == "&#55296;");
    REQUIRE(normalize_imported_markup("&#x110000;") == "&#x110000;");
    REQUIRE(normalize_imported_markup("&#0;") == "&#0;");
    REQUIRE(normalize_imported_markup("&#12ab;") == "&#12ab;");
    REQUIRE(normalize_imported_markup("&#;") == "&#;");
}

TEST_CASE("parse_inline_markup builds styled spans", "[model][rich_text]")
{
    SECTION("plain")
    {
        const RichLine line = parse_inline_markup("Hello");
        REQUIRE(line.size() == 1);
        REQUIRE(line[0].text == "Hello");
        REQUIRE(line[0].style == TextStyle{});
    }

    SECTION("bold then plain")
    {
        const RichLine line = parse_inline_markup("<b>Hi</b> there");
        REQUIRE(line.size() == 2);
        REQUIRE(line[0] == span("Hi", TextStyle{.bold = true}));
        REQUIRE(line[1] == span(" there"));
    }

    SECTION("nested bold italic")
    {
        const RichLine line = parse_inline_markup("<b>bold <i>and</i> more</b>");
        REQUIRE(line.size() == 3);
        REQUIRE(line[0] == span("bold ", TextStyle{.bold = true}));
        REQUIRE(line[1] == span("and", TextStyle{.bold = true, .italic = true}));
        REQUIRE(line[2] == span(" more", TextStyle{.bold = true}));
    }

    SECTION("size nested innermost wins")
    {
        const RichLine line = parse_inline_markup("<size=80>a<size=50>b</size>c</size>");
        REQUIRE(line.size() == 3);
        REQUIRE(line[0] == span("a", TextStyle{.size_pct = 80}));
        REQUIRE(line[1] == span("b", TextStyle{.size_pct = 50}));
        REQUIRE(line[2] == span("c", TextStyle{.size_pct = 80}));
    }

    SECTION("size clamped on parse")
    {
        const RichLine line = parse_inline_markup("<size=5>tiny</size>");
        REQUIRE(line.size() == 1);
        REQUIRE(line[0].style.size_pct == 10);
    }

    SECTION("chords stay plain text")
    {
        const RichLine line = parse_inline_markup("Hello [C] world");
        REQUIRE(plain_text(line) == "Hello [C] world");
        REQUIRE(line.size() == 1);
    }

    SECTION("bold around size")
    {
        const RichLine line = parse_inline_markup("<b><size=80>x</size></b>");
        REQUIRE(line.size() == 1);
        REQUIRE(line[0] == span("x", TextStyle{.bold = true, .size_pct = 80}));
    }

    SECTION("color run")
    {
        const RichLine line = parse_inline_markup("Hi <color=#888888>dim</color>");
        REQUIRE(line.size() == 2);
        REQUIRE(line[0] == span("Hi "));
        REQUIRE(line[1] == span("dim", TextStyle{.color = "#888888"}));
    }

    SECTION("font run")
    {
        const RichLine line = parse_inline_markup("Hi <font=Times New Roman>serif</font>");
        REQUIRE(line.size() == 2);
        REQUIRE(line[0] == span("Hi "));
        REQUIRE(line[1] == span("serif", TextStyle{.font_family = "Times New Roman"}));
    }

    SECTION("super run")
    {
        const RichLine line = parse_inline_markup("<super>3:16</super> For God");
        REQUIRE(line.size() == 2);
        REQUIRE(line[0] == span("3:16", TextStyle{.baseline_rise_pct = 40}));
        REQUIRE(line[1] == span(" For God"));
    }

    SECTION("sup alias and nested size")
    {
        const RichLine line = parse_inline_markup("<size=50><sup>3:16</sup></size>");
        REQUIRE(line.size() == 1);
        REQUIRE(line[0] == span("3:16", TextStyle{.size_pct = 50, .baseline_rise_pct = 40}));
    }

    SECTION("explicit rise")
    {
        const RichLine line = parse_inline_markup("<rise=20>mid</rise>");
        REQUIRE(line.size() == 1);
        REQUIRE(line[0].style.baseline_rise_pct == 20);
    }
}

TEST_CASE("format_inline_markup round-trips through parse", "[model][rich_text]")
{
    const char* samples[] = {
        "Hello",
        "<b>Hi</b> there",
        "<b>bold <i>and</i> more</b>",
        "<size=80>Small</size> normal",
        "<size=80>a<size=50>b</size>c</size>",
        "<b><size=80>x</size></b>",
        "<size=80><b>x</b></size>",
        "pre <u>under</u> post",
        "Hello [C] world",
        "[---][---]",
        "Hi <color=#888888>dim</color>",
        "<color=#ff0000><b>red</b></color>",
        "Hi <font=Helvetica>plain</font>",
        "<font=Times New Roman><size=80>serif</size></font>",
        "<super>3:16</super> For God",
        "<size=50><super>3:16</super></size> body",
        "<rise=20>mid</rise> base",
    };

    for (const char* sample : samples) {
        CAPTURE(sample);
        const RichLine once = parse_inline_markup(sample);
        const std::string formatted = format_inline_markup(once);
        const RichLine again = parse_inline_markup(formatted);
        REQUIRE(again == once);
        REQUIRE(plain_text(again) == plain_text(once));
    }
}

TEST_CASE("parse and format multi-line markup", "[model][rich_text]")
{
    const auto lines = parse_markup_lines("a\n<b>b</b>\n");
    REQUIRE(lines.size() == 3);
    REQUIRE(plain_text(lines[0]) == "a");
    REQUIRE(plain_text(lines[1]) == "b");
    REQUIRE(lines[1][0].style.bold);
    REQUIRE(plain_text(lines[2]).empty());

    const std::string back = format_markup_lines(lines);
    REQUIRE(parse_markup_lines(back) == lines);
}

TEST_CASE("forced break line detection", "[model][rich_text]")
{
    REQUIRE(is_forced_break_line(parse_inline_markup(std::string(k_forced_split))));
    REQUIRE(is_forced_break_line(parse_inline_markup("  [---][---]  ")));
    REQUIRE_FALSE(is_forced_break_line(parse_inline_markup("not a break")));
}

TEST_CASE("strip_markup_tags removes tags and decodes entities", "[model][rich_text]")
{
    REQUIRE(strip_markup_tags("<b>Welcome</b>") == "Welcome");
    REQUIRE(strip_markup_tags("<size=80>a</size>\n<i>b</i>") == "a\nb");
    REQUIRE(strip_markup_tags("<img src=x>") == "<img src=x>");
    REQUIRE(strip_markup_tags("say &lt;b> &amp; more") == "say <b> & more");
    REQUIRE(strip_markup_tags("plain") == "plain");
}

TEST_CASE("reconcile_rich_line keeps untouched runs", "[model][rich_text]")
{
    const RichLine line{span("A "), span("Mighty", TextStyle{.bold = true}), span(" Fortress")};

    SECTION("unchanged text is returned as-is")
    {
        REQUIRE(reconcile_rich_line(line, "A Mighty Fortress") == line);
    }

    SECTION("a character typed inside a run inherits that run")
    {
        const RichLine out = reconcile_rich_line(line, "A Migxhty Fortress");
        REQUIRE(plain_text(out) == "A Migxhty Fortress");
        REQUIRE(out.size() == 3);
        REQUIRE(out[0].text == "A ");
        REQUIRE_FALSE(out[0].style.bold);
        REQUIRE(out[1].text == "Migxhty");
        REQUIRE(out[1].style.bold);
        REQUIRE(out[2].text == " Fortress");
        REQUIRE_FALSE(out[2].style.bold);
    }

    SECTION("typing over a selection takes the first replaced run's style")
    {
        const RichLine out = reconcile_rich_line(line, "A Big Fortress");
        REQUIRE(plain_text(out) == "A Big Fortress");
        REQUIRE(out.size() == 3);
        REQUIRE(out[1].text == "Big");
        REQUIRE(out[1].style.bold);
    }

    SECTION("appending inherits the trailing run")
    {
        const RichLine out = reconcile_rich_line(RichLine{span("Hymn", TextStyle{.italic = true})},
                                                  "Hymn #506");
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].text == "Hymn #506");
        REQUIRE(out[0].style.italic);
    }

    SECTION("deleting a whole run drops it and fuses the neighbours")
    {
        const RichLine out = reconcile_rich_line(line, "A  Fortress");
        REQUIRE(plain_text(out) == "A  Fortress");
        REQUIRE(out.size() == 1);
        REQUIRE_FALSE(out[0].style.bold);
    }

    SECTION("clearing the line keeps one run carrying its style")
    {
        const RichLine out = reconcile_rich_line(RichLine{span("x", TextStyle{.bold = true})}, "");
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].text.empty());
        REQUIRE(out[0].style.bold);
    }

    SECTION("multi-byte text is never cut mid-character")
    {
        const RichLine out =
            reconcile_rich_line(RichLine{span("Grüße", TextStyle{.bold = true})}, "Grüsse");
        REQUIRE(plain_text(out) == "Grüsse");
        for (const TextSpan& sp : out) {
            REQUIRE(sp.style.bold);
        }
    }
}

TEST_CASE("reconcile_rich_lines pairs lines from both ends", "[model][rich_text]")
{
    const std::vector<RichLine> lines{
        RichLine{span("A Mighty Fortress", TextStyle{.bold = true})},
        RichLine{span("Hymn #506")},
    };

    SECTION("unchanged text keeps both lines")
    {
        const auto out = reconcile_rich_lines(lines, "A Mighty Fortress\nHymn #506");
        REQUIRE(out == lines);
    }

    SECTION("inserting a line does not slide styling down")
    {
        const auto out = reconcile_rich_lines(lines, "A Mighty Fortress\nEin feste Burg\nHymn #506");
        REQUIRE(out.size() == 3);
        REQUIRE(plain_text(out[0]) == "A Mighty Fortress");
        REQUIRE(out[0][0].style.bold);
        REQUIRE(plain_text(out[1]) == "Ein feste Burg");
        REQUIRE(plain_text(out[2]) == "Hymn #506");
        REQUIRE_FALSE(out[2][0].style.bold);
    }

    SECTION("deleting a line does not slide styling up")
    {
        std::vector<RichLine> three = lines;
        three.insert(three.begin() + 1, RichLine{span("Ein feste Burg")});
        const auto out = reconcile_rich_lines(three, "A Mighty Fortress\nHymn #506");
        REQUIRE(out.size() == 2);
        REQUIRE(out[0][0].style.bold);
        REQUIRE_FALSE(out[1][0].style.bold);
    }

    SECTION("splitting a line keeps each half's styling")
    {
        const std::vector<RichLine> one{
            RichLine{span("A Mighty ", TextStyle{.bold = true}), span("Fortress")}};
        const auto out = reconcile_rich_lines(one, "A Mighty \nFortress");
        REQUIRE(out.size() == 2);
        REQUIRE(plain_text(out[0]) == "A Mighty ");
        REQUIRE(out[0][0].style.bold);
        REQUIRE(plain_text(out[1]) == "Fortress");
        REQUIRE_FALSE(out[1][0].style.bold);
    }

    SECTION("a new blank line inherits the line it grew out of")
    {
        const auto out = reconcile_rich_lines(lines, "A Mighty Fortress\n\nHymn #506");
        REQUIRE(out.size() == 3);
        REQUIRE(plain_text(out[1]).empty());
        REQUIRE(out[1][0].style.bold);
    }

    SECTION("empty text yields one empty line")
    {
        const auto out = reconcile_rich_lines(lines, "");
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].size() == 1);
        REQUIRE(out[0][0].text.empty());
    }

    SECTION("no previous lines yields plain lines")
    {
        const auto out = reconcile_rich_lines({}, "one\ntwo");
        REQUIRE(out.size() == 2);
        REQUIRE(plain_text(out[0]) == "one");
        REQUIRE(plain_text(out[1]) == "two");
    }
}

TEST_CASE("A gap survives markup, even a gap of nothing", "[model][rich_text]")
{
    using seventhslide::model::format_inline_markup;
    using seventhslide::model::parse_inline_markup;
    for (const int pct : {0, 40, 400}) {
        const std::string markup =
            "<gap=" + std::to_string(pct) + ">3:16</gap> For God";
        const auto line = parse_inline_markup(markup);
        REQUIRE(line.size() == 2);
        CHECK(line[0].text == "3:16");
        CHECK(line[0].style.margin_for_space);
        CHECK(line[0].style.margin_right_pct == pct);
        CHECK_FALSE(line[1].style.margin_for_space);
        CHECK(format_inline_markup(line) == markup);
        CHECK(seventhslide::model::plain_text(line) == "3:16 For God");
    }
    CHECK(parse_inline_markup("<gap=999>x</gap>")[0].style.margin_right_pct
          == seventhslide::model::k_max_gap_pct);
    // Nothing asked for, nothing written.
    CHECK_FALSE(parse_inline_markup("plain")[0].style.margin_for_space);
    CHECK(format_inline_markup(parse_inline_markup("plain")) == "plain");
}

TEST_CASE("A gap is a style that stored text keeps", "[model][rich_text]")
{
    const auto flags = seventhslide::model::format_editor_object(
        seventhslide::model::parse_markup_lines("<gap=40>3:16</gap> In"));
    CHECK(flags.mixed);
    CHECK(flags.text == "<gap=40>3:16</gap> In");
    // Held to what the tag can say.
    seventhslide::model::TextSpan span;
    span.text = "x";
    span.style.margin_right_pct = 1000;
    span.style.margin_for_space = true;
    CHECK(seventhslide::model::format_inline_markup({span}) == "<gap=400>x</gap>");
}
