#pragma once

// Even / balanced wrap: post-group top-heavy evening within slide slack.
// Seam: plain C++20 (no Qt). Does not move lines between slides.

#include "seventhslide/build/song_layout.hpp"
#include "seventhslide/layout/word_wrap.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::build {

// Lines carry inline markup, so every ``measure`` below is a run measurer
// (``bind_run_measurer``): styled words take their own width.

/// Smallest width keeping the box-minimum line count.
[[nodiscard]] double min_even_width(const layout::StyledWords& words, const layout::RunMeasurer& measure,
                                    double box_w);

/// Top-heavy even target for the widest line.
[[nodiscard]] double topheavy_even_width(const layout::StyledWords& words, const layout::RunMeasurer& measure,
                                         double box_w);

/**
 * Smallest target in ``[ideal, box_w]`` whose ``slide_height(target)`` still fits
 * ``cap``.
 */
[[nodiscard]] double search_even_target(
    double ideal,
    double box_w,
    double cap,
    const std::function<double(double)>& slide_height);

/**
 * Even one slide's evenable lines in place.
 * Sets ``LayoutLine::wrap_fragments`` when evening bakes breaks (``size() > 1``).
 */
void even_one_slide(
    const std::vector<int>& indices,
    std::vector<LayoutLine>& lines,
    const layout::RunMeasurer& measure,
    double box_w,
    double cap,
    double strength);

/**
 * Post-grouping evening for all slides.
 * ``strength`` is 0.0-1.0 (UI percent / 100).
 */
void even_slide_lines(
    LayoutResult& result,
    const layout::RunMeasurer& measure,
    double box_w,
    double cap,
    double strength);

/// Clamp UI ``balance_wrapped_strength`` (0-100+) -> 0.0-1.0.
[[nodiscard]] double balance_strength_from_percent(int percent) noexcept;

/**
 * Apply evening when ``balance_wrapped_lines`` is on, strength &gt; 0, and
 * ``follow_lines &lt;= 0``.
 */
void apply_even_wrap_if_enabled(
    LayoutResult& result,
    const theme::ResolvedTheme& theme,
    const layout::RunMeasurer& measure,
    double box_w,
    double cap);

}  // namespace seventhslide::build
