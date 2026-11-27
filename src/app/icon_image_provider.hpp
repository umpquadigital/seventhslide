#pragma once

// QQuickImageProvider for the UI's vector glyph set (ribbon / tab / button icons).

#include <QQuickImageProvider>
#include <QString>

namespace seventhslide::app {

/// ``svg`` (a whole SVG document) as an ``image://ssicon`` URL the provider
/// below draws: base64url without padding.
[[nodiscard]] QString svg_icon_url(const QString& svg);

/**
 * Rasterises UI glyphs supplied inline by QML:
 * ``image://ssicon/<base64url of an SVG document>``
 *
 * The art itself lives in glyphs.cpp (``Icons`` in QML) - this only turns it
 * into pixels.
 * The point of the provider is that it loads *synchronously*: Qt always pulls a
 * ``data:`` URL through the async pixmap-reader thread, so a recoloured glyph
 * (hover, selection) blanked for a frame before the new copy arrived. A
 * provider-backed source is Ready in the frame it is assigned, so icons neither
 * pop in late nor blink when they change ink.
 */
class IconImageProvider final : public QQuickImageProvider {
public:
    IconImageProvider();

    [[nodiscard]] QImage requestImage(const QString& id,
                                      QSize* size,
                                      const QSize& requestedSize) override;
};

} // namespace seventhslide::app
