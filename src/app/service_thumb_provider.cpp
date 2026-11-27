#include "service_thumb_provider.hpp"

#include "entry_error.hpp"
#include "service_thumbs.hpp"

#include <QColor>
#include <QUrl>
#include <QUrlQuery>

namespace seventhslide::app {

ServiceThumbProvider::ServiceThumbProvider(RequestFn request)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , request_(std::move(request))
{
}

QImage ServiceThumbProvider::requestImage(const QString& id,
                                          QSize* size,
                                          const QSize& requestedSize)
try {
    QString path = id;
    QUrlQuery query;
    if (const auto q = path.indexOf(QLatin1Char('?')); q >= 0) {
        query.setQuery(path.mid(q + 1));
        path = path.left(q);
    }
    bool ok = false;
    const qint64 service_id = QUrl::fromPercentEncoding(path.toUtf8()).toLongLong(&ok);
    const QSize want = ServiceThumbs::requested_or_default(requestedSize);

    QImage img;
    if (ok && service_id >= 0 && request_) {
        img = request_(service_id, query.queryItemValue(QStringLiteral("k"), QUrl::FullyDecoded),
                       want);
    }
    // A service whose rows lay out no slide: the dark of an empty output.
    if (img.isNull()) {
        img = QImage(QSize(want.width(), want.width() * 9 / 16).expandedTo(QSize(1, 1)),
                     QImage::Format_ARGB32_Premultiplied);
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

}  // namespace seventhslide::app
