#pragma once

// Freeform shape outlines -> QPainterPath, and the one painter that fills and
// strokes a path from an object's fill and line parts. Shared by audience
// windows, thumbnails, and the editor canvas item so a shape, or a text box's
// frame, looks identical everywhere. Geometry itself lives in core
// (geom/shape_path); what paints is canvas::Fill and canvas::Line.

#include "seventhslide/canvas/object.hpp"
#include "seventhslide/geom/shape_path.hpp"

#include <QBrush>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

#include <string_view>

namespace seventhslide::render {

/// Convert a core outline (design px, box-local) to a Qt path.
[[nodiscard]] QPainterPath to_painter_path(const geom::ShapePath& path);

/// Outline for ``kind`` inside ``(0, 0, w, h)``. Adjustments are clamped.
[[nodiscard]] QPainterPath shape_painter_path(geom::ShapeKind kind, double w, double h,
                                              const geom::ShapeAdjust& adjust);

/// Parse a CSS color string (``#rrggbb``, ``rgba(...)``, name). Invalid -> invalid QColor.
[[nodiscard]] QColor shape_color(std::string_view css);

/// The pen ``line`` paints with: its colour, its width times ``scale`` (the
/// stage's zoom; 1 on an output), and its dash as Qt's patterns, which are in
/// pen widths so a dash keeps its look at any width. Mitred joins, capped so a
/// star's inner vertex does not spike far outside its box.
[[nodiscard]] QPen line_pen(const canvas::Line& line, double scale = 1.0);

/// ``gradient`` across ``bounds``: a linear one along its angle, corner to
/// corner of the box that way; a radial one from its centre to the farthest
/// corner.
[[nodiscard]] QBrush gradient_brush(const canvas::Gradient& gradient, const QRectF& bounds);

/**
 * Fill and stroke ``path`` from the object's parts: the fill when it paints
 * (canvas::fill_paints; a gradient across the path's bounds, a picture fill
 * paints ``picture`` fitted and clipped to the path), then the
 * line, centred on the outline (PowerPoint behaviour).
 *
 * Assumes the caller already applied the object's translate / rotate / opacity.
 */
void paint_fill_and_line(QPainter& painter, const QPainterPath& path, const canvas::Fill& fill,
                         const canvas::Line& line, double line_scale = 1.0,
                         const QImage* picture = nullptr);

/// The frame behind a box's words: ``rect`` filled and outlined from
/// ``fill`` and ``line`` (``picture`` is a picture fill's, decoded), with
/// ``effects`` round it. What a text box's object, a theme's body box and
/// the stage's frame item all paint.
void paint_rect_frame(QPainter& painter, const QRectF& rect, const canvas::Fill& fill,
                      const canvas::Line& line, const canvas::Effects& effects,
                      const QImage* picture = nullptr);

/// How far a line reaches past the outline it is centred on: twice its
/// width, for a mitre (the miter limit is four half-widths).
[[nodiscard]] double line_reach(const canvas::Line& line) noexcept;

/// How far an object's line reaches past its box: ``line_reach``, or for an
/// open outline with an arrowhead or dot, what that can take beyond its end.
[[nodiscard]] double shape_reach(geom::ShapeKind kind, const geom::ShapeAdjust& adjust,
                                 const canvas::Line& line) noexcept;

/// ``paint_fill_and_line`` on the outline of ``kind`` inside ``(0, 0, w, h)``.
/// ``picture`` is the fill's picture, decoded, when it is one (null paints
/// nothing for a picture fill).
void paint_shape_outline(QPainter& painter, geom::ShapeKind kind, double w, double h,
                         const geom::ShapeAdjust& adjust, const canvas::Fill& fill,
                         const canvas::Line& line, double line_scale = 1.0,
                         const QImage* picture = nullptr);

} // namespace seventhslide::render
