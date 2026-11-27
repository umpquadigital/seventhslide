#pragma once

// Overlay boxes wrapped and measured: title slides, theme text elements,
// Freeform text, clocks.
//
// Overlay text wraps with the same engine as lyrics (``StyledWords`` /
// ``wrap_word_runs``), with two things lyrics do not need: whitespace between
// words is kept as typed, and a word wider than the box is broken between
// characters. Scene build lays every box out once; paint only draws.
//
// Plain C++20 (no Qt). Measure via ITextMeasurer.

#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/layout/text_measurer.hpp"

namespace seventhslide::layout {

/**
 * Break ``box``'s lines into rows that fit ``box.w`` and place every run.
 *
 * Each run is measured in the box font with its own style on top (family,
 * size, bold, italic, margin), the box's bold and italic having been baked
 * into the runs already. A row is as tall as its largest run at the box line
 * height (default 1.15); each run sits centred in the row by its own font's
 * metrics. Logical lines are ``line_gap`` percent of the font size apart. A
 * line with no text keeps one empty row.
 *
 * Empty for a box with no lines or no area (paint skips those).
 */
[[nodiscard]] OverlayLayout lay_out_overlay(const OverlayBox& box, ITextMeasurer& measurer);

/// The most columns a box sets its words in.
inline constexpr int k_max_columns = 6;

/// How many columns ``box`` sets its words in: its paragraph's, 1 to
/// k_max_columns.
[[nodiscard]] int overlay_columns(const OverlayBox& box) noexcept;

/// The width of one of ``box``'s columns: its width inside the margins, less
/// the gaps between columns, shared out (the whole of it in one column).
/// Rows wrap at this; a row's ``x`` is its column's left.
[[nodiscard]] double overlay_column_width(const OverlayBox& box) noexcept;

/// The space ``box`` leaves between two logical lines: ``line_gap`` percent
/// of its font size. A box's laid-out height is its lines' heights plus one of
/// these between each pair, so lines can be measured a few at a time.
[[nodiscard]] double overlay_line_gap_px(const OverlayBox& box) noexcept;

}  // namespace seventhslide::layout
