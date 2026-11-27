#pragma once

// Each song format's reader, for song_import.cpp (and its tests through
// read_song_bytes). Text comes in decoded to UTF-8; ``fallback_title`` names
// a song whose file names none. Readers throw only SongXmlError, and only
// for XML that does not parse.

#include <seventhslide/parse/song_import.hpp>

#include <string_view>
#include <vector>

namespace seventhslide::parse {

/// OpenLyrics 0.6-0.9 (SeventhSlide's own files among them), and XML shaped
/// like it.
[[nodiscard]] std::vector<ImportedSong> read_openlyrics(std::string_view xml,
                                                        std::string_view fallback_title);

/// OpenSong: ``<song>`` whose ``<lyrics>`` is text with ``[V1]`` headings.
[[nodiscard]] std::vector<ImportedSong> read_opensong(std::string_view xml,
                                                      std::string_view fallback_title);

/// ChordPro, one or more songs (``{new_song}``).
[[nodiscard]] std::vector<ImportedSong> read_chordpro(std::string_view text,
                                                      std::string_view fallback_title);

/// SongSelect's lyrics ``.txt`` (either footer layout), or with
/// ``songselect`` false, plain text: a title line, then verses.
[[nodiscard]] std::vector<ImportedSong> read_lyrics_text(std::string_view text,
                                                         std::string_view fallback_title,
                                                         bool songselect);

/// SongSelect's ``.usr`` / ``.bin``.
[[nodiscard]] std::vector<ImportedSong> read_songselect_usr(std::string_view text,
                                                            std::string_view fallback_title);

}  // namespace seventhslide::parse
