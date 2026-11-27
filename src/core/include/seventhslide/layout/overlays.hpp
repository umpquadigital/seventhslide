#pragma once

// Title-slide overlays: positioned text boxes with ``{token}`` substitution.
// Plain C++20 (no Qt).

#include "seventhslide/canvas/box.hpp"
#include "seventhslide/layout/chords.hpp"
#include "seventhslide/layout/slide_grouper.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/model/rich_text.hpp"
#include "seventhslide/model/transition.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::layout {

/// Built-in template token names.
inline constexpr std::string_view k_template_variables[] = {
    "song-title",
    "songbook",
    "songbook-number",
    "authors",
    "key",
    "copyright",
    "ccli-number",
    "ccli-license",
};

/// One run of an overlay row, placed: drawn in the box font with ``style``
/// on top (see ``span_font_style``).
struct OverlayRun {
    std::string text;
    model::TextStyle style;
    /// Left edge, from the row's left edge.
    double x = 0.0;
    /// Baseline, from the row's top (any ``baseline_rise_pct`` included).
    double baseline = 0.0;

    [[nodiscard]] bool operator==(const OverlayRun&) const noexcept = default;
};

/// A chord over a row of a box that shows chords, in the box's chord style.
struct OverlayChord {
    /// Display glyphs (typographic accidentals).
    std::string text;
    /// Left edge, from the row's left edge.
    double x = 0.0;
    /// Baseline, from the row's top.
    double baseline = 0.0;
    double size_px = 0.0;

    [[nodiscard]] bool operator==(const OverlayChord&) const noexcept = default;
};

/// One visual row of an overlay box: a logical line, or a piece of one that
/// wrapped.
struct OverlayRow {
    /// Its column's left, from the left of the box's content: 0 in a box of
    /// one column.
    double x = 0.0;
    /// Top, from the top of its column's content.
    double y = 0.0;
    /// Its paragraph's own alignment; empty = the box's.
    std::string align;
    double width = 0.0;
    double height = 0.0;
    std::vector<OverlayRun> runs;
    /// Chords over the runs (a box that shows chords, a line that has them).
    std::vector<OverlayChord> chords;

    [[nodiscard]] bool operator==(const OverlayRow&) const noexcept = default;
};

/// An overlay box broken into rows and measured: everything paint needs but
/// the box's position, alignment and colours. See ``lay_out_overlay``.
struct OverlayLayout {
    std::vector<OverlayRow> rows;
    /// Rows plus the gaps between logical lines (the margins excluded).
    double height = 0.0;
    /// What the box's type was scaled by to fit (autofit): 1 unless it
    /// shrank. Paint sizes the box font by it; the rows' runs carry theirs.
    double font_scale = 1.0;

    [[nodiscard]] bool operator==(const OverlayLayout&) const noexcept = default;
};

/// One positioned flow box. Each line is a ``RichLine``: box bold, italic and
/// underline are baked into the run when the markup has no inline style of
/// its own, and left on the runs when it does.
struct OverlayBox : canvas::BoxRect, canvas::BoxType {
    /// Degrees clockwise about the box's middle: words and box together.
    double rotation = 0.0;
    std::vector<model::RichLine> lines;
    /// Extra room above each line, in px, when another is above it (a song's
    /// verse gap); empty = none. Parallel to ``lines``.
    std::vector<int> gaps_above;
    /// Read ``[chord]`` markers in ``lines`` as chords over their words, in
    /// ``chords``' style. Off, they are text like any other.
    bool show_chords = false;
    ChordStyle chords;
    /// Freeform object id when this box was compiled from a document object.
    std::string id;
    /// Its place in the frame's stack when it paints as a box of its own (a
    /// title slide's, from its element's z). A box an object carries goes by
    /// the object's ``SceneObject::z``.
    int z = 0;
    /// The effects on its words (an outline, a text shadow). Its frame and
    /// the effects round that are its object's (the scene carries it).
    canvas::Effects effects;
    /// Its paragraphs: bullets, the spaces between, the indent, margins and
    /// autofit. Each line of ``lines`` is one paragraph.
    canvas::Paragraph paragraph;
    /// ``lines`` wrapped to the box and measured, filled when a scene frame
    /// is built (``scene::lay_out_frame_text``). Empty in templates and
    /// documents; paint lays out a box that arrives without it.
    std::optional<OverlayLayout> layout;

    [[nodiscard]] bool operator==(const OverlayBox&) const noexcept = default;
};

/// One overlay line. Unstyled markup becomes a single run carrying ``bold``,
/// ``italic`` and ``underline``; styled markup keeps each run's own style.
[[nodiscard]] model::RichLine styled_line(const model::RichLine& parsed,
                                          bool bold,
                                          bool italic,
                                          bool underline);

/// Full-canvas transparent overlay (boxes only; background is separate).
struct OverlayScene {
    std::vector<OverlayBox> boxes;

    [[nodiscard]] bool empty() const noexcept { return boxes.empty(); }
    [[nodiscard]] bool operator==(const OverlayScene&) const noexcept = default;
};

/// Token lookup key: trimmed and ASCII-lowercased.
[[nodiscard]] std::string token_key(std::string_view name);

/// The widest gap a box may set between its lines, in canvas pixels.
inline constexpr int k_max_line_gap = 300;

[[nodiscard]] double clamp_overlay_line_height(double raw) noexcept;
[[nodiscard]] int clamp_overlay_line_gap(int raw) noexcept;
[[nodiscard]] double clamp_overlay_letter_spacing(double raw) noexcept;

/**
 * Fill ``{token}`` / ``{-token}`` placeholders.
 *
 * Empty known tokens substitute to nothing by default (literal neighbors stay).
 * A leading ``-`` inside the braces (``{-songbook-number}``) opts that token
 * into whole-line hide when its value is empty, for decoration lines that
 * mean nothing without their value.
 */
[[nodiscard]] std::string substitute_variables(
    std::string_view text,
    const std::map<std::string, std::string>& values);

/// Every built-in token (``k_template_variables``), empty, overlaid with
/// ``song_vars``: what title overlays and the copyright notice fill from.
[[nodiscard]] std::map<std::string, std::string> song_token_values(
    const std::map<std::string, std::string>& song_vars);

/// Song -> built-in token map (keys from ``k_template_variables``).
/// ``ccli_licence`` fills ``ccli-license`` (church licence); ``ccli-number`` is
/// the song's CCLI catalog number.
[[nodiscard]] std::map<std::string, std::string> song_template_vars(
    const model::Song& song,
    std::string_view ccli_licence = {});

/// Resolve template boxes (lines hold ``{token}`` text) into a scene: tokens
/// substituted, empty lines and boxes dropped, typography clamped.
[[nodiscard]] OverlayScene build_overlay_scene(
    const std::vector<OverlayBox>& templates,
    const std::map<std::string, std::string>& token_values);

/// Title slide: song vars only (no slots).
[[nodiscard]] OverlayScene build_title_overlay_scene(
    const std::vector<OverlayBox>& templates,
    const std::map<std::string, std::string>& song_vars);

/**
 * Per-slide overlay scenes: copy ``overlay`` onto title groups, empty elsewhere
 *.
 */
[[nodiscard]] std::vector<OverlayScene> assign_overlay_to_title_slides(
    const std::vector<SlideGroup>& groups,
    const OverlayScene& overlay);

}  // namespace seventhslide::layout
