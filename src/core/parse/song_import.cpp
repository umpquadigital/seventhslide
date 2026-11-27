// Song files of every kind; see song_import.hpp. Each format's reader is
// its own file (song_readers.hpp); this tells them apart.

#include <seventhslide/parse/song_import.hpp>

#include "song_readers.hpp"

#include <seventhslide/parse/song_sections.hpp>
#include <seventhslide/parse/song_xml.hpp>
#include <seventhslide/parse/xml_safe.hpp>
#include <seventhslide/util/strings.hpp>
#include <seventhslide/util/text_encoding.hpp>

#include <algorithm>
#include <cstdint>
#include <system_error>
#include <utility>

namespace seventhslide::parse {
namespace {

constexpr const char* k_not_supported = "Format not supported";
constexpr const char* k_not_recognized = "Format not recognized";

/// No song file comes near this; a video picked by mistake is not read whole.
constexpr std::uintmax_t k_max_song_file_bytes = 64 * 1024 * 1024;

[[nodiscard]] std::string extension_of(std::string_view name)
{
    const auto slash = name.find_last_of("/\\");
    const auto leaf = slash == std::string_view::npos ? name : name.substr(slash + 1);
    const auto dot = leaf.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        return {};
    }
    return util::to_lower(leaf.substr(dot));
}

[[nodiscard]] std::string stem_of(std::string_view name)
{
    const auto slash = name.find_last_of("/\\");
    auto leaf = slash == std::string_view::npos ? name : name.substr(slash + 1);
    const auto dot = leaf.rfind('.');
    if (dot != std::string_view::npos && dot > 0) {
        leaf = leaf.substr(0, dot);
    }
    auto stem = util::trim(leaf);
    return stem.empty() ? std::string("Untitled") : stem;
}

/// A line that is a ChordPro directive: ``{name}`` or ``{name: value}``,
/// the name letters and underscores (and a selector's ``-``).
[[nodiscard]] bool is_chordpro_directive(std::string_view line)
{
    const auto t = util::trim_view(line);
    if (t.size() < 3 || t.front() != '{' || t.back() != '}') {
        return false;
    }
    std::size_t i = 1;
    while (i < t.size() && (util::is_alpha(t[i]) || t[i] == '_' || t[i] == '-')) {
        ++i;
    }
    return i > 1 && (t[i] == '}' || t[i] == ':' || t[i] == ' ');
}

/// Programs whose song files this app knows of but cannot read yet, by
/// extension, as the user is told.
[[nodiscard]] std::string_view unsupported_program(std::string_view extension)
{
    static constexpr std::pair<std::string_view, std::string_view> k_programs[] = {
        {".pro4", "ProPresenter 4"}, {".pro5", "ProPresenter 5"}, {".pro6", "ProPresenter 6"},
        {".ews", "EasyWorship"},     {".ewsx", "EasyWorship"},    {".sng", "SongBeamer"},
        {".sbsong", "SongShow Plus"}, {".qsp", "Quelea"},
    };
    for (const auto& [ext, program] : k_programs) {
        if (extension == ext) {
            return program;
        }
    }
    return {};
}

/// Documents that can start out as text, whatever their extension: what
/// they are, as the user is told, or nothing.
[[nodiscard]] std::string_view document_kind(std::string_view bytes)
{
    if (bytes.starts_with("%PDF-")) {
        return "PDF";
    }
    if (bytes.starts_with("{\\rtf")) {
        return "Rich Text";
    }
    return {};
}

/// Bytes that are not text: a NUL where no UTF-16 byte order mark explains it.
[[nodiscard]] bool looks_binary(std::string_view bytes)
{
    if (bytes.size() >= 2
        && ((bytes[0] == '\xFF' && bytes[1] == '\xFE') || (bytes[0] == '\xFE' && bytes[1] == '\xFF'))) {
        return false;
    }
    return bytes.substr(0, 8192).find('\0') != std::string_view::npos;
}

[[nodiscard]] bool has_lyrics(const std::vector<ImportedSong>& songs)
{
    return std::any_of(songs.begin(), songs.end(), [](const ImportedSong& s) {
        return !util::trim_view(s.song.lyrics).empty();
    });
}

}  // namespace

std::string_view song_format_name(SongFormat format) noexcept
{
    switch (format) {
    case SongFormat::OpenLyrics:
        return "OpenLyrics";
    case SongFormat::SeventhSlide:
        return "SeventhSlide";
    case SongFormat::OpenSong:
        return "OpenSong";
    case SongFormat::ChordPro:
        return "ChordPro";
    case SongFormat::SongSelectText:
        return "SongSelect text";
    case SongFormat::SongSelectUsr:
        return "SongSelect file";
    case SongFormat::PlainText:
        return "Plain text";
    }
    return "Unknown";
}

SongFormat detect_song_format(std::string_view name, std::string_view text)
{
    const auto t = util::trim_view(text);
    if (!t.empty() && t.front() == '<') {
        if (util::contains_ci(t, "openlyrics.info") || t.find("<properties") != std::string_view::npos
            || t.find("<verse") != std::string_view::npos) {
            return t.find("createdIn=\"SeventhSlide") != std::string_view::npos ? SongFormat::SeventhSlide
                                                                                : SongFormat::OpenLyrics;
        }
        return t.find("<lyrics") != std::string_view::npos ? SongFormat::OpenSong
                                                            : SongFormat::OpenLyrics;
    }
    const auto lines = split_lines(t);
    if (!lines.empty() && util::trim_view(lines.front()) == "[File]") {
        return SongFormat::SongSelectUsr;
    }
    const auto ext = extension_of(name);
    if ((ext == ".usr" || ext == ".bin")
        && std::any_of(lines.begin(), lines.end(),
                       [](std::string_view l) { return util::starts_with_ci(l, "Words="); })) {
        return SongFormat::SongSelectUsr;
    }
    if (std::any_of(lines.begin(), lines.end(), is_chordpro_directive)) {
        return SongFormat::ChordPro;
    }
    if (ccli_footer_start(lines) < lines.size()) {
        return SongFormat::SongSelectText;
    }
    return SongFormat::PlainText;
}

SongFile read_song_bytes(std::string_view name, std::string_view bytes)
{
    SongFile out;
    out.name = std::string(name);
    const auto ext = extension_of(name);
    if (const auto program = unsupported_program(ext); !program.empty()) {
        out.format_name = std::string(program);
        out.error = k_not_supported;
        return out;
    }
    if (const auto kind = document_kind(bytes); !kind.empty()) {
        out.format_name = std::string(kind);
        out.error = k_not_supported;
        return out;
    }
    if (looks_binary(bytes)) {
        // A ChordPro .pro is text; ProPresenter 7's is not.
        if (ext == ".pro") {
            out.format_name = "ProPresenter 7";
            out.error = k_not_supported;
        } else {
            out.error = k_not_recognized;
        }
        return out;
    }
    const auto text = util::decode_text(bytes);
    const auto title = stem_of(name);
    const auto format = detect_song_format(name, text);
    try {
        switch (format) {
        case SongFormat::OpenLyrics:
        case SongFormat::SeventhSlide:
            out.songs = read_openlyrics(text, title);
            break;
        case SongFormat::OpenSong:
            out.songs = read_opensong(text, title);
            break;
        case SongFormat::ChordPro:
            out.songs = read_chordpro(text, title);
            break;
        case SongFormat::SongSelectText:
            out.songs = read_lyrics_text(text, title, true);
            break;
        case SongFormat::PlainText:
            out.songs = read_lyrics_text(text, title, false);
            break;
        case SongFormat::SongSelectUsr:
            out.songs = read_songselect_usr(text, title);
            break;
        }
    } catch (const std::exception& e) {
        out.songs.clear();
        // XML that never said it was a song (a web page) is not a broken song.
        if (text.find("<song") == std::string::npos) {
            out.error = k_not_recognized;
        } else {
            out.error = "Damaged file";
            out.detail = e.what();
        }
        return out;
    }
    if (!has_lyrics(out.songs)) {
        out.songs.clear();
        // XML that is no kind of song, or a song without words.
        const auto t = util::trim_view(text);
        out.error = t.starts_with('<') && t.find("<song") == std::string_view::npos
                        ? k_not_recognized
                        : "No lyrics found";
        return out;
    }
    // A file of several songs may hold an empty one (a stray {new_song}).
    std::erase_if(out.songs, [](const ImportedSong& s) { return util::trim_view(s.song.lyrics).empty(); });
    return out;
}

SongFile read_song_file(const std::filesystem::path& path)
{
    const auto name = path.filename().string();
    SongFile out;
    out.name = name;
    std::error_code size_error;
    if (const auto size = std::filesystem::file_size(path, size_error);
        !size_error && size > k_max_song_file_bytes) {
        out.error = k_not_recognized;
        return out;
    }
    try {
        return read_song_bytes(name, read_file_bytes(path));
    } catch (const std::exception&) {
        out.error = "Could not read file";
        return out;
    }
}

}  // namespace seventhslide::parse
