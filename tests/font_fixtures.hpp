#pragma once

// The fonts in fixtures/fonts (see its README): by path, and as the only
// families a FontRegistry knows, so a test of faces and weights runs the same
// whatever the machine has installed; or the fonts the program ships.

#include <seventhslide/canvas/box.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/fonts/font_registry.hpp>
#include <seventhslide/util/strings.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace seventhslide::test {

/// A family with a real Bold.
inline constexpr std::string_view k_fixture_sans = "SSFixture Sans";
/// A family named for its weight, as "Open Sans Semibold" ships.
inline constexpr std::string_view k_fixture_semibold = "SSFixture Sans Semibold";
/// A family with nothing but a Regular: bold is synthesised.
inline constexpr std::string_view k_fixture_plain = "SSFixture Plain";
/// A family with a real Bold that looks like no other: the rest are
/// Liberation Sans, the app's stand-in for a font a library has not got.
inline constexpr std::string_view k_fixture_wide = "SSFixture Wide";

[[nodiscard]] inline std::filesystem::path fixture_font(std::string_view file)
{
    return std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "fonts" / file;
}

/// Make ``fonts`` resolve what the program ships, from the source tree: real
/// fonts, the same on every machine, with the app's stand-in.
inline void use_shipped_fonts(fonts::FontRegistry& fonts)
{
    fonts::FontIndex shipped;
    const auto root =
        std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR}.parent_path() / "third_party" / "fonts";
    for (const auto& family : std::filesystem::directory_iterator(root)) {
        if (!family.is_directory()) {
            continue;
        }
        auto& faces = shipped[util::to_lower(family.path().filename().string())];
        for (const auto& face : std::filesystem::directory_iterator(family.path())) {
            if (face.path().extension() == ".ttf" || face.path().extension() == ".otf") {
                const auto stem = face.path().stem().string();
                faces.push_back({.subfamily = stem.substr(stem.rfind('-') + 1), .path = face.path()});
            }
        }
    }
    fonts.set_library_fonts(std::move(shipped));
    fonts.set_stand_in_family(std::string(canvas::k_default_font_family));
}

/// ``db`` as a library from before the shipped fonts were copied in.
inline void forget_bundled_taken(db::Database& db)
{
    db.exec("DELETE FROM library_meta WHERE key = '" + std::string(db::k_bundled_fonts_key) + "'");
}

/// Make ``fonts`` resolve the fixture families and nothing else.
inline void use_fixture_fonts(fonts::FontRegistry& fonts)
{
    fonts::FontIndex index;
    index["ssfixture sans"] = {
        {.subfamily = "Regular", .path = fixture_font("SSFixtureSans-Regular.ttf")},
        {.subfamily = "Bold", .path = fixture_font("SSFixtureSans-Bold.ttf")},
    };
    index["ssfixture sans semibold"] = {
        {.subfamily = "Regular", .path = fixture_font("SSFixtureSansSemibold-Regular.ttf")},
    };
    index["ssfixture plain"] = {
        {.subfamily = "Regular", .path = fixture_font("SSFixturePlain-Regular.ttf")},
    };
    fonts.set_library_fonts(std::move(index));
}

}  // namespace seventhslide::test
