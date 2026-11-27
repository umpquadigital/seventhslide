#pragma once

// A picture in a box, as a picture object or a picture fill paints it: the
// crop taken off its edges, the rest fitted (contain / cover / fill), then
// recoloured and made see-through as asked. Shared by the output, the
// thumbnails and the stage's picture item. Seam: Qt Gui.

#include "seventhslide/canvas/parts.hpp"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>

#include <string_view>

namespace seventhslide::render {

/// ``picture`` recoloured: itself for none, its grey for grayscale, its grey
/// in ``tint`` for a tint (an unreadable tint colour leaves it grey).
[[nodiscard]] QImage recolored(const QImage& picture, canvas::Recolor recolor,
                               std::string_view tint);

/// ``picture`` brought down to about ``width`` x ``height`` device pixels
/// (it keeps its shape, never grows), for a preview a good deal smaller than
/// the file: drawing it far down in one step samples it and leaves it jagged.
[[nodiscard]] QImage shrunk_to_show(const QImage& picture, double width, double height);

/**
 * Draw ``picture`` into ``box`` (the painter's coordinates): ``crop`` taken
 * off its edges, the rest placed by ``fit``, through ``adjust``'s recolour
 * and transparency. Nothing paints outside ``box``.
 */
void paint_picture(QPainter& painter, const QImage& picture, const QRectF& box,
                   std::string_view fit, const canvas::Crop& crop,
                   const canvas::Picture& adjust = {});

/// ``fill``'s picture (already decoded as ``picture``) inside ``path``: fitted
/// to the path's bounds and clipped to it.
void paint_picture_fill(QPainter& painter, const QPainterPath& path, const QImage& picture,
                        const canvas::PictureFill& fill);

}  // namespace seventhslide::render
