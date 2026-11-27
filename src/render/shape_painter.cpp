#include "seventhslide/render/shape_painter.hpp"

#include "seventhslide/render/css_color.hpp"
#include "seventhslide/render/effects_painter.hpp"
#include "seventhslide/render/picture_painter.hpp"

#include <QBrush>
#include <QLineF>
#include <QLinearGradient>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRadialGradient>
#include <QString>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace seventhslide::render {

QPainterPath to_painter_path(const geom::ShapePath& path)
{
    QPainterPath out;
    // Where parts of an outline overlap (a cloud's bubbles) they are all inside.
    out.setFillRule(Qt::WindingFill);
    for (const auto& sub : path.subpaths) {
        out.moveTo(sub.start.x, sub.start.y);
        for (const auto& seg : sub.segments) {
            if (seg.kind == geom::SegmentKind::Cubic) {
                out.cubicTo(seg.c1.x, seg.c1.y, seg.c2.x, seg.c2.y, seg.to.x, seg.to.y);
            } else {
                out.lineTo(seg.to.x, seg.to.y);
            }
        }
        if (sub.closed) {
            out.closeSubpath();
        }
    }
    return out;
}

QPainterPath shape_painter_path(geom::ShapeKind kind, double w, double h,
                                const geom::ShapeAdjust& adjust)
{
    return to_painter_path(geom::build_shape_path(kind, w, h, adjust));
}

QColor shape_color(std::string_view css)
{
    return css_color(css);
}

QPen line_pen(const canvas::Line& line, double scale)
{
    QPen pen(shape_color(line.color));
    pen.setWidthF(line.width * scale);
    pen.setJoinStyle(Qt::MiterJoin);
    // A mitre on a star's inner vertex spikes far outside the box; cap it.
    pen.setMiterLimit(4.0);
    switch (line.dash) {
    case canvas::LineDash::Solid:
        pen.setStyle(Qt::SolidLine);
        break;
    case canvas::LineDash::Dash:
        pen.setStyle(Qt::DashLine);
        break;
    case canvas::LineDash::Dot:
        pen.setStyle(Qt::DotLine);
        break;
    case canvas::LineDash::DashDot:
        pen.setStyle(Qt::DashDotLine);
        break;
    }
    return pen;
}

QBrush gradient_brush(const canvas::Gradient& gradient, const QRectF& bounds)
{
    QGradientStops stops;
    for (const auto& stop : gradient.stops) {
        stops.append({std::clamp(stop.at, 0.0, 1.0), shape_color(stop.color)});
    }
    if (gradient.kind == canvas::GradientKind::Radial) {
        // From its centre out to the farthest corner.
        const QPointF centre(bounds.left() + bounds.width() * std::clamp(gradient.center_x, 0.0, 1.0),
                             bounds.top() + bounds.height() * std::clamp(gradient.center_y, 0.0, 1.0));
        const double across = std::max(centre.x() - bounds.left(), bounds.right() - centre.x());
        const double down = std::max(centre.y() - bounds.top(), bounds.bottom() - centre.y());
        QRadialGradient radial(centre, std::hypot(across, down));
        radial.setStops(stops);
        return QBrush(radial);
    }
    // Along the angle (0 left to right, 90 top to bottom), end to end across
    // the box: its corners are where the colours start and end.
    const double rad = gradient.angle * std::numbers::pi / 180.0;
    const QPointF dir(std::cos(rad), std::sin(rad));
    const double half = (std::abs(dir.x()) * bounds.width() + std::abs(dir.y()) * bounds.height()) * 0.5;
    QLinearGradient linear(bounds.center() - dir * half, bounds.center() + dir * half);
    linear.setStops(stops);
    return QBrush(linear);
}

void paint_fill_and_line(QPainter& painter, const QPainterPath& path, const canvas::Fill& fill,
                         const canvas::Line& line, double line_scale, const QImage* picture)
{
    if (path.isEmpty()) {
        return;
    }
    const bool picture_fill = fill.kind == canvas::FillKind::Picture;
    const bool gradient_fill = fill.kind == canvas::FillKind::Gradient && canvas::fill_paints(fill);
    const QColor fill_color = canvas::fill_paints(fill) && !picture_fill && !gradient_fill
                                  ? shape_color(fill.color)
                                  : QColor();
    const bool has_fill = gradient_fill || (fill_color.isValid() && fill_color.alpha() > 0);
    const QPen pen = canvas::line_paints(line) ? line_pen(line, line_scale) : QPen(Qt::NoPen);
    const bool has_line = pen.style() != Qt::NoPen && pen.color().isValid()
        && pen.color().alpha() > 0 && pen.widthF() > 0.0;
    const bool has_picture = picture_fill && picture != nullptr && !picture->isNull();
    if (!has_fill && !has_line && !has_picture) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (has_picture) {
        paint_picture_fill(painter, path, *picture, fill.picture);
    }
    if (has_fill) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient_fill ? gradient_brush(fill.gradient, path.boundingRect())
                                       : QBrush(fill_color));
        painter.drawPath(path);
    }
    if (has_line) {
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
    painter.restore();
}

double line_reach(const canvas::Line& line) noexcept
{
    return canvas::line_paints(line) ? std::max(0.0, line.width) * 2.0 : 0.0;
}

double shape_reach(geom::ShapeKind kind, const geom::ShapeAdjust& adjust,
                   const canvas::Line& line) noexcept
{
    const bool ends = geom::is_open(kind, adjust)
                      && (line.start != canvas::LineEnd::None || line.end != canvas::LineEnd::None);
    return ends && canvas::line_paints(line) ? std::max(1.0, line.width) * 5.0 : line_reach(line);
}

void paint_rect_frame(QPainter& painter, const QRectF& rect, const canvas::Fill& fill,
                      const canvas::Line& line, const canvas::Effects& effects,
                      const QImage* picture)
{
    if (!canvas::fill_paints(fill) && !canvas::line_paints(line)) {
        return;
    }
    const double reach = line_reach(line);
    paint_with_effects(painter, effects, rect.adjusted(-reach, -reach, reach, reach),
                       [&](QPainter& p) {
                           QPainterPath path;
                           path.addRect(rect);
                           paint_fill_and_line(p, path, fill, line, 1.0, picture);
                       });
}

namespace {

/// An open outline's end at ``tip``, coming from ``from``: an arrowhead or a
/// dot as ``end`` says, in the line's colour and sized by its width.
void paint_line_end(QPainter& painter, canvas::LineEnd end, const QPointF& from,
                    const QPointF& tip, const canvas::Line& line, double line_scale)
{
    if (end == canvas::LineEnd::None) {
        return;
    }
    const double size = std::max(1.0, line.width) * line_scale;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(shape_color(line.color));
    if (end == canvas::LineEnd::Dot) {
        const double r = size * 1.6;
        painter.drawEllipse(tip, r, r);
    } else {
        const QLineF along(from, tip);
        if (along.length() > 0.0) {
            const QPointF unit = (tip - from) / along.length();
            const QPointF normal(-unit.y(), unit.x());
            const double length = size * 4.0;
            const double half = size * 1.6;
            const QPointF base = tip - unit * length;
            painter.drawPolygon(QPolygonF{tip, base + normal * half, base - normal * half});
        }
    }
    painter.restore();
}

}  // namespace

void paint_shape_outline(QPainter& painter, geom::ShapeKind kind, double w, double h,
                         const geom::ShapeAdjust& adjust, const canvas::Fill& fill,
                         const canvas::Line& line, double line_scale, const QImage* picture)
{
    if (!geom::is_open(kind, adjust)) {
        paint_fill_and_line(painter, shape_painter_path(kind, w, h, adjust), fill, line,
                            line_scale, picture);
        return;
    }
    // An open outline is a line: no fill, and its ends as the line has them.
    const auto outline = geom::build_shape_path(kind, w, h, adjust);
    paint_fill_and_line(painter, to_painter_path(outline), canvas::Fill{.kind = canvas::FillKind::None},
                        line, line_scale);
    if (outline.empty() || outline.subpaths.front().segments.empty() || !canvas::line_paints(line)) {
        return;
    }
    const auto& sub = outline.subpaths.front();
    const auto point = [](const geom::ShapePoint& p) { return QPointF(p.x, p.y); };
    // Each end's way in: the first and last segments' control points, or the
    // point before.
    const auto& first = sub.segments.front();
    QPointF start_from = point(first.kind == geom::SegmentKind::Cubic ? first.c1 : first.to);
    if (start_from == point(sub.start)) {
        start_from = point(first.to);
    }
    const auto& last = sub.segments.back();
    const QPointF tip = point(last.to);
    QPointF end_from = sub.segments.size() > 1 ? point(sub.segments[sub.segments.size() - 2].to)
                                               : point(sub.start);
    if (last.kind == geom::SegmentKind::Cubic && point(last.c2) != tip) {
        end_from = point(last.c2);
    }
    paint_line_end(painter, line.start, start_from, point(sub.start), line, line_scale);
    paint_line_end(painter, line.end, end_from, tip, line, line_scale);
}

} // namespace seventhslide::render
