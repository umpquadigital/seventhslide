#pragma once

// Scaled media thumbnails for library / controller galleries.
// Serves ``image://mediathumb/images/<percent-encoded-filename>`` and
// ``image://mediathumb/videos/...`` (a frame of the video).
// Prefers persistent disk thumbs under ``media/thumbs/`` when available.

#include "image_lru_cache.hpp"

#include <QImage>
#include <QQuickImageProvider>
#include <QString>

#include <filesystem>
#include <functional>

namespace seventhslide::app {

/**
 * Decodes library image thumbs at the requested tile size (capped) with an
 * in-memory LRU. Disk JPEG pyramids (import-time / lazy) keep cold browse cheap.
 */
class MediaThumbProvider final : public QQuickImageProvider {
public:
    using ResolveFn = std::function<std::filesystem::path(const QString& subdir,
                                                          const QString& filename)>;
    using MediaDirFn = std::function<std::filesystem::path()>;

    explicit MediaThumbProvider(ResolveFn resolve,
                                MediaDirFn media_dir = {},
                                int max_cached = 160);

    [[nodiscard]] QImage requestImage(const QString& id,
                                      QSize* size,
                                      const QSize& requestedSize) override;

private:
    [[nodiscard]] QImage load_scaled(const std::filesystem::path& path,
                                     const QSize& requested) const;

    ResolveFn resolve_;
    MediaDirFn media_dir_;
    ImageLruCache cache_;
};

} // namespace seventhslide::app
