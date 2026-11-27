#pragma once

// Scaled Freeform library thumbs for ``image://freeformthumb/<freeformId>?k=&t=``
// (the item's stamp key and the theme identity, see
// FreeformLibraryController). Disk JPEGs under ``media/thumbs/freeform/`` keep
// cold browse cheap.

#include "image_lru_cache.hpp"

#include <QImage>
#include <QQuickImageProvider>
#include <QString>

#include <functional>

namespace seventhslide::app {

class FreeformThumbProvider final : public QQuickImageProvider {
public:
    /// ``key`` and ``themes`` are the URL's ``k`` and ``t``, decoded.
    using RequestFn = std::function<QImage(qint64 freeform_id, const QSize& requested,
                                           const QString& key, const QString& themes)>;

    explicit FreeformThumbProvider(RequestFn request, int max_cached = 160);

    [[nodiscard]] QImage requestImage(const QString& id,
                                      QSize* size,
                                      const QSize& requestedSize) override;

private:
    RequestFn request_;
    ImageLruCache cache_;
};

} // namespace seventhslide::app
