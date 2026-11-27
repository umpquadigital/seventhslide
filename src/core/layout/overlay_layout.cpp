#include "seventhslide/layout/overlay_layout.hpp"

#include "seventhslide/layout/chords.hpp"
#include "seventhslide/layout/word_wrap.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace seventhslide::layout {
namespace {

/// The size a run counts as in its row's height: the box size at its scale.
[[nodiscard]] double run_size_px(const OverlayBox& box, const model::TextStyle& style) noexcept
{
    return style.size_px > 0.0 ? style.size_px : box.font_size * (std::max(1, style.size_pct) / 100.0);
}

/// ``spans`` (one logical line, sizes filled in) as rows of words with their
/// chords over them, the chord row on top of each row's own line box
/// (``wrap_chord_line``, as a song's chord lines are). ``line_size`` is the
/// style with the line's size: chord rows have a fixed height, so no cell has
/// another.
void lay_out_chord_line(const OverlayBox& box,
                        const model::RichLine& spans,
                        const model::TextStyle& line_size,
                        double line_height,
                        double max_width,
                        ITextMeasurer& measurer,
                        double& y,
                        OverlayLayout& out)
{
    FontStyle lyric;
    lyric.family = box.font_family;
    lyric.size_px = run_size_px(box, line_size);
    lyric.bold = box.bold;
    lyric.italic = box.italic;
    lyric.letter_spacing = box.letter_spacing;
    const auto chord = chord_font(lyric, box.chords);
    const auto wrapped = wrap_chord_line(parse_chord_line(model::format_inline_markup(spans)),
                                         lyric, box.chords, measurer, max_width);
    const auto& items = wrapped.line.items;

    const double lyric_h = line_box_height_px(lyric.size_px, line_height);
    const auto metrics = measurer.font_metrics(lyric);
    const double half_leading = std::max(0.0, (lyric_h - metrics.height) * 0.5);
    const int chord_row = chord_row_px(measurer, lyric, box.chords);
    const double chord_baseline =
        half_leading + metrics.ascent - metrics.cap_height + measurer.font_metrics(chord).cap_height;
    for (const auto& [begin, end] : wrapped.rows) {
        const auto placed =
            place_chord_items(std::span(items).subspan(begin, end - begin), lyric, chord, measurer);
        OverlayRow row;
        row.y = y;
        row.height = lyric_h + chord_row;
        row.width = placed.width;
        const double baseline = half_leading + chord_row + metrics.ascent;
        model::TextStyle last_style;
        for (std::size_t k = begin; k < end; ++k) {
            const auto& item = items[k];
            if (item.kind != ChordItemKind::Cell) {
                continue;
            }
            const double x = placed.x[k - begin];
            if (item.cell.chord && !item.cell.chord->empty()) {
                row.chords.push_back({*item.cell.chord, x, chord_baseline, chord.size_px});
            }
            if (!item.cell.syllable.empty()) {
                auto style = item.cell.style;
                style.size_pct = line_size.size_pct;
                style.size_px = line_size.size_px;
                style.baseline_rise_pct = 0;
                row.runs.push_back({item.cell.syllable, style, x, baseline});
                last_style = style;
            }
        }
        for (const double hx : placed.hyphens) {
            row.runs.push_back({"-", last_style, hx, baseline});
        }
        y += row.height;
        out.rows.push_back(std::move(row));
    }
}

/// The mark a paragraph starts with: a disc, or its number.
[[nodiscard]] std::string bullet_text(canvas::Bullets bullets, std::size_t index)
{
    switch (bullets) {
    case canvas::Bullets::None:
        return {};
    case canvas::Bullets::Disc:
        return "\u2022";
    case canvas::Bullets::Number:
        return std::to_string(index + 1) + ".";
    }
    return {};
}

/// ``out``'s rows, laid out at a column's width, flowed down the box's
/// columns in turn: a column takes rows while they fit the box's height (and
/// always one), the last takes the rest. Its height is the tallest column's.
void flow_into_columns(const OverlayBox& box, OverlayLayout& out)
{
    const int columns = overlay_columns(box);
    if (columns <= 1 || out.rows.empty()) {
        return;
    }
    const auto& para = box.paragraph;
    const double avail_h = box.h - std::max(0.0, para.margins.top) - std::max(0.0, para.margins.bottom);
    const double step = overlay_column_width(box) + std::max(0.0, para.column_gap);
    int column = 0;
    int in_column = 0;
    double top = out.rows.front().y;
    double height = 0.0;
    for (auto& row : out.rows) {
        if (in_column > 0 && column + 1 < columns && row.y + row.height - top > avail_h) {
            ++column;
            in_column = 0;
            top = row.y;
        }
        row.y -= top;
        row.x = column * step;
        ++in_column;
        height = std::max(height, row.y + row.height);
    }
    out.height = height;
}

/// ``box`` laid out as it is, autofit aside.
[[nodiscard]] OverlayLayout lay_out_overlay_as_is(const OverlayBox& box, ITextMeasurer& measurer)
{
    OverlayLayout out;
    if (box.lines.empty() || box.w <= 0 || box.h <= 0) {
        return out;
    }
    const double box_line_height = box.line_height > 0 ? box.line_height : canvas::k_default_line_height;
    const double line_gap = overlay_line_gap_px(box);
    const auto& para = box.paragraph;
    // Box bold and italic are already on the runs (``styled_line``).
    FontStyle base;
    base.family = box.font_family;
    base.size_px = box.font_size;
    base.letter_spacing = box.letter_spacing;
    const auto measure = bind_run_measurer(measurer, base);
    const double max_width = overlay_column_width(box);

    double y = 0.0;
    std::size_t numbered = 0;
    double after_previous = 0.0;
    for (std::size_t li = 0; li < box.lines.size(); ++li) {
        const auto& line = box.lines[li];
        // What this paragraph sets for itself, else the box's.
        const model::LineFormat format = line.empty() ? model::LineFormat{} : line.front().style.line;
        const auto own = [&](int pct, double box_value) {
            return pct >= 0 ? box.font_size * pct / 100.0 : std::max(0.0, box_value);
        };
        const double line_height = format.line_pct > 0 ? format.line_pct / 100.0 : box_line_height;
        const double indent = own(format.indent_pct, para.indent);
        if (li > 0) {
            y += line_gap + after_previous + own(format.before_pct, para.space_before);
            if (li < box.gaps_above.size()) {
                y += box.gaps_above[li];
            }
        }
        after_previous = own(format.after_pct, para.space_after);
        // A run with no size of its own takes the line's first size.
        const int line_scale = !line.empty() && line.front().style.size_pct > 0
                                   ? line.front().style.size_pct
                                   : 100;
        model::RichLine spans;
        spans.reserve(line.size());
        for (const auto& span : line) {
            if (span.text.empty()) {
                continue;
            }
            auto copy = span;
            if (copy.style.size_pct <= 0) {
                copy.style.size_pct = line_scale;
            }
            spans.push_back(std::move(copy));
        }

        // A chord line keeps the margins; a bullet or an indent is not for a
        // line with chords over it.
        if (box.show_chords && has_chords(model::plain_text(spans))) {
            const std::size_t first_row = out.rows.size();
            lay_out_chord_line(box, spans, spans.front().style, line_height, max_width, measurer,
                               y, out);
            for (std::size_t r = first_row; r < out.rows.size(); ++r) {
                out.rows[r].align = format.align;
            }
            continue;
        }
        const auto words = StyledWords::spans(spans);
        if (words.empty()) {
            OverlayRow row;
            row.y = y;
            row.align = format.align;
            model::TextStyle blank;
            blank.size_pct = line_scale;
            blank.size_px = line.empty() ? 0.0 : line.front().style.size_px;
            row.height = line_box_height_px(run_size_px(box, blank), line_height);
            y += row.height;
            out.rows.push_back(std::move(row));
            continue;
        }
        // A bullet hangs: the mark at the indent, every row of the paragraph
        // starting after it. Numbers count the paragraphs that get one.
        const std::string mark = bullet_text(para.bullets, numbered);
        if (!mark.empty()) {
            ++numbered;
        }
        model::TextStyle mark_style = spans.front().style;
        mark_style.baseline_rise_pct = 0;
        const double hang = mark.empty()
            ? 0.0
            : measure(mark, mark_style) + run_size_px(box, mark_style) * 0.4;
        const double first_offset = indent + hang;
        const auto rows = wrap_styled_rows(words, {0, words.size()}, measure,
                                           std::max(1.0, max_width - hang),
                                           std::max(1.0, max_width - first_offset));
        for (std::size_t ri = 0; ri < rows.size(); ++ri) {
            const auto& pieces = rows[ri];
            OverlayRow row;
            row.y = y;
            row.align = format.align;
            // Every run on one baseline, set by the box's font at the row's
            // largest size, as in a song's box.
            double size = 1.0;
            for (const auto& piece : pieces) {
                size = std::max(size, run_size_px(box, piece.style));
            }
            row.height = line_box_height_px(size, line_height);
            auto row_font = base;
            row_font.size_px = size;
            row_font.bold = box.bold;
            row_font.italic = box.italic;
            const double baseline =
                baseline_in_line_box(row.height, measurer.font_metrics(row_font));
            double x = ri == 0 ? first_offset : hang;
            row.runs.reserve(pieces.size() + 1);
            if (ri == 0 && !mark.empty()) {
                OverlayRun bullet;
                bullet.text = mark;
                bullet.style = mark_style;
                bullet.x = indent;
                bullet.baseline = baseline;
                row.runs.push_back(std::move(bullet));
            }
            for (const auto& piece : pieces) {
                OverlayRun placed;
                placed.text = piece.text;
                placed.style = piece.style;
                placed.x = x;
                const double rise =
                    box.font_size * (std::max(0, piece.style.baseline_rise_pct) / 100.0);
                placed.baseline = baseline - rise;
                x += measure(piece.text, piece.style);
                row.runs.push_back(std::move(placed));
            }
            row.width = x;
            y += row.height;
            out.rows.push_back(std::move(row));
        }
    }
    out.height = y;
    flow_into_columns(box, out);
    return out;
}

/// The whole-pixel size ``font_size`` scaled by ``scale`` comes to.
[[nodiscard]] int scaled_font_size(int font_size, double scale)
{
    return std::max(1, static_cast<int>(std::lround(font_size * scale)));
}

/// ``box`` with its type scaled by ``scale``: what autofit lays out.
[[nodiscard]] OverlayBox scaled_box(const OverlayBox& box, double scale)
{
    OverlayBox copy = box;
    copy.font_size = scaled_font_size(box.font_size, scale);
    copy.letter_spacing = box.letter_spacing * scale;
    for (auto& line : copy.lines) {
        for (auto& span : line) {
            if (span.style.size_px > 0.0) {
                span.style.size_px *= scale;
            }
        }
    }
    return copy;
}

}  // namespace

int overlay_columns(const OverlayBox& box) noexcept
{
    return std::clamp(box.paragraph.columns, 1, k_max_columns);
}

double overlay_column_width(const OverlayBox& box) noexcept
{
    const auto& para = box.paragraph;
    const double inner = box.w - para.margins.left - para.margins.right;
    const int columns = overlay_columns(box);
    const double gaps = std::max(0.0, para.column_gap) * (columns - 1);
    return std::max(1.0, (inner - gaps) / columns);
}

double overlay_line_gap_px(const OverlayBox& box) noexcept
{
    return box.line_gap > 0 ? (static_cast<double>(box.line_gap) / 100.0) * box.font_size : 0.0;
}

OverlayLayout lay_out_overlay(const OverlayBox& box, ITextMeasurer& measurer)
{
    OverlayLayout out = lay_out_overlay_as_is(box, measurer);
    if (box.paragraph.autofit != canvas::Autofit::Shrink || out.rows.empty()) {
        return out;
    }
    // Shrink to fit: the largest of the type's steps whose rows fit the box's
    // height (the margins taken out), else the smallest. Each step is 5%
    // smaller than the one before, rounded to a whole pixel, down to the first
    // at 8 px or under; a step that rounds to the size before it is passed
    // over. Smaller type never needs more height, so steps 0, 1, 3, 7, ... are
    // tried until one fits, and the gap below it is halved: a box a step or
    // two too full is laid out once or twice more. The scale reported is the
    // one the rounded size came out at.
    constexpr double k_step = 0.95;
    constexpr int k_smallest = 8;
    const double avail_h =
        box.h - std::max(0.0, box.paragraph.margins.top) - std::max(0.0, box.paragraph.margins.bottom);
    if (out.height <= avail_h || box.font_size <= k_smallest) {
        return out;
    }
    std::vector<double> scales;
    double scale = 1.0;
    for (int size = box.font_size; size > k_smallest;) {
        scale *= k_step;
        const int smaller = scaled_font_size(box.font_size, scale);
        if (smaller != size) {
            size = smaller;
            scales.push_back(scale);
        }
    }
    const auto lay_out_step = [&](std::size_t step) {
        const OverlayBox smaller = scaled_box(box, scales[step]);
        OverlayLayout laid = lay_out_overlay_as_is(smaller, measurer);
        laid.font_scale = static_cast<double>(smaller.font_size) / box.font_size;
        return laid;
    };
    // Every step below ``lo`` is too big. The steps tried stop at one that
    // fits, ``hi``, which ``out`` is then laid out at, or at the last, which is
    // taken fitting or not.
    const std::size_t last = scales.size() - 1;
    std::size_t lo = 0;
    std::size_t hi = 0;
    for (std::size_t probe = 0;; probe = std::min(last, probe * 2 + 1)) {
        OverlayLayout laid = lay_out_step(probe);
        const bool fits = laid.height <= avail_h;
        if (fits || probe == last) {
            out = std::move(laid);
            if (!fits) {
                return out;
            }
            hi = probe;
            break;
        }
        lo = probe + 1;
    }
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        OverlayLayout laid = lay_out_step(mid);
        if (laid.height <= avail_h) {
            hi = mid;
            out = std::move(laid);
        } else {
            lo = mid + 1;
        }
    }
    return out;
}

}  // namespace seventhslide::layout
