#include "seventhslide/scene/text_metrics.hpp"

#include "seventhslide/layout/chords.hpp"
#include "seventhslide/layout/overlay_layout.hpp"
#include "seventhslide/layout/word_wrap.hpp"

#include <algorithm>

namespace seventhslide::scene {
namespace {

/// The font ``line`` is set in: its type over the box's.
[[nodiscard]] layout::FontStyle line_font(const SceneTextLine& line, const SceneTextBoxStyle& box)
{
    const auto type = canvas::painted_type(box, line.type);
    layout::FontStyle font;
    font.family = type.family;
    font.size_px = type.size > 0 ? static_cast<double>(type.size) : 1.0;
    font.bold = type.bold;
    font.italic = type.italic;
    font.letter_spacing = box.letter_spacing;
    return font;
}

}  // namespace

SceneLineMetrics measure_text_line(const SceneTextLine& line,
                                   const SceneTextBoxStyle& box,
                                   const layout::ChordStyle& chords,
                                   layout::ITextMeasurer& measurer)
{
    SceneLineMetrics out;
    const auto font = line_font(line, box);
    const double size = font.size_px;
    const double factor = box.line_height > 0.0 ? box.line_height : 1.2;
    out.height = layout::line_box_height_px(size, factor);
    const auto metrics = measurer.font_metrics(font);
    // Taller line boxes centre the font's own height in them.
    const double half_leading = std::max(0.0, (out.height - metrics.height) * 0.5);

    if (line.has_chords) {
        // The chord row opens up the lyric's line box where its capitals
        // begin: line spacing and the room above the capitals stay above the
        // chords, so chord lines sit as far apart as plain lines do, and each
        // chord is the gap away from its words.
        const auto chord_font = layout::chord_font(font, chords);
        const int chord_row = layout::chord_row_px(measurer, font, chords);
        auto placed = layout::place_chord_items(line.chords.items, font, chord_font, measurer);
        out.x = std::move(placed.x);
        out.hyphens = std::move(placed.hyphens);
        out.width = placed.width;
        const double above_caps = half_leading + metrics.ascent - metrics.cap_height;
        out.height += chord_row;
        out.chord_baseline = above_caps + measurer.font_metrics(chord_font).cap_height;
        out.baseline = half_leading + chord_row + metrics.ascent;
        return out;
    }

    const auto measure = layout::bind_run_measurer(measurer, font);
    out.x.reserve(line.runs.size());
    double x = 0.0;
    for (const auto& span : line.runs) {
        out.x.push_back(x);
        x += measure(span.text, span.style);
    }
    out.width = x;
    // A row opens up for its largest text, as layout plans it, on one baseline.
    auto row_font = font;
    row_font.size_px = size * (layout::row_size_pct(line.runs) / 100.0);
    out.height = layout::line_box_height_px(row_font.size_px, factor);
    const auto row_metrics = measurer.font_metrics(row_font);
    out.baseline = layout::baseline_in_line_box(out.height, row_metrics);
    return out;
}

void measure_text_content(SceneTextContent& content, layout::ITextMeasurer& measurer)
{
    for (auto& line : content.lines) {
        if (!line.metrics) {
            line.metrics = measure_text_line(line, content.box, content.chords, measurer);
        }
    }
}

void lay_out_frame_text(SceneFrame& frame, layout::ITextMeasurer& measurer)
{
    if (frame.text) {
        measure_text_content(*frame.text, measurer);
    }
    for (auto& box : frame.overlay.boxes) {
        if (!box.layout) {
            box.layout = layout::lay_out_overlay(box, measurer);
        }
    }
    if (frame.media_stand_in && !frame.media_stand_in->label.layout) {
        auto& label = frame.media_stand_in->label;
        label.layout = layout::lay_out_overlay(label, measurer);
    }
    for (auto& object : frame.objects) {
        if (object.text && !object.text->layout) {
            object.text->layout = layout::lay_out_overlay(*object.text, measurer);
        }
    }
}

}  // namespace seventhslide::scene
