#pragma once

// What a font file holds, read from its bytes alone, whatever it is called:
// whether a library can take it, and what each face in it is. The file comes
// from anywhere, so nothing in it is trusted and every read is checked against
// the bytes there are.
//
// A library's fonts follow the app's text styles: a family and bold or italic
// on top. So a face's family and style are the legacy names (name IDs 1 and
// 2), which group faces that way, rather than the typographic ones: "Open Sans
// Semibold" is a family a theme can pick, where "Open Sans" with a style
// "Semibold" could not be reached. A file naming a weight as its style all the
// same, as a Mac collection's "Helvetica" and "Light" do, is read the same way:
// family "Helvetica Light", style "Regular".
//
// Seam: plain C++20 (no Qt).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::fonts {

/// Largest font file read: the biggest CJK collections, with room. No face of a
/// collection is larger either.
inline constexpr std::int64_t k_max_font_file_bytes = std::int64_t{256} * 1024 * 1024;
/// The most a collection's faces come to as files of their own: they share
/// tables in the collection, and a crafted one could share one table many times.
inline constexpr std::int64_t k_max_collection_face_bytes = std::int64_t{1024} * 1024 * 1024;

/// One face, as a file of its own.
struct FontFileFace {
    /// The face's bytes: the file's own, or for a face of a collection a font
    /// file made of its tables.
    std::string bytes;
    /// ".ttf" or ".otf", by what the bytes hold.
    std::string_view extension;
    std::string family;
    std::string style;
    bool bold = false;
    bool italic = false;
    /// It has a weight axis or others, so heavier faces may be in this file.
    bool variable = false;
};

/// What ``read_font_file`` found: the faces, or why there are none.
struct FontFileRead {
    std::vector<FontFileFace> faces;
    /// In words for people; empty when the file is usable.
    std::string refusal;
};

/// The faces of a font file (TrueType, OpenType, or a collection of either),
/// no two in the same family and bold and italic. A collection with one bad
/// face is refused whole. Throws only when memory runs out.
[[nodiscard]] FontFileRead read_font_file(std::string_view bytes);

}  // namespace seventhslide::fonts
