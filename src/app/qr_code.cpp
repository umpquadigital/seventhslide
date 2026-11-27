#include "qr_code.hpp"

#include "icon_image_provider.hpp"

#include <qrcodegen.hpp>

#include <QByteArray>

#include <optional>
#include <string>

namespace seventhslide::app {
namespace qr_code {

namespace {

std::optional<qrcodegen::QrCode> encode(const QString& text)
{
    const QByteArray utf8 = text.toUtf8();
    if (utf8.isEmpty()) {
        return std::nullopt;
    }
    try {
        return qrcodegen::QrCode::encodeText(utf8.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    } catch (const qrcodegen::data_too_long&) {
        return std::nullopt;
    }
}

}  // namespace

int extent(const QString& text)
{
    const auto code = encode(text);
    return code ? code->getSize() + 2 * k_quiet_zone : 0;
}

QString svg(const QString& text)
{
    const auto code = encode(text);
    if (!code) {
        return {};
    }

    // One path of unit squares, row by row; a run of dark modules is one
    // rectangle, which keeps the document small.
    const int size = code->getSize();
    std::string path;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size;) {
            if (!code->getModule(x, y)) {
                ++x;
                continue;
            }
            const int start = x;
            while (x < size && code->getModule(x, y)) {
                ++x;
            }
            path += "M" + std::to_string(start + k_quiet_zone) + " "
                    + std::to_string(y + k_quiet_zone) + "h" + std::to_string(x - start)
                    + "v1h-" + std::to_string(x - start) + "z";
        }
    }
    const int extent = size + 2 * k_quiet_zone;
    return QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %1 %1\" "
                          "shape-rendering=\"crispEdges\">"
                          "<rect width=\"%1\" height=\"%1\" fill=\"#ffffff\"/>"
                          "<path d=\"%2\" fill=\"#000000\"/></svg>")
        .arg(extent)
        .arg(QString::fromStdString(path));
}

}  // namespace qr_code

QString QrCode::url(const QString& text) const
{
    const QString doc = qr_code::svg(text);
    return doc.isEmpty() ? QString() : svg_icon_url(doc);
}

int QrCode::extent(const QString& text) const
{
    return qr_code::extent(text);
}

}  // namespace seventhslide::app
