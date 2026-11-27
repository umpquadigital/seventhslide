#pragma once

// OpenLyrics song XML import and export. Every other song format, and
// telling them apart, is song_import.hpp.
// Seam: plain C++20 + pugixml (no Qt). Reuses ``model::normalize_imported_markup``.

#include "seventhslide/model/content.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::parse {

/// Thrown when XML cannot be parsed or contains a forbidden DOCTYPE/DTD.
class SongXmlError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Parse an OpenLyrics (or OpenSong-shaped OpenLyrics) XML file into Song DTOs.
/// Does not touch the database; callers insert via ``SongRepository``.
[[nodiscard]] std::vector<model::Song> parse_song_file(
    const std::filesystem::path& path);

/// Same as ``parse_song_file`` but from an in-memory UTF-8 XML document.
/// ``fallback_title`` is used when the root is not a ``song`` and no ``<title>``
/// is found.
[[nodiscard]] std::vector<model::Song> parse_song_xml(
    std::string_view xml,
    std::string_view fallback_title = "Untitled");

/**
 * ``song`` as an OpenLyrics 0.8 document (UTF-8, one song), which
 * ``parse_song_xml`` reads back as the same song but for what only this
 * library knows (its id, its themes, whether it shows its copyright).
 *
 * Verses are written in the order the lyrics hold them; a verse split by
 * forced breaks is written as lettered parts (``v1a``, ``v1b``), which the
 * reader joins again. Chords are ``<chord>`` elements, and formatting is
 * ``<tag>`` elements the file's ``<format>`` defines in this app's markup, so
 * other programs keep the words and what formatting they know. Theme chips
 * are left out of the verse order: the themes they name stay in the library.
 *
 * The file passes OpenLyrics 0.8's schema, so what it cannot say is said
 * plainly: an author credited for neither words, music nor a translation
 * into a language is credited without a role; a CCLI number that is not
 * digits is a comment ("CCLI: ..."); a songbook without a name is left out.
 * ``created_in`` names the program in ``createdIn`` and ``modifiedIn``
 * ("SeventhSlide" when empty), and ``modified_date`` (ISO 8601) is
 * ``modifiedDate``, now when empty.
 */
[[nodiscard]] std::string write_song_xml(const model::Song& song,
                                         std::string_view created_in = {},
                                         std::string_view modified_date = {});

}  // namespace seventhslide::parse
