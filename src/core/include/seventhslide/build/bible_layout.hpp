#pragma once

// Bible chunking - shared verse chunks -> per-output slides.
// Seam: plain C++20 (no Qt). Measure via ITextMeasurer; no HTML spans.

#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/layout/word_wrap.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/theme/resolved_theme.hpp"

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::build {

/// One verse to lay out.
using BibleVerseInput = model::PassageVerse;

/// Shared chunks for one verse (text includes ``{chapter}:{verse} `` prefix).
struct BibleVerseChunks {
    int verse_num = 0;
    std::vector<std::string> chunks;
};

/// Unified chunk plan across all verses (player ``plain_lines`` / labels).
struct BibleChunkPlan {
    int chapter = 0;
    std::vector<BibleVerseChunks> verses;
    std::vector<std::string> plain_lines;
    std::vector<std::string> line_labels;  // ``"{chapter}:{verse}"``
};

/// One audience bible slide for a single output (no HTML).
struct BibleSlide {
    /// Display text; verses sharing a slide are joined by a space. Shown
    /// citations keep the layout marker ``"ch:v "`` so paint wraps where the fit
    /// pass did. Hidden citations are stripped.
    std::string text;
    /// ``"ch:v"`` of each citation marker in ``text``, in order. Empty when
    /// citations are hidden.
    std::vector<std::string> citations;
};

struct BibleOutputSlides {
    std::vector<BibleSlide> slides;
    /// Flat chunk index -> slide index (same length as ``BibleChunkPlan::plain_lines``).
    std::vector<int> line_to_slide;
};

/// Usable bible text box + font for one output.
struct BibleBoxMetrics {
    layout::FontStyle style;
    int line_height_px = 1;
    int avail_w = 1;
    int avail_h = 1;
    /// A row holding a shown citation: taller than ``line_height_px`` when the
    /// citation is larger than the text, as paint opens the row up for it.
    int citation_row_px = 1;
};

/// Usable box + the font paint will actually use (``inherit`` sentinels resolved).
[[nodiscard]] BibleBoxMetrics bible_box_metrics(
    const theme::ResolvedTheme& theme, layout::ITextMeasurer& measurer);

/// Theme fields that decide how paint draws a ``ch:v`` citation.
struct BibleCitationStyle {
    bool shown = false;
    /// Absolute px from the theme; ``0`` -> half the body size.
    int font_size_px = 0;
    /// Empty -> the bible text box family.
    std::string font_family;
    bool bold = false;
    bool italic = false;
    int margin_right_pct = 40;
};

[[nodiscard]] BibleCitationStyle bible_citation_style(const theme::ResolvedTheme& theme);

/// Citation font size as a percent of ``body_px`` (the citation span's ``size_pct``).
[[nodiscard]] int bible_citation_size_pct(const BibleCitationStyle& cite, int body_px);

/// One citation marker found in bible body text.
struct BibleCitationMarker {
    /// Byte offset of ``ch:v`` in the searched text.
    std::size_t offset = 0;
    /// Byte length of ``ch:v`` (excludes the trailing space).
    std::size_t length = 0;
    /// A layout space follows the marker (paint drops it for the citation margin).
    bool trailing_space = false;

    /// One past the marker, including its trailing space.
    [[nodiscard]] std::size_t end() const noexcept
    {
        return offset + length + (trailing_space ? 1 : 0);
    }
};

/**
 * Citation markers in ``text``: whole space-delimited words equal to one of
 * ``citations``.
 *
 * Markers are matched against the slide's known citations, never by shape, so
 * verse text that happens to contain ``digits:digits`` is left alone.
 */
[[nodiscard]] std::vector<BibleCitationMarker> find_bible_citation_markers(
    std::string_view text,
    const std::vector<std::string>& citations);

/**
 * Width measurer for bible body text carrying citation markers.
 *
 * Measures what paint draws: the text at ``box_style``, every marker of
 * ``citations`` in it as its citation span, with no space after it. The fit pass
 * and the scene builder both wrap through this, so they break rows at the same
 * words. A citation counts as no more than ``max_width_px``, so that wrapping
 * never cuts it. Only ``measurer`` must outlive the result.
 */
[[nodiscard]] layout::WidthMeasurer bible_text_measurer(
    const layout::FontStyle& box_style,
    const BibleCitationStyle& cite,
    const std::vector<std::string>& citations,
    layout::ITextMeasurer& measurer,
    double max_width_px = std::numeric_limits<double>::infinity());

/**
 * Split one verse into chunks that fit *every* bible output's box.
 *
 * Chunks are shared across outputs (player line indices are global), so each
 * output's box constrains the split in turn. Empty ``bible_outputs`` -> single
 * prefixed chunk.
 */
[[nodiscard]] std::vector<std::string> chunk_bible_verse(
    int chapter,
    int verse_num,
    std::string_view verse_text,
    const std::vector<const theme::ResolvedTheme*>& bible_themes,
    layout::ITextMeasurer& measurer);

/**
 * Chunk all verses and build flat ``plain_lines`` / ``line_labels``
 * (shared across outputs).
 */
[[nodiscard]] BibleChunkPlan plan_bible_chunks(
    int chapter,
    const std::vector<BibleVerseInput>& verses,
    const std::vector<const theme::ResolvedTheme*>& bible_themes,
    layout::ITextMeasurer& measurer);

/**
 * Pack shared chunks into slides for one output.
 *
 * Chunks of one verse fill a slide greedily. With ``bible_fluid_verses`` the next
 * verse joins the open slide when the whole verse fits; a verse is never split
 * to use leftover room. Otherwise every verse starts a new slide.
 */
[[nodiscard]] BibleOutputSlides build_bible_slides_for_output(
    const theme::ResolvedTheme& theme,
    const BibleChunkPlan& plan,
    layout::ITextMeasurer& measurer);

}  // namespace seventhslide::build
