#pragma once

// Title slides: virtual title prepend and upcoming-line reuse.
// Overlay content is built in layout/overlays.hpp; this only assigns slots.
// Plain C++20 (no Qt).

#include "seventhslide/build/song_layout.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::build {

/// True when verse_order is empty OR contains a ``t1`` token.
[[nodiscard]] bool verse_order_allows_title_slide(std::string_view verse_order) noexcept;

/**
 * The song's half of the title-slide decision: empty verse_order (implicit
 * title) or a ``t1`` token.
 *
 * The theme's half is ``show_title_slides``, and ``session::build_song`` ands
 * the two together across every active output at once - they share one line
 * cursor, so the slide exists in all of them or in none. Whether a given output
 * *paints* a title design there is separate and per-output: one without a design
 * shows the song's opening lines instead (``should_show_title_upcoming``).
 */
[[nodiscard]] bool title_slide_mode_active(std::string_view verse_order) noexcept;

/// Prepend only when title mode is on and verse_order is blank.
[[nodiscard]] bool should_prepend_title_line(
    bool title_mode_active,
    std::string_view verse_order) noexcept;

/**
 * Show opening lyric lines dimmed on the title slide when this output has no
 * overlay of its own (follow-lines + ``title_slide_show_lines``).
 */
[[nodiscard]] bool should_show_title_upcoming(
    bool title_mode_active,
    const theme::ResolvedTheme& theme,
    bool this_output_has_overlay) noexcept;

/**
 * Prepend a virtual title line + group.
 * Mutates ``result`` in place; rebuilds ``line_to_slide``.
 */
void prepend_title_line(LayoutResult& result, bool show_upcoming_lines);

/**
 * Show the next lyric window on the title slide ``group_index``: its lines,
 * with ``active_count = 0``. The title line stays the slide's own. False when
 * no lyric slide follows. Does not rebuild ``line_to_slide``.
 */
bool show_upcoming_on_title(LayoutResult& result, std::size_t group_index);

/**
 * ``show_upcoming_on_title`` for every title slide. Rebuilds the map.
 */
void apply_title_upcoming(LayoutResult& result);

/**
 * Apply title-slide layout policy for one output after ``compute_song_layout``
 * (with ``suppress_titles == title_mode_active``).
 *
 * - empty ``verse_order`` -> ``prepend_title_line`` (upcoming if applicable)
 * - else if upcoming -> ``apply_title_upcoming`` on existing ``t1`` groups
 */
void apply_title_slide_layout(
    LayoutResult& result,
    const theme::ResolvedTheme& theme,
    std::string_view verse_order,
    bool title_mode_active,
    bool this_output_has_overlay);

}  // namespace seventhslide::build
