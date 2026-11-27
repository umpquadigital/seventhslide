#include <seventhslide/render/table_painter.hpp>

#include <seventhslide/layout/overlays.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/render/css_color.hpp>
#include <seventhslide/render/text_painter.hpp>

#include <QPen>
#include <QRectF>

#include <algorithm>

namespace seventhslide::render {

namespace {

/// ``color`` at ``alpha`` (0 to 1) of its own.
[[nodiscard]] QColor faded(QColor color, double alpha)
{
    color.setAlphaF(std::clamp(color.alphaF() * alpha, 0.0, 1.0));
    return color;
}

}  // namespace

void paint_table_object(QPainter& painter, const canvas::Object& table, const PaintContext& ctx)
{
    const auto& t = table.table;
    if (table.w <= 0 || table.h <= 0 || t.rows <= 0 || t.columns <= 0) {
        return;
    }
    const double cell_w = table.w / t.columns;
    const double cell_h = table.h / t.rows;
    const QColor border = css_color_or(t.border_color, QColor(Qt::white));
    const QRectF whole(0, 0, table.w, table.h);
    if (table.fill.kind == canvas::FillKind::Solid && canvas::fill_paints(table.fill)) {
        painter.fillRect(whole, css_color(table.fill.color));
    }
    const double pad = std::max(4.0, table.font_size * 0.3);
    for (int r = 0; r < t.rows; ++r) {
        const bool header = t.header && r == 0;
        const QRectF row(0, r * cell_h, table.w, cell_h);
        // The header in the border's colour, banded rows a little of it.
        if (header) {
            painter.fillRect(row, faded(border, 0.3));
        } else if (t.banding && (r - (t.header ? 1 : 0)) % 2 == 1) {
            painter.fillRect(row, faded(border, 0.12));
        }
        for (int c = 0; c < t.columns; ++c) {
            const auto at = static_cast<std::size_t>(r) * static_cast<std::size_t>(t.columns)
                            + static_cast<std::size_t>(c);
            if (at >= t.cells.size() || t.cells[at].empty()) {
                continue;
            }
            // A cell is a text box: wrapped, weighted and clipped as one is.
            layout::OverlayBox cell;
            static_cast<canvas::BoxType&>(cell) = table;
            cell.x = c * cell_w;
            cell.y = r * cell_h;
            cell.w = cell_w;
            cell.h = cell_h;
            cell.paragraph.margins.left = pad;
            cell.paragraph.margins.right = pad;
            for (const auto& line : model::parse_markup_lines(t.cells[at])) {
                cell.lines.push_back(
                    layout::styled_line(line, table.bold || header, table.italic, table.underline));
            }
            paint_overlay_box(painter, cell, ctx);
        }
    }
    if (t.border_width > 0 && border.alpha() > 0) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(border);
        pen.setWidthF(t.border_width);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        for (int c = 1; c < t.columns; ++c) {
            painter.drawLine(QPointF(c * cell_w, 0), QPointF(c * cell_w, table.h));
        }
        for (int r = 1; r < t.rows; ++r) {
            painter.drawLine(QPointF(0, r * cell_h), QPointF(table.w, r * cell_h));
        }
        painter.drawRect(whole);
        painter.restore();
    }
}

}  // namespace seventhslide::render
