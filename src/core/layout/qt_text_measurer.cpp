#include "seventhslide/layout/qt_text_measurer.hpp"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QString>

#include <stdexcept>

namespace seventhslide::layout {
namespace {

/// Cache key: the resolved font (all ``measured_width`` reads from ``style``,
/// the approx fallback included), then the text itself.
void fill_width_key(std::string& key, const FontStyle& style, std::string_view text)
{
    key.clear();
    QtFontFaces::append_font_key(key, style);
    key += '\x1f';
    key += text;
}

}  // namespace

QtTextMeasurer::QtTextMeasurer(fonts::FontRegistry& fonts)
    : faces_(fonts)
{
    if (QGuiApplication::instance() == nullptr) {
        throw std::logic_error(
            "QtTextMeasurer requires a QGuiApplication (use QT_QPA_PLATFORM=offscreen "
            "in headless tests)");
    }
}

double QtTextMeasurer::measure_width(std::string_view text, const FontStyle& style)
{
    if (text.empty()) {
        return 0.0;
    }

    // Reused per thread: a cache hit (the common case) allocates nothing.
    static thread_local std::string key;
    fill_width_key(key, style, text);
    {
        const std::lock_guard<std::mutex> lock(width_mu_);
        if (const auto it = widths_.find(key); it != widths_.end()) {
            return it->second;
        }
    }

    const double width = measured_width(text, style);
    {
        const std::lock_guard<std::mutex> lock(width_mu_);
        if (widths_.size() >= k_max_cached_widths) {
            widths_.clear();
        }
        widths_.emplace(key, width);
    }
    return width;
}

double QtTextMeasurer::measured_width(std::string_view text, const FontStyle& style) const
{
    const ResolvedQtFont face = faces_.resolve(style);
    if (!face.resolved) {
        return approx_text_width(text, style);
    }

    const QFontMetricsF metrics(face.font);
    const QString qtext = QString::fromUtf8(text);
    double advance = metrics.horizontalAdvance(qtext);
    if (!(advance > 0.0) && !qtext.isEmpty()) {
        return approx_text_width(text, style);
    }
    if (face.needs_synthetic_embolden()) {
        advance += face.synthetic_embolden_px;
    }
    return advance;
}

FontMetrics QtTextMeasurer::font_metrics(const FontStyle& style)
{
    static thread_local std::string key;
    key.clear();
    QtFontFaces::append_font_key(key, style);
    {
        const std::lock_guard<std::mutex> lock(width_mu_);
        if (const auto it = metrics_.find(key); it != metrics_.end()) {
            return it->second;
        }
    }
    const FontMetrics out = vertical_metrics(faces_.resolve(style).font);
    {
        const std::lock_guard<std::mutex> lock(width_mu_);
        metrics_.emplace(key, out);
    }
    return out;
}

void QtTextMeasurer::clear_cache()
{
    faces_.clear_cache();
    const std::lock_guard<std::mutex> lock(width_mu_);
    widths_.clear();
    metrics_.clear();
}

}  // namespace seventhslide::layout
