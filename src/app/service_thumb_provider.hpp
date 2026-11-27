#pragma once

// Serves ``image://servicethumb/<serviceId>?k=<key>&p=<pictures>``: the
// Services window's thumbnails (see service_thumbs.hpp). ``k`` is the key
// over all the thumbnail is made from; one drawn under it is served again.

#include <QImage>
#include <QQuickImageProvider>
#include <QString>

#include <functional>

namespace seventhslide::app {

class ServiceThumbProvider final : public QQuickImageProvider {
public:
    /// The thumbnail of service ``service_id``, ``key`` the URL's ``k``;
    /// null when it has none.
    using RequestFn =
        std::function<QImage(qint64 service_id, const QString& key, const QSize& requested)>;

    explicit ServiceThumbProvider(RequestFn request);

    [[nodiscard]] QImage requestImage(const QString& id,
                                      QSize* size,
                                      const QSize& requestedSize) override;

private:
    RequestFn request_;
};

}  // namespace seventhslide::app
