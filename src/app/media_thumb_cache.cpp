#include "media_thumb_cache.hpp"

#include "video_frame.hpp"

#include <seventhslide/model/static_media.hpp>
#include <seventhslide/util/atomic_file.hpp>

#include <QBuffer>
#include <QByteArray>
#include <QImageReader>
#include <QImageWriter>

#include <algorithm>
#include <system_error>

namespace seventhslide::app {
namespace {

constexpr int k_thumb_jpeg_quality = 82;

void set_error(QString* error_out, const QString& message)
{
    if (error_out) {
        *error_out = message;
    }
}

[[nodiscard]] std::string thumb_stem(std::string_view image_filename)
{
    const auto leaf = std::filesystem::path(image_filename).filename();
    return leaf.stem().string();
}

[[nodiscard]] bool thumb_fresh(const std::filesystem::path& thumb,
                               const std::filesystem::path& source)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(thumb, ec) || ec) {
        return false;
    }
    if (!std::filesystem::is_regular_file(source, ec) || ec) {
        return false;
    }
    const auto thumb_t = std::filesystem::last_write_time(thumb, ec);
    if (ec) {
        return false;
    }
    const auto src_t = std::filesystem::last_write_time(source, ec);
    if (ec) {
        return false;
    }
    return thumb_t >= src_t;
}

[[nodiscard]] QSize fit_longest_edge(const QSize& orig, int max_edge)
{
    if (orig.width() <= 0 || orig.height() <= 0 || max_edge <= 0) {
        return QSize(max_edge, max_edge);
    }
    const int longest = std::max(orig.width(), orig.height());
    if (longest <= max_edge) {
        return orig;
    }
    const qreal s = qreal(max_edge) / qreal(longest);
    return QSize(std::max(1, int(orig.width() * s)), std::max(1, int(orig.height() * s)));
}

/// Decode with the longest edge capped at ``max_edge``, letting the reader scale
/// during decode when the format reports its size up front.
[[nodiscard]] QImage decode_fitted(QImageReader& reader, int max_edge)
{
    const QSize orig = reader.size();
    if (orig.isValid()) {
        reader.setScaledSize(fit_longest_edge(orig, max_edge));
        return reader.read();
    }
    QImage full = reader.read();
    if (full.isNull()) {
        return full;
    }
    const QSize target = fit_longest_edge(full.size(), max_edge);
    if (full.size() == target) {
        return full;
    }
    return full.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace

int media_thumb_tier_for_request(int requested_w, int requested_h)
{
    const int edge = std::max(1, std::max(requested_w, requested_h));
    if (edge <= k_media_thumb_tier_sm) {
        return k_media_thumb_tier_sm;
    }
    if (edge <= k_media_thumb_tier_md) {
        return k_media_thumb_tier_md;
    }
    return k_media_thumb_tier_lg;
}

QSize clamp_thumb_request(const QSize& requested)
{
    int w = requested.width();
    int h = requested.height();
    if (w <= 0 && h <= 0) {
        return QSize(160, 90);
    }
    if (w <= 0) {
        w = h * 16 / 9;
    }
    if (h <= 0) {
        h = w * 9 / 16;
    }
    constexpr int k_max = 512;
    if (w > k_max || h > k_max) {
        const qreal s = qMin(qreal(k_max) / w, qreal(k_max) / h);
        w = qMax(1, int(w * s));
        h = qMax(1, int(h * s));
    }
    return QSize(w, h);
}

bool write_thumb_jpeg(const QImage& image,
                      int max_edge,
                      const std::filesystem::path& dest,
                      QString* error_out)
{
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    if (ec) {
        set_error(error_out, QStringLiteral("Cannot create thumbs directory"));
        return false;
    }
    const bool shrink = std::max(image.width(), image.height()) > max_edge;
    const QImage out = shrink ? image.scaled(fit_longest_edge(image.size(), max_edge),
                                             Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation)
                              : image;
    // Encoded in memory and renamed into place: the warming pool, the image
    // providers and the remote can all write the same thumb, and a reader
    // must never load a half-written one (its mtime would make it look fresh).
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(k_thumb_jpeg_quality);
    if (!writer.write(out)
        || !util::write_cache_file(dest, std::string_view(bytes.constData(),
                                                          static_cast<std::size_t>(bytes.size())))) {
        set_error(error_out, QStringLiteral("Thumb write failed"));
        return false;
    }
    return true;
}

std::filesystem::path media_thumb_path(const std::filesystem::path& media_dir,
                                       std::string_view image_filename,
                                       int max_edge)
{
    if (media_dir.empty()) {
        return {};
    }
    const auto stem = thumb_stem(image_filename);
    if (stem.empty() || stem == "." || stem == "..") {
        return {};
    }
    return media_thumbs_dir(media_dir) / std::to_string(max_edge) / (stem + ".jpg");
}

bool ensure_media_thumb(const std::filesystem::path& media_dir,
                        const std::filesystem::path& source_path,
                        std::string_view image_filename,
                        int max_edge,
                        QString* error_out)
{
    if (media_dir.empty() || source_path.empty() || max_edge <= 0) {
        set_error(error_out, QStringLiteral("Invalid thumb arguments"));
        return false;
    }
    const auto dest = media_thumb_path(media_dir, image_filename, max_edge);
    if (dest.empty()) {
        set_error(error_out, QStringLiteral("Invalid image filename"));
        return false;
    }
    if (thumb_fresh(dest, source_path)) {
        return true;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(source_path, ec) || ec) {
        set_error(error_out, QStringLiteral("Source image missing"));
        return false;
    }
    QImageReader reader(QString::fromStdString(source_path.string()));
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        set_error(error_out, QStringLiteral("Cannot read image"));
        return false;
    }
    const QImage img = decode_fitted(reader, max_edge);
    if (img.isNull()) {
        set_error(error_out, QStringLiteral("Image decode failed"));
        return false;
    }
    return write_thumb_jpeg(img, max_edge, dest, error_out);
}

bool ensure_media_thumbs_all_tiers(const std::filesystem::path& media_dir,
                                   const std::filesystem::path& source_path,
                                   std::string_view image_filename,
                                   QString* error_out)
{
    if (media_dir.empty() || source_path.empty()) {
        set_error(error_out, QStringLiteral("Invalid thumb arguments"));
        return false;
    }
    const bool all_fresh =
        std::all_of(k_media_thumb_tiers.begin(), k_media_thumb_tiers.end(), [&](int tier) {
            return thumb_fresh(media_thumb_path(media_dir, image_filename, tier), source_path);
        });
    if (all_fresh) {
        return true;
    }

    // Decode once at the largest tier; the smaller tiers scale down from it.
    QImageReader reader(QString::fromStdString(source_path.string()));
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        set_error(error_out, QStringLiteral("Cannot read image"));
        return false;
    }
    const QImage master = decode_fitted(reader, k_media_thumb_tier_lg);
    if (master.isNull()) {
        set_error(error_out, QStringLiteral("Image decode failed"));
        return false;
    }

    bool ok = true;
    QString last_err;
    for (const int tier : k_media_thumb_tiers) {
        const auto dest = media_thumb_path(media_dir, image_filename, tier);
        if (dest.empty()) {
            ok = false;
            last_err = QStringLiteral("Invalid image filename");
            continue;
        }
        if (thumb_fresh(dest, source_path)) {
            continue;
        }
        if (!write_thumb_jpeg(master, tier, dest, &last_err)) {
            ok = false;
        }
    }
    if (!ok) {
        set_error(error_out, last_err);
    }
    return ok;
}

void remove_media_thumbs(const std::filesystem::path& media_dir,
                         std::string_view image_filename)
{
    if (media_dir.empty()) {
        return;
    }
    for (const int tier : k_media_thumb_tiers) {
        const auto path = media_thumb_path(media_dir, image_filename, tier);
        if (path.empty()) {
            continue;
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}


std::filesystem::path video_thumb_path(const std::filesystem::path& media_dir,
                                       std::string_view video_filename,
                                       int max_edge)
{
    if (media_dir.empty()) {
        return {};
    }
    const auto leaf = std::filesystem::path(video_filename).filename().string();
    if (leaf.empty() || leaf == "." || leaf == "..") {
        return {};
    }
    return media_thumbs_dir(media_dir) / std::string(model::k_videos_subdir)
           / std::to_string(max_edge) / (leaf + ".jpg");
}

bool ensure_video_thumbs(const std::filesystem::path& media_dir,
                         const std::filesystem::path& source_path,
                         std::string_view video_filename,
                         QString* error_out)
{
    if (media_dir.empty() || source_path.empty()) {
        set_error(error_out, QStringLiteral("Invalid thumb arguments"));
        return false;
    }
    if (video_thumb_path(media_dir, video_filename, k_media_thumb_tier_lg).empty()) {
        set_error(error_out, QStringLiteral("Invalid video filename"));
        return false;
    }
    const bool all_fresh =
        std::all_of(k_media_thumb_tiers.begin(), k_media_thumb_tiers.end(), [&](int tier) {
            return thumb_fresh(video_thumb_path(media_dir, video_filename, tier), source_path);
        });
    if (all_fresh) {
        return true;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source_path, ec) || ec) {
        set_error(error_out, QStringLiteral("Source video missing"));
        return false;
    }
    // One frame at the largest tier; the smaller tiers scale down from it.
    const QImage master = grab_video_frame(source_path, k_media_thumb_tier_lg);
    if (master.isNull()) {
        set_error(error_out, QStringLiteral("Video has no frame to show"));
        return false;
    }
    bool ok = true;
    QString last_err;
    for (const int tier : k_media_thumb_tiers) {
        if (!write_thumb_jpeg(master, tier, video_thumb_path(media_dir, video_filename, tier),
                              &last_err)) {
            ok = false;
        }
    }
    if (!ok) {
        set_error(error_out, last_err);
    }
    return ok;
}

void remove_video_thumbs(const std::filesystem::path& media_dir,
                         std::string_view video_filename)
{
    for (const int tier : k_media_thumb_tiers) {
        const auto path = video_thumb_path(media_dir, video_filename, tier);
        if (path.empty()) {
            continue;
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

} // namespace seventhslide::app
