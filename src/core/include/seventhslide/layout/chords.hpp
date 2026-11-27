#pragma once

// Chord-aware layout: chord lines processed into structured cells.
// Seam: plain C++20 (no Qt).

#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/layout/word_wrap.hpp"
#include "seventhslide/model/rich_text.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::layout {

inline constexpr int k_default_chord_size_pct = 75;
inline constexpr int k_min_chord_size_pct = 25;
inline constexpr int k_max_chord_size_pct = 200;
inline constexpr int k_default_chord_gap_pct = 15;
inline constexpr int k_max_chord_gap_pct = 100;
inline constexpr std::string_view k_default_chord_color = "#ffd400";

/// How a lyrics box draws chords: a theme's Lyrics element sets it. Sizes are
/// percents of the lyric size, so they hold at any canvas size.
struct ChordStyle {
    /// Chord glyph size.
    int size_pct = k_default_chord_size_pct;
    /// Room from the chords' baseline to the top of the capitals under them.
    int gap_pct = k_default_chord_gap_pct;
    std::string color{k_default_chord_color};

    [[nodiscard]] bool operator==(const ChordStyle&) const noexcept = default;
};

/// ``style`` with its percents in range.
[[nodiscard]] ChordStyle clamped(ChordStyle style) noexcept;

/// The font chords are drawn in over lyrics in ``lyric``: bold, ``size_pct`` of it.
[[nodiscard]] FontStyle chord_font(const FontStyle& lyric, const ChordStyle& style);

/// Height a chord line adds to its lyric's own line box: the chords' cap
/// height and the gap. Paint puts that row where the lyric's capitals begin,
/// so the room the faces leave above their capitals stays above the chords.
/// Layout and paint both use it.
[[nodiscard]] int chord_row_px(ITextMeasurer& measurer,
                               const FontStyle& lyric,
                               const ChordStyle& style);

/// One chord-over-lyric cell.
struct ChordCell {
    /// Display glyph with typographic accidentals. ``nullopt`` = no chord (ZWSP row).
    /// Empty string = ``[]`` marker present with no name.
    std::optional<std::string> chord;
    /// Lyric syllable under the chord (may be empty).
    std::string syllable;
    /// The syllable's inline style. Chord rows have a fixed height, so every
    /// box ignores its size and rise.
    model::TextStyle style;

    [[nodiscard]] bool operator==(const ChordCell&) const noexcept = default;
};

enum class ChordItemKind { Cell, Space };

struct ChordLineItem {
    ChordItemKind kind = ChordItemKind::Cell;
    ChordCell cell;  // meaningful when ``kind == Cell``

    [[nodiscard]] bool operator==(const ChordLineItem&) const noexcept = default;

    [[nodiscard]] static ChordLineItem space()
    {
        ChordLineItem it;
        it.kind = ChordItemKind::Space;
        return it;
    }

    [[nodiscard]] static ChordLineItem make_cell(ChordCell c)
    {
        ChordLineItem it;
        it.kind = ChordItemKind::Cell;
        it.cell = std::move(c);
        return it;
    }
};

struct ChordLine {
    std::vector<ChordLineItem> items;

    [[nodiscard]] bool operator==(const ChordLine&) const noexcept = default;
};

/// Where a run of chord line items sits: see ``place_chord_items``.
struct ChordPlacement {
    /// Left edge of each item.
    std::vector<double> x;
    /// Left edge of a hyphen in each word a chord split open.
    std::vector<double> hyphens;
    /// How far the row runs: the lyric, or a last chord past it.
    double width = 0.0;

    [[nodiscard]] bool operator==(const ChordPlacement&) const noexcept = default;
};

/**
 * ``items`` side by side from 0, each cell as wide as its syllable in
 * ``lyric`` with the cell's style on top, at ``lyric``'s own size: chord rows
 * have a fixed height, so ``<size>`` does not apply there. A chord (in
 * ``chord``) may overhang its syllable; the next chord waits until it has
 * passed, a ``chord`` space after it, and its cell moves along with it. A word
 * opened up by at least a ``lyric`` space that way gets a hyphen in the gap.
 */
[[nodiscard]] ChordPlacement place_chord_items(std::span<const ChordLineItem> items,
                                               const FontStyle& lyric,
                                               const FontStyle& chord,
                                               ITextMeasurer& measurer);

/**
 * ``items`` in rows no wider than ``max_width``, each placed as
 * ``place_chord_items`` places it: whole words (runs of cells between
 * spaces), each chord travelling with its syllable, as many to a row as fit.
 * A chord alone before a word (``[F] heart``) goes with that word where the
 * two fit a row. A word, or a run of chords, wider than the box breaks between
 * its cells. Each row is a range of ``items`` that starts and ends on a cell;
 * the spaces between rows are in none. No items, no rows.
 */
[[nodiscard]] std::vector<WrapRun> wrap_chord_items(std::span<const ChordLineItem> items,
                                                    const FontStyle& lyric,
                                                    const FontStyle& chord,
                                                    ITextMeasurer& measurer,
                                                    double max_width);

/// Every syllable of ``items`` wider than ``max_width`` cut between characters
/// into cells of their own, the chord on the first. A single character wider
/// than the box stays whole, as a chord does: there is nowhere to cut one.
void cut_wide_syllables(std::vector<ChordLineItem>& items,
                        const FontStyle& lyric,
                        ITextMeasurer& measurer,
                        double max_width);

/// A chord line as a box draws it: wide syllables cut, then wrapped.
struct ChordRows {
    /// The line's items with their wide syllables cut: ``rows`` index these.
    ChordLine line;
    std::vector<WrapRun> rows;

    /// Whether row ``r`` starts inside a word cut at the row before.
    [[nodiscard]] bool continues_word(std::size_t r) const
    {
        const auto start = rows[r].start;
        return r > 0 && line.items[start - 1].kind == ChordItemKind::Cell;
    }
};

/// ``line`` as the rows it takes in a box ``max_width`` wide, lyric in ``lyric``.
/// Song layout counts these rows, and every box that shows chords draws them,
/// so what is measured is what is drawn, in song mode and in Freeform alike.
[[nodiscard]] ChordRows wrap_chord_line(ChordLine line,
                                        const FontStyle& lyric,
                                        const ChordStyle& style,
                                        ITextMeasurer& measurer,
                                        double max_width);

/// Items ``run`` of ``line`` as a line of their own.
[[nodiscard]] ChordLine chord_line_part(const ChordLine& line, WrapRun run);

/// The lyric of ``items``: each syllable in its cell's style, one space where
/// any run of spaces and chords alone falls between two words.
[[nodiscard]] model::RichLine chord_items_lyric(std::span<const ChordLineItem> items);

/// True when ``text`` contains at least one ``[...]`` chord marker.
[[nodiscard]] bool has_chords(std::string_view text) noexcept;

/// Remove ``[chord]`` markers; lyric text (incl. markup) remains.
[[nodiscard]] std::string strip_chords(std::string_view text);

/// The lyric of a line as layout measures and shows it: ``strip_chords``, runs
/// of ASCII spaces collapsed, trimmed. Still markup (inline tags are kept).
[[nodiscard]] std::string lyric_without_chords(std::string_view raw_line);

/// ``lyric_without_chords`` with the chords kept: spaces collapsed and the
/// ends trimmed exactly as there, so stripping the chords from the result
/// gives that lyric. Each chord stays before the character it was before.
[[nodiscard]] std::string lyric_with_chords(std::string_view raw_line);

/// A chord marker taken out of markup: the chord, and the UTF-8 byte offset
/// in the markup's plain text (tags read, line breaks not counted) of the
/// character it was before. An offset at the end is a chord after the text.
struct ChordMark {
    std::size_t at = 0;
    std::string chord;

    [[nodiscard]] bool operator==(const ChordMark&) const noexcept = default;
};

/// Markup with its chord markers taken out, and the markers.
struct ChordMarks {
    std::string markup;
    std::vector<ChordMark> marks;

    [[nodiscard]] bool operator==(const ChordMarks&) const noexcept = default;
};

/// ``markup``'s chord markers taken out. Markup with none comes back as it
/// was; markup with some is rewritten (``format_markup_lines``) without them.
/// A marker is a ``[...]`` within one run of text, as ``strip_chords`` reads it.
[[nodiscard]] ChordMarks take_chord_marks(std::string_view markup);

/// ``markup`` with ``marks`` put in at their offsets, in order; one past the
/// end goes after the text. Markup with no marks to put comes back as it was.
[[nodiscard]] std::string put_chord_marks(std::string_view markup,
                                          const std::vector<ChordMark>& marks);

/// ``b`` -> ♭, ``#`` -> ♯ (case-sensitive: only lowercase ``b`` is a flat).
[[nodiscard]] std::string format_chord_accidentals(std::string_view chord);

/// Parse a lyric line into structured chord cells (no HTML).
/// Leading/trailing spaces are dropped.
[[nodiscard]] ChordLine parse_chord_line(std::string_view line);

}  // namespace seventhslide::layout
