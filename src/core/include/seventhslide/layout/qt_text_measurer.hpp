#pragma once

// Qt Gui text metrics adapter.
// Documented Qt Gui seam - see docs/CODING.md.
// Uses FontRegistry / QtFontFaces for face paths; QFontMetricsF for widths.

#include "seventhslide/fonts/font_registry.hpp"
#include "seventhslide/layout/qt_font_faces.hpp"
#include "seventhslide/layout/text_measurer.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace seventhslide::layout {

/**
 * ``ITextMeasurer`` backed by Qt text layout (HarfBuzz) + ``FontRegistry``.
 *
 * Resolves the styled face when bold/italic, else the family; on failure falls
 * back to a ``0.5 * size`` average char width. Weight and italic come
 * from the resolved QFont (real face, or Qt synthesis when no styled file).
 *
 * Requires a ``QGuiApplication`` (tests use the offscreen QPA plugin).
 *
 * Widths are memoized per face, size and string: wrapping measures the same
 * prefixes over and over, and a rebuild re-measures the same lyrics.
 */
class QtTextMeasurer final : public ITextMeasurer {
public:
    explicit QtTextMeasurer(fonts::FontRegistry& fonts);

    [[nodiscard]] double measure_width(
        std::string_view text, const FontStyle& style) override;

    /// The resolved font's own metrics, for every style (a face that did not
    /// resolve still has the Qt fallback font paint draws with). Memoized per
    /// font.
    [[nodiscard]] FontMetrics font_metrics(const FontStyle& style) override;

    [[nodiscard]] QtFontFaces& faces() noexcept { return faces_; }
    [[nodiscard]] const QtFontFaces& faces() const noexcept { return faces_; }

    /// Drop cached application-font ids and measured widths.
    void clear_cache();

private:
    /// Cleared wholesale when it grows past this; a service never approaches it.
    static constexpr std::size_t k_max_cached_widths = 200000;

    QtFontFaces faces_;
    mutable std::mutex width_mu_;
    std::unordered_map<std::string, double> widths_;
    std::unordered_map<std::string, FontMetrics> metrics_;

    /// The Qt measurement itself (no cache).
    [[nodiscard]] double measured_width(std::string_view text, const FontStyle& style) const;
};

}  // namespace seventhslide::layout
