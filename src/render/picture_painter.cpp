#include "seventhslide/render/picture_painter.hpp"

#include "seventhslide/render/css_color.hpp"
#include "seventhslide/render/object_fit.hpp"

#include <QColor>
#include <QHash>
#include <QString>

#include <algorithm>
#include <cmath>

namespace seventhslide::render {

QImage recolored(const QImage& picture, canvas::Recolor recolor, std::string_view tint)
{
    if (recolor == canvas::Recolor::None || picture.isNull()) {
        return picture;
    }
    // Recolouring walks every pixel of the decoded picture, and the stage
    // repaints on every drag: the last few are kept by the picture's
    // identity and the recolour asked for.
    static thread_local QHash<QString, QImage> kept;
    const QString key = QString::number(picture.cacheKey()) + QLatin1Char('\x1f')
        + QString::number(static_cast<int>(recolor)) + QLatin1Char('\x1f')
        + QString::fromUtf8(tint);
    if (const auto it = kept.constFind(key); it != kept.constEnd()) {
        return *it;
    }
    if (kept.size() >= 8) {
        kept.clear();
    }
    QImage out = picture.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QColor color = recolor == canvas::Recolor::Tint ? css_color(tint) : QColor();
    const double tr = color.isValid() ? color.redF() : 1.0;
    const double tg = color.isValid() ? color.greenF() : 1.0;
    const double tb = color.isValid() ? color.blueF() : 1.0;
    for (int y = 0; y < out.height(); ++y) {
        auto* px = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgb p = px[x];
            // Premultiplied, so the grey is premultiplied too.
            const int grey = static_cast<int>(0.299 * qRed(p) + 0.587 * qGreen(p) + 0.114 * qBlue(p) + 0.5);
            px[x] = qRgba(static_cast<int>(grey * tr + 0.5), static_cast<int>(grey * tg + 0.5),
                          static_cast<int>(grey * tb + 0.5), qAlpha(p));
        }
    }
    return *kept.insert(key, out);
}

QImage shrunk_to_show(const QImage& picture, double width, double height)
{
    if (picture.isNull() || width <= 0.0 || height <= 0.0) {
        return picture;
    }
    const double factor = std::max(width / picture.width(), height / picture.height());
    if (factor >= 0.75) {
        return picture;
    }
    return picture.scaled(static_cast<int>(std::ceil(picture.width() * factor)),
                          static_cast<int>(std::ceil(picture.height() * factor)),
                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void paint_picture(QPainter& painter, const QImage& picture, const QRectF& box,
                   std::string_view fit, const canvas::Crop& crop, const canvas::Picture& adjust)
{
    if (picture.isNull() || box.isEmpty()) {
        return;
    }
    // The crop in source pixels; what is left is what gets fitted.
    const auto frac = [](double f) { return std::clamp(f, 0.0, canvas::k_max_crop); };
    const double sx = picture.width() * frac(crop.left);
    const double sy = picture.height() * frac(crop.top);
    const double sw = picture.width() * (1.0 - frac(crop.left) - frac(crop.right));
    const double sh = picture.height() * (1.0 - frac(crop.top) - frac(crop.bottom));
    if (sw <= 0.0 || sh <= 0.0) {
        return;
    }
    const ObjectFitPlacement place =
        object_fit(fit, static_cast<int>(std::lround(sw)), static_cast<int>(std::lround(sh)),
                   box.x(), box.y(), box.width(), box.height());
    if (place.empty()) {
        return;
    }
    const QImage shown = recolored(picture, adjust.recolor, adjust.tint);
    painter.save();
    painter.setClipRect(box, Qt::IntersectClip);
    const double transparency = std::clamp(adjust.transparency, 0.0, 1.0);
    if (transparency > 0.0) {
        painter.setOpacity(painter.opacity() * (1.0 - transparency));
    }
    painter.drawImage(QRectF(place.dst_x, place.dst_y, place.dst_w, place.dst_h), shown,
                      QRectF(sx + place.src_x, sy + place.src_y, place.src_w, place.src_h));
    painter.restore();
}

void paint_picture_fill(QPainter& painter, const QPainterPath& path, const QImage& picture,
                        const canvas::PictureFill& fill)
{
    if (path.isEmpty() || picture.isNull()) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipPath(path, Qt::IntersectClip);
    paint_picture(painter, picture, path.boundingRect(), fill.fit, fill.crop);
    painter.restore();
}

}  // namespace seventhslide::render
