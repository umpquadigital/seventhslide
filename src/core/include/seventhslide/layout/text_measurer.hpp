#pragma once

// Text measurement seam for layout.
// Measures advance width; line height follows CSS (1.2 by default).
// Qt Gui lives only in QtTextMeasurer (see qt_text_measurer.hpp).

#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace seventhslide::layout {

/// Font request for measurement (family + CSS pixel size + style flags).
struct FontStyle {
    std::string family;
    double size_px = 48.0;
    bool bold = false;
    bool italic = false;
    /// Unitless CSS line-height factor (lyrics default 1.2).
    double line_height = 1.2;
    /// Extra tracking in layout pixels (CSS ``letter-spacing``). 0 = font default.
    double letter_spacing = 0.0;

    [[nodiscard]] bool operator==(const FontStyle&) const noexcept = default;
};

/// The line-height multipliers a theme or a Freeform box may set.
inline constexpr double k_min_line_height = 0.8;
inline constexpr double k_max_line_height = 3.0;

/// Clamp a CSS line-height multiplier (theme / Freeform overlay range).
[[nodiscard]] inline double clamp_css_line_height(double factor) noexcept
{
    if (!(factor > 0.0) || !std::isfinite(factor)) {
        return 1.2;
    }
    if (factor < k_min_line_height) {
        return k_min_line_height;
    }
    if (factor > k_max_line_height) {
        return k_max_line_height;
    }
    return factor;
}

/// The extra character spacing a box may set, in layout/canvas pixels.
inline constexpr double k_min_letter_spacing_px = -50.0;
inline constexpr double k_max_letter_spacing_px = 200.0;

/// Clamp extra character spacing in layout/canvas pixels.
[[nodiscard]] inline double clamp_letter_spacing_px(double px) noexcept
{
    if (!std::isfinite(px)) {
        return 0.0;
    }
    if (px < k_min_letter_spacing_px) {
        return k_min_letter_spacing_px;
    }
    if (px > k_max_letter_spacing_px) {
        return k_max_letter_spacing_px;
    }
    return px;
}

/// Extra width from CSS-style tracking between UTF-8 code points.
[[nodiscard]] inline double tracking_extra_px(std::string_view text, double letter_spacing) noexcept
{
    const double spacing = clamp_letter_spacing_px(letter_spacing);
    if (spacing == 0.0 || text.empty()) {
        return 0.0;
    }
    const std::size_t n = util::utf8_length(text);
    if (n < 2) {
        return 0.0;
    }
    return static_cast<double>(n - 1) * spacing;
}

/// CSS ``line-height`` -> ``ceil(size_px * factor)`` (default factor 1.2).
[[nodiscard]] inline int css_line_height_px(double size_px, double factor = 1.2) noexcept
{
    if (!(size_px > 0.0)) {
        return 1;
    }
    return static_cast<int>(std::ceil(size_px * clamp_css_line_height(factor)));
}

/// Height of a painted line box: ``ceil(size_px * factor)``, the factor
/// unclamped (chord rows run taller than any theme line height). A
/// non-positive size counts as 1 px and a non-positive factor as 1.2.
[[nodiscard]] inline double line_box_height_px(double size_px, double factor) noexcept
{
    const double size = size_px > 0.0 ? size_px : 1.0;
    const double f = factor > 0.0 ? factor : 1.2;
    return std::ceil(size * f);
}

/// Vertical metrics of a font, in layout pixels (Qt ``QFontMetricsF``).
struct FontMetrics {
    double ascent = 0.0;
    double descent = 0.0;
    /// ``ascent + descent`` plus whatever the face adds (Qt ``height()``).
    double height = 0.0;
    /// Top of a capital letter above the baseline: where the ink starts,
    /// where ``ascent`` also counts the room the face leaves above it.
    double cap_height = 0.0;
};

/// Baseline from the top of a line box ``box_height`` high holding text in a
/// font with ``metrics``: centred in it, the font's top never above the box's.
/// Every text box sets its rows by this.
[[nodiscard]] inline double baseline_in_line_box(double box_height,
                                                 const FontMetrics& metrics) noexcept
{
    return std::max(0.0, (box_height - metrics.height) * 0.5) + metrics.ascent;
}

/// Advance-width measurer used by wrap / split / slide packing, and by scene
/// build to place what paint draws.
class ITextMeasurer {
public:
    virtual ~ITextMeasurer() = default;

    /// Horizontal advance in layout pixels (Qt ``horizontalAdvance``).
    /// Empty text -> 0. Never returns a non-positive width for non-empty text when using
    /// the approx fallback (wrapping would otherwise collapse).
    [[nodiscard]] virtual double measure_width(
        std::string_view text, const FontStyle& style) = 0;

    /// Line box height in pixels for ``style`` (independent of glyph load success).
    [[nodiscard]] virtual int line_height_px(const FontStyle& style) const
    {
        return css_line_height_px(style.size_px, style.line_height);
    }

    /// Ascent, descent and height of ``style``'s font. The default is a
    /// proportion of the size, for measurers with no real font behind them.
    [[nodiscard]] virtual FontMetrics font_metrics(const FontStyle& style)
    {
        const double size = style.size_px > 0.0 ? style.size_px : 1.0;
        return {size * 0.8, size * 0.2, size, size * 0.7};
    }
};

/// Width at an average ``0.5 * size_px`` per character, plus tracking: the
/// fallback when no font can be measured.
[[nodiscard]] double approx_text_width(std::string_view text, const FontStyle& style) noexcept;

/// Measures every string with ``approx_text_width``.
class ApproxTextMeasurer final : public ITextMeasurer {
public:
    [[nodiscard]] double measure_width(std::string_view text, const FontStyle& style) override
    {
        return approx_text_width(text, style);
    }
};

}  // namespace seventhslide::layout
