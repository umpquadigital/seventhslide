// Which file a family names: the library's, then the stand-in, and nothing the
// system has.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/fonts/font_registry.hpp>

#include <string>
#include <utility>
#include <vector>

using seventhslide::fonts::classify_subfamily;
using seventhslide::fonts::FontIndex;
using seventhslide::fonts::FontRegistry;
using seventhslide::fonts::FontSource;
using seventhslide::fonts::index_match;

namespace {

FontIndex index_of(const std::string& family, const std::string& where)
{
    FontIndex index;
    index[family] = {{.subfamily = "Regular", .path = where + "-regular.ttf"},
                     {.subfamily = "Bold", .path = where + "-bold.ttf"},
                     {.subfamily = "Italic", .path = where + "-italic.ttf"},
                     {.subfamily = "Bold Italic", .path = where + "-bolditalic.ttf"}};
    return index;
}

struct Told {
    std::vector<std::string> asked;

    void listen(FontRegistry& fonts)
    {
        fonts.set_listener([this](const std::string& family, std::uint64_t) { asked.push_back(family); });
    }
};

}  // namespace

TEST_CASE("A face's subfamily says bold and italic, and a slot takes the nearest", "[fonts]")
{
    CHECK(classify_subfamily("Regular") == std::pair{false, false});
    CHECK(classify_subfamily("Bold Italic") == std::pair{true, true});
    CHECK(classify_subfamily("BoldItalic") == std::pair{true, true});
    CHECK(classify_subfamily("Oblique") == std::pair{false, true});

    const auto index = index_of("lato", "lato");
    CHECK(index_match(index, "LATO", true, false)->path == "lato-bold.ttf");
    CHECK(index_match(index, "Lato", true, true)->path == "lato-bolditalic.ttf");
    CHECK(index_match(index, "Other", false, false) == nullptr);
    FontIndex plain;
    plain["plain"] = {{.subfamily = "Regular", .path = "plain.ttf"}};
    CHECK(index_match(plain, "plain", true, true)->path == "plain.ttf");
}

TEST_CASE("A family shipped but not in the library is missing from it", "[fonts]")
{
    FontRegistry fonts;
    Told told;
    told.listen(fonts);
    fonts.set_shipped_fonts([] {
        auto shipped = index_of("lato", "lato");
        shipped.merge(index_of("liberation sans", "stand-in"));
        return shipped;
    }());
    fonts.set_stand_in_family("Liberation Sans");

    const auto shown = fonts.find("Lato", false, false);
    REQUIRE(shown.has_value());
    CHECK(shown->path == "stand-in-regular.ttf");
    CHECK(shown->source == FontSource::StandIn);
    CHECK(told.asked == std::vector<std::string>{"Lato"});

    told.asked.clear();
    fonts.set_library_fonts(index_of("lato", "library"));
    const auto own = fonts.find("Lato", true, false);
    REQUIRE(own.has_value());
    CHECK(own->path == "library-bold.ttf");
    CHECK(own->source == FontSource::Library);
    CHECK(told.asked.empty());
}

TEST_CASE("A family nobody has is shown in the stand-in, and said so", "[fonts]")
{
    FontRegistry fonts;
    Told told;
    told.listen(fonts);
    CHECK_FALSE(fonts.find("Gotham", false, false).has_value());  // no fonts at all

    told.asked.clear();
    fonts.set_shipped_fonts(index_of("liberation sans", "stand-in"));
    fonts.set_stand_in_family("Liberation Sans");
    const auto stand_in = fonts.find("Gotham", true, true);
    REQUIRE(stand_in.has_value());
    CHECK(stand_in->path == "stand-in-bolditalic.ttf");
    CHECK(stand_in->source == FontSource::StandIn);
    CHECK(told.asked == std::vector<std::string>{"Gotham"});

    // No family at all is the stand-in's, and nothing to tell.
    told.asked.clear();
    const auto none = fonts.find("", false, false);
    REQUIRE(none.has_value());
    CHECK(none->path == "stand-in-regular.ttf");
    CHECK(told.asked.empty());

    // One whose file would not load goes the same way.
    fonts.set_library_fonts(index_of("gotham", "broken"));
    const auto own = fonts.find("Gotham", false, false);
    REQUIRE(own.has_value());
    CHECK(own->source == FontSource::Library);
    told.asked.clear();
    const auto instead = fonts.stand_in_for("Gotham", false, false);
    REQUIRE(instead.has_value());
    CHECK(instead->path == "stand-in-regular.ttf");
    CHECK(told.asked == std::vector<std::string>{"Gotham"});

    // The library's copy of the stand-in's own family may not be here yet.
    fonts.set_library_fonts(index_of("liberation sans", "library"));
    CHECK(fonts.find("Gotham", false, false)->path == "stand-in-regular.ttf");
}
