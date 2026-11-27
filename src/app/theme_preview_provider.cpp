#include "theme_preview_provider.hpp"

#include "entry_error.hpp"
#include "requested_size.hpp"

#include <QColor>
#include <QImage>

namespace seventhslide::app {

ThemePreviewProvider::ThemePreviewProvider(RequestFn request)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , request_(std::move(request))
{
}

QImage ThemePreviewProvider::requestImage(const QString& id,
                                          QSize* size,
                                          const QSize& requestedSize)
try {
    // Draft stage may request before layout; keep a sharp default (not gallery thumb size).
    const QSize want = size_or(requestedSize, QSize(960, 540));
    QImage img;
    if (request_) {
        img = request_(id, want);
    }
    if (img.isNull()) {
        img = QImage(want, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(0x0d, 0x0d, 0x0d));
    }
    if (size) {
        *size = img.size();
    }
    return img;
} catch (const std::exception& ex) {
    // Qt's pixmap loader cannot take an exception; a failed thumbnail is left
    // blank (and logged) rather than ending the process.
    (void)report_entry_error(__func__, ex);
    return {};
}

} // namespace seventhslide::app
