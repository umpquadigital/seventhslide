#pragma once

// Bible XML import + free-text references.
// Seam: plain C++20 + pugixml (no Qt).

#include "seventhslide/model/content.hpp"
#include "seventhslide/model/library.hpp"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::parse {

class BibleXmlError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Result of parsing a Zefania, OSIS or OpenSong Bible XML document.
struct ParsedBible {
    std::string name;
    std::string copyright;
    /// Flat verse rows; ``id`` / ``bible_id`` left unset for the caller to fill.
    std::vector<model::BibleVerse> verses;
};

[[nodiscard]] ParsedBible parse_bible_file(const std::filesystem::path& path);
/// The same, for a file's bytes already read (one in the program's resources).
[[nodiscard]] ParsedBible parse_bible_bytes(std::string_view data);

/// Resolve a free-text book token against a bible's book-name list.
/// Exact normalized match -> alias table -> unique prefix. ``nullopt`` if ambiguous
/// or missing.
[[nodiscard]] std::optional<std::string> resolve_bible_book(
    const std::vector<std::string>& books,
    std::string_view token);

/// Books whose normalized name or alias is prefixed by ``token`` (bible order).
/// Empty token yields an empty list. Used for search autocomplete.
[[nodiscard]] std::vector<std::string> suggest_bible_books(
    const std::vector<std::string>& books,
    std::string_view token);

/// Parse ``"John 3:16"``, ``"Rom 8:28-30"``, ``"Ps 23"``, etc. against ``books``.
/// ``nullopt`` when the string or book cannot be resolved. Whole-chapter refs leave
/// ``verse_start`` / ``verse_end`` empty.
[[nodiscard]] std::optional<model::BibleRef> parse_bible_reference(
    std::string_view reference,
    const std::vector<std::string>& books);

/// One verse by chapter and number, as a Bible line labels it (``3:16``).
struct VerseRef {
    int chapter = 0;
    int verse = 0;

    [[nodiscard]] bool operator==(const VerseRef&) const noexcept = default;
};

/// The longest chapter (Psalms) and the longest one's last verse (Psalm 119).
inline constexpr int k_max_bible_chapter = 150;
inline constexpr int k_max_bible_verse = 176;

/// ``chapter:verse``.
[[nodiscard]] std::string format_verse_ref(VerseRef ref);

/// ``text`` when it is exactly ``chapter:verse`` with both in range, else nullopt.
[[nodiscard]] std::optional<VerseRef> parse_verse_ref(std::string_view text);

/// A leading ``chapter:verse`` on ``text``: the reference, and the text after the
/// one space that ends it, if any. Nullopt when ``text`` does not start with a
/// reference that is a word of its own.
struct LeadingVerseRef {
    VerseRef ref;
    std::string_view rest;
};
[[nodiscard]] std::optional<LeadingVerseRef> split_leading_verse_ref(std::string_view text);

}  // namespace seventhslide::parse
