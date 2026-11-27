#pragma once

// Song slide layout: verses -> measured lines -> slide groups.
// Seam: plain C++20; uses parse / wrap / chords / ITextMeasurer (no Qt).

#include "seventhslide/layout/chords.hpp"
#include "seventhslide/layout/slide_grouper.hpp"
#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/theme/resolved_theme.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::build {

/// One finished logical line ready for packing / later paint.
struct LayoutLine {
    std::string text;  // plain lyric (empty for virtual title); may carry ``<b>/<i>/<u>``
    std::string label;
    int verse_index = 0;
    int base_height_px = 0;
    int active_height_px = 0;
    /// Effective height per visual line (evening / paint); excludes ``extra_px``.
    int line_height_px = 0;
    int extra_px = 0;
    bool evenable = false;
    bool is_title = false;
    /// ``text`` with its chords, when it has some (a part of a split line,
    /// its own words' chords). Kept whether or not this output shows chords
    /// (Save as Freeform reads it).
    std::string chord_text;
    /// Present when ``show_chords`` and the source line had markers: the
    /// chords over ``text``. Paint wraps them to the box
    /// (``layout::wrap_chord_items``); the heights above count those rows.
    layout::ChordLine chords;
    bool has_chord_layout = false;
    /// Baked visual fragments for paint when the logical line needs more than one
    /// visual line (even-wrap target and/or full box width), a word wider than
    /// the box cut across them. Empty => single visual line (or chord/title -
    /// paint uses ``text`` as-is).
    std::vector<std::string> wrap_fragments;
    /// ``wrap_fragments`` are evening's breaks, not what the box's width makes.
    bool evened = false;
};

/// The height ``line`` paints at in ``theme``'s box at the box's own size: what
/// a follow line from another theme chip takes on this one's slide.
[[nodiscard]] int painted_line_height_px(const LayoutLine& line,
                                         const theme::ResolvedTheme& theme,
                                         layout::ITextMeasurer& measurer);

struct LayoutResult {
    std::vector<LayoutLine> lines;
    std::vector<layout::SlideGroup> groups;
    std::vector<std::string> verse_codes;
    std::vector<int> line_to_slide;
    /// The rows every output laid out together shares (see
    /// ``compute_song_layouts``) - what the player's cursor steps through -
    /// each mapped to the line in ``lines`` that shows it, or -1 where this
    /// output shows nothing for it. A line shown whole where others show it in
    /// parts covers each part's row.
    std::vector<int> row_lines;
};

/// ``layout.line_to_slide`` per row. A row the output shows nothing for keeps
/// the slide of the row before it (the first slide before any).
[[nodiscard]] std::vector<int> row_to_slide(const LayoutResult& layout);

/**
 * Lay a song out on several outputs at once, one result per output in order.
 *
 * The player has one cursor for all outputs, so every output gets the same
 * rows; each still groups them into slides its own way (four lines a slide on
 * one, two on another). A line too tall for some output's box, at any size that
 * output draws it at, is split into parts that fit every output that cannot
 * show it whole; an output with room shows it whole and holds it across the
 * parts' rows. Each line is read and tokenised once.
 *
 * Applies ``balance_wrapped_lines`` when enabled. Theme title prepend remains a
 * separate post-pass. Virtual ``t1`` from ``suppress_titles`` is included.
 * ``measurer`` supplies advance widths; line-height uses ``css_line_height_px``.
 */
[[nodiscard]] std::vector<LayoutResult> compute_song_layouts(
    const std::vector<const theme::ResolvedTheme*>& themes,
    layout::ITextMeasurer& measurer,
    std::string_view lyrics,
    std::string_view verse_order = {},
    bool suppress_titles = false);

/// ``compute_song_layouts`` for one output.
[[nodiscard]] LayoutResult compute_song_layout(
    const theme::ResolvedTheme& theme,
    layout::ITextMeasurer& measurer,
    std::string_view lyrics,
    std::string_view verse_order = {},
    bool suppress_titles = false);

/**
 * True when ``line_index`` follows ``prev_line_index`` across a verse boundary
 * that fluid slides separate with the theme's ``verse_gap``. Sections of one
 * lettered verse (``v1a`` / ``v1b``) are not separated.
 */
[[nodiscard]] bool needs_verse_gap(const LayoutResult& layout,
                                   bool fluid_slides,
                                   int verse_gap,
                                   int line_index,
                                   int prev_line_index);

}  // namespace seventhslide::build
