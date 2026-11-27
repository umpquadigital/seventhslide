// What a font file is, read from its bytes: faces, names and refusals. The
// bytes come from anywhere, so damaged ones have to be read safely too.

#include <catch2/catch_test_macros.hpp>

#include "font_fixtures.hpp"

#include <seventhslide/fonts/font_file.hpp>
#include <seventhslide/util/read_file.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using seventhslide::fonts::read_font_file;
using seventhslide::test::fixture_font;

namespace {

std::string fixture(std::string_view file)
{
    auto bytes = seventhslide::util::read_file(fixture_font(file));
    REQUIRE(bytes.has_value());
    return *bytes;
}

std::uint32_t u32_at(std::string_view d, std::size_t at)
{
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        v = (v << 8U) | static_cast<unsigned char>(d[at + i]);
    }
    return v;
}

std::uint16_t u16_at(std::string_view d, std::size_t at)
{
    return static_cast<std::uint16_t>((static_cast<unsigned char>(d[at]) << 8)
                                      | static_cast<unsigned char>(d[at + 1]));
}

/// ``font`` with its ``tag`` table no longer called that.
std::string without_table(std::string font, std::string_view tag)
{
    const std::size_t count = u16_at(font, 4);
    for (std::size_t i = 0; i < count; ++i) {
        const auto record = 12 + i * 16;
        if (std::string_view(font).substr(record, 4) == tag) {
            font[record] = '!';
            return font;
        }
    }
    FAIL("no such table: " << tag);
    return font;
}

void put_u16(std::string& out, std::uint16_t value)
{
    out.push_back(static_cast<char>(value >> 8));
    out.push_back(static_cast<char>(value & 0xff));
}

std::string utf16be(std::string_view ascii)
{
    std::string out;
    for (const char c : ascii) {
        out.push_back('\0');
        out.push_back(c);
    }
    return out;
}

struct NameRecord {
    std::uint16_t platform;
    std::uint16_t encoding;
    std::uint16_t language;
    std::uint16_t id;
    std::string text;
};

std::string name_table(const std::vector<NameRecord>& records)
{
    std::string strings;
    std::string name;
    put_u16(name, 0);
    put_u16(name, static_cast<std::uint16_t>(records.size()));
    put_u16(name, static_cast<std::uint16_t>(6 + records.size() * 12));
    for (const auto& r : records) {
        put_u16(name, r.platform);
        put_u16(name, r.encoding);
        put_u16(name, r.language);
        put_u16(name, r.id);
        put_u16(name, static_cast<std::uint16_t>(r.text.size()));
        put_u16(name, static_cast<std::uint16_t>(strings.size()));
        strings += r.text;
    }
    return name + strings;
}

void put_u32(std::string& out, std::uint32_t value)
{
    put_u16(out, static_cast<std::uint16_t>(value >> 16));
    put_u16(out, static_cast<std::uint16_t>(value & 0xffff));
}

void set_u32(std::string& out, std::size_t at, std::uint32_t value)
{
    std::string bytes;
    put_u32(bytes, value);
    out.replace(at, 4, bytes);
}

/// Where ``tag``'s record is in the directory of the font at ``base``.
std::size_t record_of(std::string_view font, std::string_view tag, std::size_t base = 0)
{
    const std::size_t count = u16_at(font, base + 4);
    for (std::size_t i = 0; i < count; ++i) {
        const auto record = base + 12 + i * 16;
        if (font.substr(record, 4) == tag) {
            return record;
        }
    }
    FAIL("no such table: " << tag);
    return 0;
}

constexpr std::uint16_t k_windows = 3;
constexpr std::uint16_t k_mac = 1;
constexpr std::uint16_t k_us_english = 0x0409;

/// ``font`` with ``records`` for its names, the new table at the end.
std::string with_names(std::string font, const std::vector<NameRecord>& records)
{
    const auto name = name_table(records);
    font.resize((font.size() + 3) / 4 * 4, '\0');
    const auto record = record_of(font, "name");
    set_u32(font, record + 8, static_cast<std::uint32_t>(font.size()));
    set_u32(font, record + 12, static_cast<std::uint32_t>(name.size()));
    return font + name;
}

/// ``font`` called ``family`` and ``style``.
std::string renamed(std::string font, const std::string& family, const std::string& style)
{
    return with_names(std::move(font), {{k_windows, 1, k_us_english, 1, utf16be(family)},
                                        {k_windows, 1, k_us_english, 2, utf16be(style)}});
}

/// A collection of ``fonts``, each with its directory and tables as it has them.
std::string collection_of(const std::vector<std::string>& fonts)
{
    std::string file = "ttcf";
    put_u32(file, 0x00010000);
    put_u32(file, static_cast<std::uint32_t>(fonts.size()));
    auto at = file.size() + fonts.size() * 4;
    for (const auto& font : fonts) {
        put_u32(file, static_cast<std::uint32_t>(at));
        at += (font.size() + 3) / 4 * 4;
    }
    for (auto font : fonts) {
        const auto base = file.size();
        const std::size_t count = u16_at(font, 4);
        for (std::size_t i = 0; i < count; ++i) {
            const auto record = 12 + i * 16;
            set_u32(font, record + 8, u32_at(font, record + 8) + static_cast<std::uint32_t>(base));
        }
        font.resize((font.size() + 3) / 4 * 4, '\0');
        file += font;
    }
    return file;
}

/// The sum a font file's 32-bit words come to, the last padded with zeros.
std::uint32_t file_sum(std::string bytes)
{
    bytes.resize((bytes.size() + 3) / 4 * 4, '\0');
    std::uint32_t sum = 0;
    for (std::size_t at = 0; at < bytes.size(); at += 4) {
        sum += u32_at(bytes, at);
    }
    return sum;
}

}  // namespace

TEST_CASE("A font file says what its one face is", "[fonts]")
{
    const auto bytes = fixture("SSFixtureSans-Regular.ttf");
    const auto read = read_font_file(bytes);
    CHECK(read.refusal.empty());
    REQUIRE(read.faces.size() == 1);
    const auto& face = read.faces.front();
    CHECK(face.family == "SSFixture Sans");
    CHECK(face.style == "Regular");
    CHECK_FALSE(face.bold);
    CHECK_FALSE(face.italic);
    CHECK_FALSE(face.variable);
    CHECK(face.extension == ".ttf");
    CHECK(face.bytes == bytes);
}

TEST_CASE("A bold face is bold, and a family named for its weight keeps its name", "[fonts]")
{
    const auto bold = read_font_file(fixture("SSFixtureSans-Bold.ttf"));
    REQUIRE(bold.faces.size() == 1);
    CHECK(bold.faces.front().family == "SSFixture Sans");
    CHECK(bold.faces.front().style == "Bold");
    CHECK(bold.faces.front().bold);

    // Its typographic names say "SSFixture Sans" and "Semibold", which a theme
    // could not pick.
    const auto semibold = read_font_file(fixture("SSFixtureSansSemibold-Regular.ttf"));
    REQUIRE(semibold.faces.size() == 1);
    CHECK(semibold.faces.front().family == "SSFixture Sans Semibold");
    CHECK(semibold.faces.front().style == "Regular");
    CHECK_FALSE(semibold.faces.front().bold);
}

TEST_CASE("A collection is a file per face, a weight named as a style its own family", "[fonts]")
{
    const auto read = read_font_file(fixture("SSFixtureCollection.ttc"));
    CHECK(read.refusal.empty());
    REQUIRE(read.faces.size() == 3);
    CHECK(read.faces[0].family == "SSFixture Collection");
    CHECK(read.faces[0].style == "Regular");
    CHECK_FALSE(read.faces[0].bold);
    CHECK(read.faces[1].family == "SSFixture Collection Light");
    CHECK(read.faces[1].style == "Regular");
    CHECK_FALSE(read.faces[1].bold);
    CHECK(read.faces[2].family == "SSFixture Collection");
    CHECK(read.faces[2].style == "Bold");
    CHECK(read.faces[2].bold);
    for (const auto& face : read.faces) {
        // Each is a font file in its own right, and reads as the same face.
        const auto alone = read_font_file(face.bytes);
        CHECK(alone.refusal.empty());
        REQUIRE(alone.faces.size() == 1);
        CHECK(alone.faces.front().bold == face.bold);
        CHECK(alone.faces.front().bytes == face.bytes);
        // The whole file's checksum is as the format has it.
        CHECK(file_sum(face.bytes) == 0xB1B0AFBA);
    }
}

TEST_CASE("What is not a usable font is refused, in words for people", "[fonts]")
{
    const auto font = fixture("SSFixtureSans-Regular.ttf");
    CHECK_FALSE(read_font_file("").refusal.empty());
    CHECK(read_font_file("Dear friends").refusal == "This is not a font file.");
    CHECK(read_font_file("wOF2 and the rest of it").refusal.find("web pages") != std::string::npos);
    CHECK(read_font_file("ttcf").refusal == "This font file is damaged.");
    CHECK(read_font_file(font.substr(0, 40)).refusal == "This font file is damaged.");

    for (const auto tag : {"head", "maxp", "hhea", "hmtx", "cmap"}) {
        INFO(tag);
        CHECK(read_font_file(without_table(font, tag)).refusal == "This font file is damaged.");
    }
    CHECK(read_font_file(without_table(font, "glyf")).refusal.find("no outlines")
          != std::string::npos);
    CHECK(read_font_file(without_table(font, "name")).refusal.find("what the font is called")
          != std::string::npos);
}

TEST_CASE("Damaged font files are read without reading past them", "[fonts]")
{
    for (const auto* file : {"SSFixtureSans-Regular.ttf", "SSFixtureCollection.ttc"}) {
        const auto font = fixture(file);
        const auto settled = [](const seventhslide::fonts::FontFileRead& read) {
            return read.faces.empty() != read.refusal.empty();
        };
        // Cut short at every length through the tables' directory, then by steps.
        for (std::size_t size = 0; size < font.size(); size += size < 512 ? 1 : 997) {
            CHECK(settled(read_font_file(std::string_view(font).substr(0, size))));
        }
        // Bytes changed, the directories above all, by a fixed sequence.
        std::uint32_t random = 12345;
        for (int round = 0; round < 3000; ++round) {
            auto damaged = font;
            for (int change = 0; change < 8; ++change) {
                random = random * 1103515245U + 12345U;
                const auto span = round % 2 == 0 ? std::min<std::size_t>(font.size(), 512) : font.size();
                damaged[(random >> 8U) % span] = static_cast<char>(random >> 24U);
            }
            const auto read = read_font_file(damaged);
            CHECK(settled(read));
        }
    }
}

TEST_CASE("A font's names are its English ones, wherever they are in the table", "[fonts]")
{
    constexpr std::uint16_t k_french = 0x040C;
    constexpr std::uint16_t k_british = 0x0809;
    constexpr std::uint16_t k_chinese = 0x0804;
    const auto font = fixture("SSFixtureSans-Regular.ttf");
    const auto family_of = [&](const std::vector<NameRecord>& records) {
        const auto read = read_font_file(with_names(font, records));
        REQUIRE(read.faces.size() == 1);
        return read.faces.front().family;
    };
    CHECK(family_of({
              {k_mac, 0, 0, 1, "Mac Name"},
              {k_windows, 1, k_us_english, 1, utf16be("English Name")},
              {k_windows, 1, k_french, 1, utf16be("Nom Francais")},
          })
          == "English Name");
    // English from anywhere comes before any other language.
    CHECK(family_of({
              {k_windows, 1, k_chinese, 1, utf16be("Chinese Name")},
              {k_mac, 0, 7, 1, "Mac Other"},
              {k_mac, 0, 0, 1, "Mac English"},
          })
          == "Mac English");
    CHECK(family_of({
              {k_windows, 1, k_french, 1, utf16be("Nom Francais")},
              {k_windows, 1, k_british, 1, utf16be("British Name")},
          })
          == "British Name");
    // Where there is nothing else.
    CHECK(family_of({{k_mac, 0, 0, 1, "Mac Name"}}) == "Mac Name");
}

TEST_CASE("A style says bold and italic however it is written, and the family the rest",
          "[fonts]")
{
    const auto font = fixture("SSFixtureSans-Regular.ttf");
    struct Case {
        const char* family;
        const char* style;
        const char* read_family;
        const char* read_style;
        bool bold;
        bool italic;
    };
    for (const auto& c : std::vector<Case>{
             {"Foo", "Regular", "Foo", "Regular", false, false},
             {"Foo", "Book", "Foo", "Book", false, false},
             {"Foo", "Bold", "Foo", "Bold", true, false},
             {"Foo", "BoldItalic", "Foo", "BoldItalic", true, true},
             {"Foo", "Bold-Oblique", "Foo", "Bold-Oblique", true, true},
             {"Foo", "Light", "Foo Light", "Regular", false, false},
             {"Foo", "Light Oblique", "Foo Light", "Italic", false, true},
             // A weight the family already says is not said twice.
             {"Foo Light", "Light", "Foo Light", "Regular", false, false},
             // Not bold, but a weight of its own, however it is spaced.
             {"Foo", "SemiBold", "Foo SemiBold", "Regular", false, false},
             {"Foo", "Semi Bold", "Foo SemiBold", "Regular", false, false},
             {"Foo", "ExtraBold Italic", "Foo ExtraBold", "Italic", false, true},
         }) {
        INFO(c.family << " / " << c.style);
        const auto read = read_font_file(renamed(font, c.family, c.style));
        CHECK(read.refusal.empty());
        REQUIRE(read.faces.size() == 1);
        CHECK(read.faces.front().family == c.read_family);
        CHECK(read.faces.front().style == c.read_style);
        CHECK(read.faces.front().bold == c.bold);
        CHECK(read.faces.front().italic == c.italic);
    }

    // The flags in the file say bold where the name does not: bold, and named so.
    const auto bold = read_font_file(renamed(fixture("SSFixtureSans-Bold.ttf"), "Foo", "Regular"));
    REQUIRE(bold.faces.size() == 1);
    CHECK(bold.faces.front().bold);
    CHECK(bold.faces.front().style == "Bold");
}

TEST_CASE("No two faces of a collection share a slot", "[fonts]")
{
    const auto font = fixture("SSFixtureSans-Regular.ttf");
    // Both regular: the second goes by its word, and neither is lost.
    const auto read = read_font_file(
        collection_of({renamed(font, "Avenir", "Book"), renamed(font, "Avenir", "Roman")}));
    CHECK(read.refusal.empty());
    REQUIRE(read.faces.size() == 2);
    CHECK(read.faces[0].family == "Avenir");
    CHECK(read.faces[0].style == "Book");
    CHECK(read.faces[1].family == "Avenir Roman");
    CHECK(read.faces[1].style == "Regular");

    // The same one twice, with no word to tell them apart even so.
    const auto twice = read_font_file(collection_of(
        {renamed(font, "Avenir", "Regular"), renamed(font, "Avenir", "Regular"),
         renamed(font, "Avenir", "Regular")}));
    CHECK(twice.refusal == "This font collection has the same font in it twice.");
    CHECK(twice.faces.empty());
}

TEST_CASE("A collection pointing many tables at one is refused before it is copied", "[fonts]")
{
    // A small file that would be gigabytes if each face were made a file: a
    // directory per face, of many tags each saying the whole of one large table.
    const auto font = fixture("SSFixtureSans-Regular.ttf");
    const std::size_t own_tables = u16_at(font, 4);
    const std::uint32_t table_bytes = 1024 * 1024;
    const auto crafted = [&](std::size_t faces, std::size_t tags) {
        const auto directory_bytes = 12 + (own_tables + tags) * 16;
        const auto header_bytes = 12 + faces * 4;
        const auto body = static_cast<std::uint32_t>(header_bytes + faces * directory_bytes);
        std::string file = "ttcf";
        put_u32(file, 0x00010000);
        put_u32(file, static_cast<std::uint32_t>(faces));
        for (std::size_t f = 0; f < faces; ++f) {
            put_u32(file, static_cast<std::uint32_t>(header_bytes + f * directory_bytes));
        }
        for (std::size_t f = 0; f < faces; ++f) {
            file += std::string("\0\1\0\0", 4);
            put_u16(file, static_cast<std::uint16_t>(own_tables + tags));
            file.append(6, '\0');
            for (std::size_t i = 0; i < own_tables; ++i) {
                auto record = font.substr(12 + i * 16, 16);
                set_u32(record, 8, u32_at(record, 8) + body);
                file += record;
            }
            for (std::size_t i = 0; i < tags; ++i) {
                put_u32(file, 0x41000000U + static_cast<std::uint32_t>(i));
                put_u32(file, 0);
                put_u32(file, body);
                put_u32(file, table_bytes);
            }
        }
        file += font;
        file.resize(body + table_bytes, '\0');
        return file;
    };
    // One face larger than any font file.
    CHECK(read_font_file(crafted(1, 4000)).refusal == "This font file is damaged.");
    // Each face possible, all of them together not.
    CHECK(read_font_file(crafted(6, 200)).refusal == "This font collection is too large.");
}
