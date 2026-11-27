#include <catch2/catch_test_macros.hpp>

#include <seventhslide/util/strings.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace util = seventhslide::util;

TEST_CASE("util::trim strips ASCII whitespace only", "[util][strings]")
{
    REQUIRE(util::trim_view(" \t\r\n\f\vabc \n") == "abc");
    REQUIRE(util::trim_view("   ").empty());
    REQUIRE(util::trim_view("").empty());
    REQUIRE(util::trim("a b") == "a b");
    // A non-breaking space (U+00A0) is text, not padding.
    REQUIRE(util::trim("\xC2\xA0x") == "\xC2\xA0x");
}

TEST_CASE("util case folding leaves non-ASCII bytes untouched", "[util][strings]")
{
    REQUIRE(util::to_lower("Pre-Chorus 1A") == "pre-chorus 1a");
    REQUIRE(util::to_upper("v1a") == "V1A");
    const std::string accented = "\xC3\x89glise";  // "Église"
    REQUIRE(util::to_lower(accented) == "\xC3\x89glise");
    REQUIRE(util::to_upper(accented) == "\xC3\x89GLISE");
}

TEST_CASE("util::iequals and contains_ci", "[util][strings]")
{
    REQUIRE(util::iequals("CHORD", "chord"));
    REQUIRE_FALSE(util::iequals("chord", "chords"));
    REQUIRE(util::iequals("", ""));
    REQUIRE(util::contains_ci("SemiBold Italic", "bold"));
    REQUIRE(util::contains_ci("anything", ""));
    REQUIRE_FALSE(util::contains_ci("Bol", "bold"));
}

TEST_CASE("util::split_ws drops empty pieces", "[util][strings]")
{
    REQUIRE(util::split_ws("  v1 c1\t\nb1  ") == std::vector<std::string_view>{"v1", "c1", "b1"});
    REQUIRE(util::split_ws(" \t ").empty());
    REQUIRE(util::split_words("a  b") == std::vector<std::string>{"a", "b"});
}

TEST_CASE("util::utf8_length counts code points", "[util][strings]")
{
    REQUIRE(util::utf8_length("") == 0);
    REQUIRE(util::utf8_length("abc") == 3);
    REQUIRE(util::utf8_length("\xC3\xA9t\xC3\xA9") == 3);   // "été"
    REQUIRE(util::utf8_length("\xF0\x9F\x8E\xB5") == 1);    // one emoji
}

TEST_CASE("util::append_utf8 writes a code point, and U+FFFD for none", "[util][strings]")
{
    const auto utf8 = [](char32_t cp) {
        std::string out = "x";
        util::append_utf8(out, cp);
        return out;
    };
    REQUIRE(utf8(U'a') == "xa");
    REQUIRE(utf8(0xE9) == "x\xC3\xA9");
    REQUIRE(utf8(0x2019) == "x\xE2\x80\x99");
    REQUIRE(utf8(0x1F3B5) == "x\xF0\x9F\x8E\xB5");
    REQUIRE(utf8(0xD800) == "x\xEF\xBF\xBD");
    REQUIRE(utf8(0x110000) == "x\xEF\xBF\xBD");
}

TEST_CASE("util::utf8_sequence_length reads a lead byte", "[util][strings]")
{
    REQUIRE(util::utf8_sequence_length('a') == 1);
    REQUIRE(util::utf8_sequence_length(0xC3) == 2);
    REQUIRE(util::utf8_sequence_length(0xE2) == 3);
    REQUIRE(util::utf8_sequence_length(0xF0) == 4);
    REQUIRE(util::utf8_sequence_length(0x80) == 1);  // a continuation byte
}

TEST_CASE("util::starts_with_ci", "[util][strings]")
{
    REQUIRE(util::starts_with_ci("RGBA(1,2,3)", "rgba("));
    REQUIRE(util::starts_with_ci("<Size=12>", "<size="));
    REQUIRE_FALSE(util::starts_with_ci("rgb", "rgba"));
    REQUIRE(util::starts_with_ci("anything", ""));
}

TEST_CASE("util::next_brace_span finds clean spans only", "[util][strings]")
{
    const std::string_view s = "a {x} {y {z} {\n} {w";
    auto span = util::next_brace_span(s, 0);
    REQUIRE(span);
    REQUIRE(span->inner(s) == "x");
    // "{y {z}": the first brace does not close cleanly; the scan resumes at "{z}".
    span = util::next_brace_span(s, span->end);
    REQUIRE(span);
    REQUIRE(span->inner(s) == "z");
    // "{\n}" spans a newline and "{w" never closes.
    REQUIRE_FALSE(util::next_brace_span(s, span->end));
    REQUIRE(util::next_brace_span("{}", 0)->inner("{}").empty());
}

TEST_CASE("util::parse_leading_int reads like stoi but never throws", "[util][strings]")
{
    REQUIRE(util::parse_leading_int("42") == 42);
    REQUIRE(util::parse_leading_int("  -7px") == -7);
    REQUIRE(util::parse_leading_int("+3") == 3);
    REQUIRE(util::parse_leading_int("007") == 7);
    REQUIRE(util::parse_leading_int("2147483647") == 2147483647);
    REQUIRE(util::parse_leading_int("-2147483648") == -2147483647 - 1);

    REQUIRE_FALSE(util::parse_leading_int(""));
    REQUIRE_FALSE(util::parse_leading_int("-"));
    REQUIRE_FALSE(util::parse_leading_int("bold"));
    REQUIRE_FALSE(util::parse_leading_int("2147483648"));
    REQUIRE_FALSE(util::parse_leading_int("-2147483649"));
    REQUIRE_FALSE(util::parse_leading_int("99999999999999999999999"));
}

TEST_CASE("xml_escape escapes markup, and quotes only for attributes", "[util][strings]")
{
    REQUIRE(util::xml_escape("a < b && c > \"d\" 'e'") == "a &lt; b &amp;&amp; c &gt; \"d\" 'e'");
    REQUIRE(util::xml_escape("say \"hi\" & 'bye'", true)
            == "say &quot;hi&quot; &amp; &apos;bye&apos;");
    REQUIRE(util::xml_escape("Café", true) == "Café");
}
