#pragma once

// A QR code for a short text (the web remote's address), drawn as an SVG:
// dark modules on white with the standard four-module quiet zone, which is
// what phone cameras read most reliably. QML gets it as an ``image://ssicon``
// URL, so it is crisp at any size and loads synchronously.

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

namespace seventhslide::app {

namespace qr_code {

/// The quiet zone around the symbol, in modules (the standard's minimum).
inline constexpr int k_quiet_zone = 4;

/// ``text`` encoded at error-correction level Medium (raised when it costs no
/// extra size) as an SVG document, one unit per module. Empty for empty text
/// or text too long for any QR version.
[[nodiscard]] QString svg(const QString& text);

/// The width of ``svg(text)`` in modules, quiet zone included; 0 when there is
/// no code. A view draws it at a whole number of pixels per module.
[[nodiscard]] int extent(const QString& text);

}  // namespace qr_code

/// ``QrCode`` in QML.
class QrCode : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    /// ``text`` as a QR code image URL; empty when there is nothing to encode.
    Q_INVOKABLE [[nodiscard]] QString url(const QString& text) const;
    /// Its width in modules, quiet zone included; 0 for no code.
    Q_INVOKABLE [[nodiscard]] int extent(const QString& text) const;
};

}  // namespace seventhslide::app
