#pragma once

// Song files of every kind the app reads: what a file is, and the songs in it.
// Seam: plain C++20 (no Qt). Each format's reader is its own file in
// src/core/parse; this is the one door to all of them.

#include "seventhslide/model/content.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::parse {

/// What a song file was read as.
enum class SongFormat {
    OpenLyrics,
    /// OpenLyrics this app wrote (``createdIn`` names it).
    SeventhSlide,
    OpenSong,
    ChordPro,
    /// The lyrics ``.txt`` SongSelect downloads.
    SongSelectText,
    /// SongSelect's older ``.usr`` / ``.bin`` file.
    SongSelectUsr,
    PlainText,
};

/// How the format is named to the user ("SongSelect text").
[[nodiscard]] std::string_view song_format_name(SongFormat format) noexcept;

/// One song read from a file, with short notes on what the reader could not
/// carry over ("Dropped languages: DE").
struct ImportedSong {
    model::Song song;
    SongFormat format = SongFormat::OpenLyrics;
    std::vector<std::string> warnings;
};

/// A file's songs, or why it gave none (``error`` set, ``songs`` empty):
/// "Format not recognized", "Format not supported", "Damaged file", ...
struct SongFile {
    /// The file's name, as given.
    std::string name;
    std::vector<ImportedSong> songs;
    std::string error;
    /// With "Format not supported": whose format it is ("ProPresenter 7", "PDF").
    std::string format_name;
    /// With "Damaged file": what the parser said, for the log.
    std::string detail;
};

/// What ``text`` (a whole file, decoded to UTF-8) is, going by its content
/// first and its file ``name`` only where content leaves it open.
[[nodiscard]] SongFormat detect_song_format(std::string_view name, std::string_view text);

/// The songs in a file named ``name`` holding ``bytes``, whatever the
/// format and text encoding. Never throws for what the file holds.
[[nodiscard]] SongFile read_song_bytes(std::string_view name, std::string_view bytes);

/// ``read_song_bytes`` on the file at ``path``; a file that cannot be read,
/// or far too big for a song, is an ``error``.
[[nodiscard]] SongFile read_song_file(const std::filesystem::path& path);

}  // namespace seventhslide::parse
