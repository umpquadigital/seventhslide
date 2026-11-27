#pragma once

// Lyrics block parser: verse headers, order, codes, logical lines.
// Seam: plain C++20; reuses ``model::RichLine`` / ``k_forced_split`` (no Qt).

#include "seventhslide/model/rich_text.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::parse {

/// Ordered verse bodies + parallel codes.
struct LyricsBlocks {
    std::vector<std::string> verses;
    std::vector<std::string> verse_codes;

    [[nodiscard]] bool empty() const noexcept { return verses.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return verses.size(); }
};

/// Parse ``---[Label]---`` lyrics (or plain blank-line verses) into ordered blocks.
/// ``suppress_titles`` drops baked Title sections and turns ``t1`` order tokens into
/// virtual empty verses (theme overlays replace them).
[[nodiscard]] LyricsBlocks parse_verses(
    std::string_view lyrics,
    std::string_view verse_order = {},
    bool suppress_titles = false);

/// One verse as the song editor shows it.
struct LabelledVerse {
    std::string label;    ///< ``Verse:1`` (no section letter)
    std::string content;  ///< body; joined sections are split by ``model::k_forced_split`` lines

    [[nodiscard]] bool operator==(const LabelledVerse&) const = default;
};

/// The verses of ``lyrics`` as the song editor edits them: one per
/// ``---[Label]---`` section, in order, with the lettered sections of one verse
/// (``Verse:1a``, ``Verse:1b``) joined exactly as ``parse_verses`` joins them for
/// playback. Lyrics without headers split at blank lines into ``Verse:1``,
/// ``Verse:2``, ...
[[nodiscard]] std::vector<LabelledVerse> labelled_verses(std::string_view lyrics);

/// ``verses`` back as lyrics: ``---[Label]---`` sections, a blank line apart.
[[nodiscard]] std::string join_labelled_verses(const std::vector<LabelledVerse>& verses);

inline constexpr int k_min_verse_number = 1;
inline constexpr int k_max_verse_number = 99;

/// One OpenLyrics verse type: its code letter and canonical label.
struct VerseType {
    char code;
    std::string_view name;
};

/// Every verse type, in the order the song editor lists them.
inline constexpr std::array<VerseType, 8> k_verse_types{{
    {'v', "Verse"},
    {'c', "Chorus"},
    {'p', "Pre-Chorus"},
    {'b', "Bridge"},
    {'i', "Intro"},
    {'e', "Ending"},
    {'o', "Other"},
    {'t', "Title"},
}};

/// The type whose code letter is ``code`` (lower case), or null.
[[nodiscard]] const VerseType* verse_type_for_code(char code) noexcept;

/// ``k_verse_types`` labels.
[[nodiscard]] const std::vector<std::string_view>& verse_type_names();

/// ``Verse`` / ``verse`` / ``Pre-Chorus`` -> canonical label. Empty / unknown / ``None`` -> empty.
[[nodiscard]] std::string canonicalize_verse_type(std::string_view type);

[[nodiscard]] int clamp_verse_number(int number) noexcept;

/// Compact playback code (``v1``, ``c2``). Empty type -> empty code.
[[nodiscard]] std::string verse_code_from_label(std::string_view type, int number);

/// ``v1`` / ``v1a`` -> Verse 1. Unknown / empty / ``misc`` -> empty type.
struct VerseLabel {
    std::string type;
    int number = k_min_verse_number;
};
[[nodiscard]] VerseLabel verse_label_from_code(std::string_view code);

/// Uppercased compact code for badges / chrome (``V1``). Empty type -> empty.
[[nodiscard]] std::string verse_indicator_text(std::string_view type, int number);

/// ``Verse:1`` / ``Verse:1a`` -> ``v1`` (section letters are not codes).
/// Unknown labels -> ``misc``.
[[nodiscard]] std::string label_to_code(std::string_view label);

/// ``t1``, ``t1a``, ... (theme title codes).
[[nodiscard]] bool is_title_code(std::string_view code);

/// ``th1``, ``th2``, ... (play-order theme chips). Not a slide; skipped when expanding verses.
[[nodiscard]] bool is_theme_code(std::string_view code);

/// ``v1a`` -> ``v1``; other codes unchanged.
[[nodiscard]] std::string base_verse_code(std::string_view code);

/// True when both are lettered sections of the same verse (``v1a``/``v1b``).
[[nodiscard]] bool same_verse_sections(std::string_view a, std::string_view b);

/// Error message if ``verse_order`` references codes absent from lyrics; else nullopt.
/// Title codes (``t1``) and theme chips (``th1``) are always allowed.
[[nodiscard]] std::optional<std::string> validate_verse_order(
    std::string_view verse_order,
    std::string_view lyrics);

/// Error if a structured verse body has a blank line between non-blank lines.
[[nodiscard]] std::optional<std::string> validate_no_blank_lines_in_verse(
    std::string_view lyrics);

// --- the song editor's verse labels -------------------------------------
// A label the editor writes is ``Type:N`` (``Verse:2``); one read back from
// imported lyrics may carry a section letter (``Verse:2a``) or be anything at
// all. These say what the editor makes of either.

/// ``Verse:2a`` -> ``Verse 2``: what comes before the last ``:<digits>``, a
/// space, and those digits. A label without ``:<digits>`` is unchanged.
[[nodiscard]] std::string verse_display_name(std::string_view label);

/// The verse type ``label`` names (the part before its first ``:``, ignoring
/// case) as the editor lists it, never Title; ``Verse`` when it names none.
[[nodiscard]] std::string_view editor_verse_type(std::string_view label);

/// The number after ``label``'s first ``:``, clamped to 1..99; 1 when there is
/// none.
[[nodiscard]] int verse_number(std::string_view label);

/// ``Type:N`` with ``number`` clamped to 1..99 and an empty ``type`` read as
/// ``Verse``.
[[nodiscard]] std::string make_verse_label(std::string_view type, int number);

/// Index of the label in ``labels`` naming verse ``type`` ``number`` (case and
/// section letter ignored), skipping ``except``; -1 when none does.
[[nodiscard]] int find_verse_label(const std::vector<std::string>& labels,
                                   std::string_view type,
                                   int number,
                                   int except = -1);

/// The lowest number from ``preferred`` up that no other ``type`` verse in
/// ``labels`` (``except`` skipped) uses, wrapping to 1 past 99; -1 when every
/// one is taken.
[[nodiscard]] int next_free_verse_number(const std::vector<std::string>& labels,
                                         std::string_view type,
                                         int preferred,
                                         int except = -1);

/// The codes of a typed verse order (``v1 c1  v2``), upper-cased.
[[nodiscard]] std::vector<std::string> verse_order_tokens(std::string_view order);

}  // namespace seventhslide::parse
