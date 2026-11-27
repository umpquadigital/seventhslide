#pragma once

// The library's fonts (``font_files``): one row per face, each a file in the
// media folder's ``fonts`` under a name made of its content, so the same bytes
// are one record on every computer, however often and wherever they are added.
//
// A face has a slot: its family (case aside), bold or not, italic or not. Text
// asks for a slot. A library normally holds one face per slot, but two
// computers adding different versions apart hold two once they sync, as rows
// are never merged. So which face a slot shows is decided here, by what the
// rows hold, and is the same on every computer: the one added last.
//
// Seam: Qt SQL via Database. Disk unlink is the caller's job (``FontStore``).

#include "seventhslide/db/database.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::db {

/// A row of ``font_files``.
struct LibraryFont {
    std::string filename;
    /// "<family> <style>": what the sync's messages call the file.
    std::string display_name;
    std::string family;
    std::string style;
    bool bold = false;
    bool italic = false;
    /// The file holds other weights or styles along an axis.
    bool variable = false;
    /// Milliseconds since the epoch, on the computer that added it.
    std::int64_t added_at = 0;

    bool operator==(const LibraryFont&) const = default;
};

/// Something in the library that names a family: where Settings says a font is
/// used, and what it takes to open it.
struct FontUse {
    enum class Kind { Theme, Output, Profile, Song, Freeform, ServiceItem };
    Kind kind = Kind::Theme;
    /// What the library calls it: the theme's, output's, profile's or song's
    /// name, the Freeform item's, the service row's title.
    std::string name;
    /// What holds it: a theme's output, by name; a service row's service.
    /// Empty for anything else.
    std::string within;
    /// Its id: a theme's own, or the row's (an output's is in its data).
    std::string id;
    /// A theme's kind (``song``, ``bible``, ``media`` or ``bg``: the kind
    /// ``model::k_output_theme_lists`` names); a service row's item type.
    std::string type;

    bool operator==(const FontUse&) const = default;
};

class FontRepository {
public:
    explicit FontRepository(Database& db) : db_(db) {}

    /// Register ``font``, or replace what is registered under its filename.
    void add(const LibraryFont& font);
    /// Hard-delete the row. Returns true if a row was removed.
    bool remove(std::string_view filename);
    [[nodiscard]] std::optional<LibraryFont> get(std::string_view filename);
    /// Every row: by family (case aside), then slot, the face a slot shows
    /// first.
    [[nodiscard]] std::vector<LibraryFont> list_all();
    /// The faces text can show, one per slot, in ``list_all``'s order.
    [[nodiscard]] std::vector<LibraryFont> list_faces();
    /// Every row in ``family`` (case aside), in ``list_all``'s order.
    [[nodiscard]] std::vector<LibraryFont> list_family(std::string_view family);
    /// Every family the library's themes, Freeform items, services and songs
    /// name, whether it has a font for them or not. Reads them all, so not for
    /// a hot path.
    [[nodiscard]] std::set<std::string> families_named();
    /// What names ``family`` (case aside), as ``families_named`` reads it:
    /// the outputs' themes, then the outputs themselves (what they name outside
    /// a theme), profiles, songs, Freeform items and service rows, each once, in
    /// the order the library lists them. Reads them all, so not for a hot path.
    [[nodiscard]] std::vector<FontUse> uses_of(std::string_view family);
    /// The library has had its copy of the shipped fonts.
    [[nodiscard]] bool bundled_taken();
    void note_bundled_taken();

private:
    Database& db_;
};

/// The families ``document`` (a Freeform item's, a theme's) names, as
/// ``FontRepository::families_named`` reads them.
[[nodiscard]] std::set<std::string> font_families_named_in(const nlohmann::json& document);

}  // namespace seventhslide::db
