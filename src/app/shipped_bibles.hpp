#pragma once

// The Bibles SeventhSlide ships, which a library takes a copy of once
// (LibraryController::take_shipped_bibles), as it does the shipped fonts.

#include <seventhslide/parse/bible.hpp>

#include <QString>

#include <string>
#include <vector>

namespace seventhslide::app {

/// Where the program has them (third_party/bibles): an OSIS file each, named
/// for the abbreviation a library gives it ("KJV.xml"). Only the program itself
/// has them, not the library its tests link, so a test's library takes none.
inline constexpr auto k_shipped_bibles_dir = ":/bundled_bibles";

struct ShippedBible {
    std::string abbreviation;
    parse::ParsedBible bible;
};

/// The abbreviations of the Bibles in ``dir``, by name.
[[nodiscard]] std::vector<std::string> shipped_bible_abbreviations(const QString& dir);

/// Read the Bibles in ``dir``, by name. One that does not read, or has no verse,
/// is left out with a warning. From any thread.
[[nodiscard]] std::vector<ShippedBible> read_shipped_bibles(const QString& dir);

}  // namespace seventhslide::app
