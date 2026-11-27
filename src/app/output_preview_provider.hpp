#pragma once

// QQuickImageProvider for operator output thumbnails.

#include <QQuickImageProvider>
#include <QString>
#include <QUrlQuery>

#include <functional>

namespace seventhslide::app {

/**
 * Serves ``image://outputpreview/<output id>?r=<rev>`` thumbnails (and the
 * Freeform editor's ``image://freeformpreview/<output id>?...``).
 *
 * ``request`` returns a rendered SceneFrame image (or a dark placeholder).
 * It is handed the URL's query whole, for what only it reads (a slide's id,
 * the side of a stack).
 */
class OutputPreviewProvider final : public QQuickImageProvider {
public:
    using RequestFn = std::function<QImage(const QString& output_id, int slide_index,
                                           const QSize& requested, const QUrlQuery& query)>;

    explicit OutputPreviewProvider(RequestFn request);

    [[nodiscard]] QImage requestImage(const QString& id,
                                      QSize* size,
                                      const QSize& requestedSize) override;

private:
    RequestFn request_;
};

} // namespace seventhslide::app
