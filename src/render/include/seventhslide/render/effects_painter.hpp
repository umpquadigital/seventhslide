#pragma once

// Shadows, glows and soft edges round what another painter draws, and the
// blur they are made of.
//
// An effect is derived from the element's own pixels: the body is drawn into
// a layer at device resolution, its coverage is blurred, tinted and offset
// for a shadow, blurred and tinted for a glow, or turned into a mask that
// fades the body's edge. Everything happens in device pixels through the
// painter's own transform, so a raster of the element (ElementRasterCache)
// and a direct draw agree exactly, and the stage's zoom scales an effect as
// it scales the box.
//
// Seam: Qt Gui.

#include "seventhslide/canvas/parts.hpp"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRectF>

#include <functional>

namespace seventhslide::render {

/// ``image``'s coverage: its alpha as an 8-bit grey image. What an effect is
/// made of; one channel, so a blur costs a quarter of a colour one.
[[nodiscard]] QImage coverage_of(const QImage& image);

/// ``coverage`` (8-bit grey) blurred by ``radius`` pixels: three box blurs,
/// close to a Gaussian of that spread, with nothing past the edges. A radius
/// under half a pixel gives the image back.
[[nodiscard]] QImage blurred_coverage(const QImage& coverage, double radius);

/// ``coverage`` (times ``gain``, clipped) as ``color``, premultiplied: what a
/// shadow or a glow paints.
[[nodiscard]] QImage tinted_coverage(const QImage& coverage, const QColor& color, double gain = 1.0);

/// Fade ``body``'s edge inward over ``radius`` pixels.
void apply_soft_edge(QImage& body, double radius);

/**
 * Draw ``body`` with ``effects`` round it: its shadow, its glow, then the body
 * with its soft edge. ``bounds`` is what the body covers in the painter's
 * coordinates; the effects reach past it by canvas::effects_reach (in the
 * same units), which the caller's clip and raster bounds allow for. A shadow
 * falls at its angle on the slide whatever the body's turn. With no effect on,
 * ``body`` is drawn directly.
 */
void paint_with_effects(QPainter& painter, const canvas::Effects& effects, const QRectF& bounds,
                        const std::function<void(QPainter&)>& body);

/// ``body`` (words) over its text shadow, when ``shadow`` is on.
void paint_with_text_shadow(QPainter& painter, const canvas::Shadow& shadow, const QRectF& bounds,
                            const std::function<void(QPainter&)>& body);

}  // namespace seventhslide::render
