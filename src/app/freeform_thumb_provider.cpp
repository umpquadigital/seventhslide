#include "freeform_thumb_provider.hpp"

#include "entry_error.hpp"

#include "media_thumb_cache.hpp"

#include <QColor>
#include <QUrl>
#include <QUrlQuery>

namespace seventhslide::app {

FreeformThumbProvider::FreeformThumbProvider(RequestFn request, int max_cached)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , request_(std::move(request))
    , cache_(max_cached)
{
}

QImage FreeformThumbProvider::requestImage(const QString& id,
                                           QSize* size,
                                           const QSize& requestedSize)
try {
    QString path = id;
    QUrlQuery query;
    const auto q = path.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        query.setQuery(path.mid(q + 1));
        path = path.left(q);
    }
    path = QUrl::fromPercentEncoding(path.toUtf8());
    bool ok = false;
    const qint64 freeform_id = path.toLongLong(&ok);
    const QSize want = clamp_thumb_request(requestedSize);
    const QString key = id + QLatin1Char('@') + QString::number(want.width()) + QLatin1Char('x')
                        + QString::number(want.height());

    if (ok && freeform_id >= 0) {
        QImage cached = cache_.get(key);
        if (!cached.isNull()) {
            if (size) {
                *size = cached.size();
            }
            return cached;
        }
        if (request_) {
            QImage img = request_(freeform_id, want,
                                  query.queryItemValue(QStringLiteral("k"), QUrl::FullyDecoded),
                                  query.queryItemValue(QStringLiteral("t"), QUrl::FullyDecoded));
            if (!img.isNull()) {
                cache_.put(key, img);
                if (size) {
                    *size = img.size();
                }
                return img;
            }
        }
    }

    QImage placeholder(want, QImage::Format_ARGB32_Premultiplied);
    placeholder.fill(QColor(0x14, 0x14, 0x14));
    if (size) {
        *size = placeholder.size();
    }
    return placeholder;
} catch (const std::exception& ex) {
    // Qt's pixmap loader cannot take an exception; a failed thumbnail is left
    // blank (and logged) rather than ending the process.
    (void)report_entry_error(__func__, ex);
    return {};
}

} // namespace seventhslide::app
