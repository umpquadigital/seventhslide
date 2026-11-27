#include "media_thumb_provider.hpp"

#include "entry_error.hpp"

#include "media_thumb_cache.hpp"

#include <seventhslide/model/static_media.hpp>
#include <seventhslide/util/leaf_filename.hpp>

#include <QImageReader>
#include <QUrl>

#include <filesystem>

namespace seventhslide::app {
namespace {

QSize fit_inside(const QSize& orig, const QSize& box)
{
    if (orig.width() <= 0 || orig.height() <= 0) {
        return box;
    }
    if (orig.width() <= box.width() && orig.height() <= box.height()) {
        return orig;
    }
    const qreal sx = qreal(box.width()) / orig.width();
    const qreal sy = qreal(box.height()) / orig.height();
    const qreal s = qMin(sx, sy);
    return QSize(qMax(1, int(orig.width() * s)), qMax(1, int(orig.height() * s)));
}

} // namespace

MediaThumbProvider::MediaThumbProvider(ResolveFn resolve,
                                       MediaDirFn media_dir,
                                       int max_cached)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , resolve_(std::move(resolve))
    , media_dir_(std::move(media_dir))
    , cache_(max_cached)
{
}

QImage MediaThumbProvider::load_scaled(const std::filesystem::path& path,
                                       const QSize& requested) const
{
    if (path.empty()) {
        return {};
    }
    QImageReader reader(QString::fromStdString(path.string()));
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        return {};
    }
    const QSize box = clamp_thumb_request(requested);
    QSize orig = reader.size();
    if (!orig.isValid()) {
        // Some formats omit size until read - fall back to a bounded decode.
        reader.setScaledSize(box);
        return reader.read();
    }
    reader.setScaledSize(fit_inside(orig, box));
    return reader.read();
}

QImage MediaThumbProvider::requestImage(const QString& id,
                                        QSize* size,
                                        const QSize& requestedSize)
try {
    // id: "images/<filename>" or "images/<url-encoded>"
    QString path_id = id;
    const int q = path_id.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        path_id = path_id.left(q);
    }
    const int slash = path_id.indexOf(QLatin1Char('/'));
    if (slash <= 0 || slash >= path_id.size() - 1) {
        return {};
    }
    const QString subdir = path_id.left(slash);
    const QString filename = QString::fromStdString(util::leaf_filename(
        QUrl::fromPercentEncoding(path_id.mid(slash + 1).toUtf8()).toStdString()));
    if (filename.isEmpty()) {
        return {};
    }

    const QSize box = clamp_thumb_request(requestedSize);
    // The query is a cache-busting token (library revision), so it belongs in the
    // key - stripping it here would serve another library's thumb.
    const QString token = q >= 0 ? id.mid(q) : QString();
    const QString cache_key =
        subdir + QLatin1Char('/') + filename + token + QLatin1Char('@')
        + QString::number(box.width()) + QLatin1Char('x') + QString::number(box.height());
    if (QImage hit = cache_.get(cache_key); !hit.isNull()) {
        if (size) {
            *size = hit.size();
        }
        return hit;
    }

    if (!resolve_) {
        return {};
    }
    const auto source = resolve_(subdir, filename);
    if (source.empty()) {
        return {};
    }

    const QLatin1String videos_subdir(model::k_videos_subdir.data(),
                                      static_cast<qsizetype>(model::k_videos_subdir.size()));
    if (subdir.compare(videos_subdir, Qt::CaseInsensitive) == 0) {
        // A video is only ever shown from its disk thumb, which the warming
        // pool made before the row was given this URL (MediaController). This
        // remakes it only if the video has changed since.
        const auto media_dir = media_dir_ ? media_dir_() : std::filesystem::path();
        const std::string fn = filename.toStdString();
        if (media_dir.empty() || !ensure_video_thumbs(media_dir, source, fn)) {
            return {};
        }
        const int tier = media_thumb_tier_for_request(box.width(), box.height());
        QImage img = load_scaled(video_thumb_path(media_dir, fn, tier), box);
        if (img.isNull()) {
            return {};
        }
        cache_.put(cache_key, img);
        if (size) {
            *size = img.size();
        }
        return img;
    }

    // Prefer persistent disk thumbs (import-time / prior lazy write).
    const QLatin1String images_subdir(model::k_images_subdir.data(),
                                      static_cast<qsizetype>(model::k_images_subdir.size()));
    if (subdir.compare(images_subdir, Qt::CaseInsensitive) == 0 && media_dir_) {
        const auto media_dir = media_dir_();
        if (!media_dir.empty()) {
            const int tier = media_thumb_tier_for_request(box.width(), box.height());
            const std::string fn = filename.toStdString();
            if (ensure_media_thumb(media_dir, source, fn, tier)) {
                const auto thumb = media_thumb_path(media_dir, fn, tier);
                QImage img = load_scaled(thumb, box);
                if (!img.isNull()) {
                    cache_.put(cache_key, img);
                    if (size) {
                        *size = img.size();
                    }
                    return img;
                }
            }
        }
    }

    // Fallback: bounded decode of the original (still capped by clamp_request).
    QImage img = load_scaled(source, box);
    if (img.isNull()) {
        return {};
    }
    cache_.put(cache_key, img);
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
