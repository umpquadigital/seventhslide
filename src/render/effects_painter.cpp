#include "seventhslide/render/effects_painter.hpp"

#include "seventhslide/render/css_color.hpp"

#include <QPaintDevice>
#include <QPoint>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

namespace seventhslide::render {
namespace {

/// One box blur of half-width ``w`` along the rows of ``src`` (8-bit grey)
/// into ``dst``, nothing past the edges.
void box_blur_rows(const QImage& src, QImage& dst, int w)
{
    const int width = src.width();
    const int height = src.height();
    const int span = 2 * w + 1;
    for (int y = 0; y < height; ++y) {
        const uchar* in = src.constScanLine(y);
        uchar* out = dst.scanLine(y);
        int sum = 0;
        for (int x = 0; x <= std::min(w, width - 1); ++x) {
            sum += in[x];
        }
        for (int x = 0; x < width; ++x) {
            out[x] = static_cast<uchar>((sum + span / 2) / span);
            const int leaving = x - w;
            const int entering = x + w + 1;
            if (leaving >= 0) {
                sum -= in[leaving];
            }
            if (entering < width) {
                sum += in[entering];
            }
        }
    }
}

/// The same down the columns.
void box_blur_columns(const QImage& src, QImage& dst, int w)
{
    const int width = src.width();
    const int height = src.height();
    const int span = 2 * w + 1;
    std::vector<int> sum(static_cast<std::size_t>(width), 0);
    for (int y = 0; y <= std::min(w, height - 1); ++y) {
        const uchar* in = src.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            sum[static_cast<std::size_t>(x)] += in[x];
        }
    }
    for (int y = 0; y < height; ++y) {
        uchar* out = dst.scanLine(y);
        for (int x = 0; x < width; ++x) {
            out[x] = static_cast<uchar>((sum[static_cast<std::size_t>(x)] + span / 2) / span);
        }
        const int leaving = y - w;
        const int entering = y + w + 1;
        if (leaving >= 0) {
            const uchar* in = src.constScanLine(leaving);
            for (int x = 0; x < width; ++x) {
                sum[static_cast<std::size_t>(x)] -= in[x];
            }
        }
        if (entering < height) {
            const uchar* in = src.constScanLine(entering);
            for (int x = 0; x < width; ++x) {
                sum[static_cast<std::size_t>(x)] += in[x];
            }
        }
    }
}

/// A layer of the painter's device pixels under ``bounds``, painted through
/// the painter's own transform; copied back at the same device origin.
struct Layer {
    QImage image;
    QPoint origin;
    double dpr = 1.0;
    /// Device pixels per painter unit.
    double scale = 1.0;
};

[[nodiscard]] std::optional<Layer> render_layer(const QPainter& painter, const QRectF& bounds,
                                                const std::function<void(QPainter&)>& body)
{
    const QPaintDevice* device = painter.device();
    if (device == nullptr || bounds.isEmpty()) {
        return std::nullopt;
    }
    const QTransform device_transform = painter.deviceTransform();
    const QRectF covered = device_transform.mapRect(bounds);
    const int left = static_cast<int>(std::floor(covered.left()));
    const int top = static_cast<int>(std::floor(covered.top()));
    const int right = static_cast<int>(std::ceil(covered.right()));
    const int bottom = static_cast<int>(std::ceil(covered.bottom()));
    const QSize size(right - left, bottom - top);
    constexpr int k_max_side = 8192;
    if (size.isEmpty() || size.width() > k_max_side || size.height() > k_max_side) {
        return std::nullopt;
    }
    Layer layer;
    layer.origin = QPoint(left, top);
    layer.dpr = device->devicePixelRatioF();
    layer.scale = std::hypot(device_transform.m11(), device_transform.m12());
    layer.image = QImage(size, QImage::Format_ARGB32_Premultiplied);
    layer.image.fill(Qt::transparent);
    layer.image.setDevicePixelRatio(layer.dpr);
    QPainter p(&layer.image);
    p.setRenderHints(painter.renderHints());
    p.setWorldTransform(device_transform * QTransform::fromTranslate(-left, -top)
                        * QTransform::fromScale(1.0 / layer.dpr, 1.0 / layer.dpr));
    body(p);
    p.end();
    return layer;
}

/// ``image`` onto the painter at device point ``origin`` plus ``offset``
/// device pixels, at the painter's opacity.
void blit(QPainter& painter, const Layer& layer, const QImage& image, QPointF offset = {})
{
    painter.save();
    painter.resetTransform();
    painter.drawImage(QPointF((layer.origin.x() + offset.x()) / layer.dpr,
                              (layer.origin.y() + offset.y()) / layer.dpr),
                      image);
    painter.restore();
}

/// The shadow's offset in device pixels: at its angle on the slide, whatever
/// the body's own turn.
[[nodiscard]] QPointF shadow_offset(const canvas::Shadow& shadow, double scale)
{
    const double radians = shadow.angle * std::numbers::pi / 180.0;
    return {std::cos(radians) * shadow.distance * scale, std::sin(radians) * shadow.distance * scale};
}

void paint_with(QPainter& painter, const canvas::Shadow& shadow, const canvas::Glow& glow,
                double soft_edge, const QRectF& bounds,
                const std::function<void(QPainter&)>& body)
{
    const bool has_shadow = shadow.on && css_color(shadow.color).isValid();
    const bool has_glow = glow.on && css_color(glow.color).isValid();
    const bool has_soft = soft_edge > 0.0;
    if (!has_shadow && !has_glow && !has_soft) {
        body(painter);
        return;
    }
    canvas::Effects reach_of;
    reach_of.shadow = shadow;
    reach_of.glow = glow;
    const double reach = canvas::effects_reach(reach_of) + 1.0;
    auto layer = render_layer(painter, bounds.adjusted(-reach, -reach, reach, reach), body);
    if (!layer) {
        body(painter);
        return;
    }
    const QImage coverage = (has_shadow || has_glow) ? coverage_of(layer->image) : QImage();
    if (has_shadow) {
        const QImage soft = blurred_coverage(coverage, shadow.blur * layer->scale);
        blit(painter, *layer, tinted_coverage(soft, css_color(shadow.color)),
             shadow_offset(shadow, layer->scale));
    }
    if (has_glow) {
        // The halo reaches ``size`` out: a blur that wide, lifted so the
        // near part is the glow's full colour.
        const QImage halo = blurred_coverage(coverage, glow.size * layer->scale);
        blit(painter, *layer, tinted_coverage(halo, css_color(glow.color), 2.5));
    }
    if (has_soft) {
        apply_soft_edge(layer->image, soft_edge * layer->scale);
    }
    blit(painter, *layer, layer->image);
}

}  // namespace

QImage coverage_of(const QImage& image)
{
    QImage out(image.size(), QImage::Format_Grayscale8);
    out.setDevicePixelRatio(image.devicePixelRatio());
    const QImage src = image.format() == QImage::Format_ARGB32_Premultiplied
        ? image
        : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < src.height(); ++y) {
        const auto* in = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        uchar* px = out.scanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            px[x] = static_cast<uchar>(qAlpha(in[x]));
        }
    }
    return out;
}

QImage blurred_coverage(const QImage& coverage, double radius)
{
    if (coverage.isNull() || radius < 0.5) {
        return coverage;
    }
    QImage src = coverage.format() == QImage::Format_Grayscale8
        ? coverage
        : coverage.convertToFormat(QImage::Format_Grayscale8);
    // Three box blurs approximate a Gaussian; their half-width is chosen so
    // the spread is about ``radius``.
    const int w = std::max(1, static_cast<int>(std::lround(radius * 0.6)));
    QImage tmp(src.size(), QImage::Format_Grayscale8);
    tmp.setDevicePixelRatio(src.devicePixelRatio());
    QImage out(src.size(), QImage::Format_Grayscale8);
    out.setDevicePixelRatio(src.devicePixelRatio());
    for (int pass = 0; pass < 3; ++pass) {
        box_blur_rows(src, tmp, w);
        box_blur_columns(tmp, out, w);
        src = out;
    }
    return src;
}

QImage tinted_coverage(const QImage& coverage, const QColor& color, double gain)
{
    const QImage src = coverage.format() == QImage::Format_Grayscale8
        ? coverage
        : coverage.convertToFormat(QImage::Format_Grayscale8);
    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(src.devicePixelRatio());
    // One colour per coverage level, looked up.
    QRgb table[256];
    const double cr = color.redF();
    const double cg = color.greenF();
    const double cb = color.blueF();
    const double ca = color.alphaF();
    for (int i = 0; i < 256; ++i) {
        const double a = std::clamp(i / 255.0 * gain, 0.0, 1.0) * ca;
        table[i] = qRgba(static_cast<int>(cr * a * 255.0 + 0.5),
                         static_cast<int>(cg * a * 255.0 + 0.5),
                         static_cast<int>(cb * a * 255.0 + 0.5), static_cast<int>(a * 255.0 + 0.5));
    }
    for (int y = 0; y < src.height(); ++y) {
        const uchar* in = src.constScanLine(y);
        auto* px = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            px[x] = table[in[x]];
        }
    }
    return out;
}

void apply_soft_edge(QImage& body, double radius)
{
    if (body.isNull() || radius < 0.5) {
        return;
    }
    // The blurred coverage is half at the edge and whole a blur's width in:
    // that ramp, stretched, is how much of the body stays.
    const QImage mask = blurred_coverage(coverage_of(body), radius);
    for (int y = 0; y < body.height(); ++y) {
        const uchar* m = mask.constScanLine(y);
        auto* px = reinterpret_cast<QRgb*>(body.scanLine(y));
        for (int x = 0; x < body.width(); ++x) {
            const double keep = std::clamp((m[x] / 255.0 - 0.5) * 2.0, 0.0, 1.0);
            if (keep >= 1.0) {
                continue;
            }
            const QRgb p = px[x];
            px[x] = qRgba(static_cast<int>(qRed(p) * keep + 0.5),
                          static_cast<int>(qGreen(p) * keep + 0.5),
                          static_cast<int>(qBlue(p) * keep + 0.5),
                          static_cast<int>(qAlpha(p) * keep + 0.5));
        }
    }
}

void paint_with_effects(QPainter& painter, const canvas::Effects& effects, const QRectF& bounds,
                        const std::function<void(QPainter&)>& body)
{
    paint_with(painter, effects.shadow, effects.glow, effects.soft_edge, bounds, body);
}

void paint_with_text_shadow(QPainter& painter, const canvas::Shadow& shadow, const QRectF& bounds,
                            const std::function<void(QPainter&)>& body)
{
    paint_with(painter, shadow, canvas::Glow{}, 0.0, bounds, body);
}

}  // namespace seventhslide::render
