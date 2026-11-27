#include "output_preview_provider.hpp"

#include "entry_error.hpp"
#include "requested_size.hpp"

#include <QColor>
#include <QImage>
#include <QUrl>
#include <QUrlQuery>

namespace seventhslide::app {

OutputPreviewProvider::OutputPreviewProvider(RequestFn request)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , request_(std::move(request))
{
}

QImage OutputPreviewProvider::requestImage(const QString& id,
                                           QSize* size,
                                           const QSize& requestedSize)
try {
    // id may include query: "out-main?r=3", "out-main?slide=2&r=3"; the output
    // id is percent-encoded.
    QString output_id = id;
    QString query;
    const auto q = output_id.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        query = output_id.mid(q + 1);
        output_id = output_id.left(q);
    }
    output_id = QUrl::fromPercentEncoding(output_id.toUtf8());

    int slide_index = -1;
    const QUrlQuery uq(query);
    if (uq.hasQueryItem(QStringLiteral("slide"))) {
        bool ok = false;
        const int parsed = uq.queryItemValue(QStringLiteral("slide")).toInt(&ok);
        if (ok) {
            slide_index = parsed;
        }
    }

    // None asked for: the controller draws one in the output's shape.
    const QSize want = size_or(requestedSize, QSize());

    QImage img;
    if (request_) {
        img = request_(output_id, slide_index, want, uq);
    }
    if (img.isNull()) {
        img = QImage(size_or(want, QSize(320, 180)), QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(0x14, 0x14, 0x14));
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
