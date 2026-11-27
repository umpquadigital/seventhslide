#include "icon_image_provider.hpp"

#include <QByteArray>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

namespace seventhslide::app {

QString svg_icon_url(const QString& svg)
{
    return QStringLiteral("image://ssicon/")
           + QString::fromLatin1(svg.toUtf8().toBase64(QByteArray::Base64UrlEncoding
                                                        | QByteArray::OmitTrailingEquals));
}

namespace {

// Glyphs are authored on a 24x24 viewBox and drawn at small sizes; when the
// caller gives no sourceSize, render at the design size rather than guessing.
constexpr int k_default_edge = 24;

} // namespace

IconImageProvider::IconImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage IconImageProvider::requestImage(const QString& id,
                                       QSize* size,
                                       const QSize& requestedSize)
{
    QSize want = requestedSize;
    if (want.width() <= 0) {
        want.setWidth(want.height() > 0 ? want.height() : k_default_edge);
    }
    if (want.height() <= 0) {
        want.setHeight(want.width());
    }

    QImage img(want, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    const QByteArray svg =
        QByteArray::fromBase64(id.toLatin1(), QByteArray::Base64UrlEncoding);
    if (!svg.isEmpty()) {
        QSvgRenderer renderer(svg);
        if (renderer.isValid()) {
            QPainter painter(&img);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            renderer.render(&painter);
        }
    }

    if (size) {
        *size = img.size();
    }
    return img;
}

} // namespace seventhslide::app
