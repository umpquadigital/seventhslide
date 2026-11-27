#include "freeform_thumb_cache.hpp"

#include "media_thumb_cache.hpp"

#include <seventhslide/util/atomic_file.hpp>

#include <QFile>

#include <algorithm>
#include <mutex>
#include <system_error>

namespace seventhslide::app {
namespace {

[[nodiscard]] std::filesystem::path freeform_thumb_dir(const std::filesystem::path& media_dir)
{
    return media_thumbs_dir(media_dir) / "freeform";
}

[[nodiscard]] std::filesystem::path stamp_path(const std::filesystem::path& media_dir,
                                               std::int64_t freeform_id)
{
    if (media_dir.empty() || freeform_id < 0) {
        return {};
    }
    return freeform_thumb_dir(media_dir) / (std::to_string(freeform_id) + ".stamp");
}

[[nodiscard]] std::string stamp_text(std::string_view preview_key,
                                     std::string_view theme_identity)
{
    std::string text(preview_key);
    text += '\n';
    text += theme_identity;
    return text;
}

/// Held by every write and removal: the GUI thread stores a tile on save, the
/// image providers and the remote's encode pool store stale ones, and a stamp
/// must never end up describing another writer's tiers.
std::mutex g_write_mutex;

[[nodiscard]] std::optional<std::string> read_stamp(const std::filesystem::path& media_dir,
                                                    std::int64_t freeform_id)
{
    const auto stamp = stamp_path(media_dir, freeform_id);
    if (stamp.empty()) {
        return std::nullopt;
    }
    QFile f(QString::fromStdString(stamp.string()));
    if (!f.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return f.readAll().toStdString();
}

bool write_locked(const std::filesystem::path& media_dir,
                  std::int64_t freeform_id,
                  std::string_view preview_key,
                  std::string_view theme_identity,
                  const QImage& image,
                  QString* error_out)
{
    if (media_dir.empty() || freeform_id < 0 || image.isNull()) {
        if (error_out) {
            *error_out = QStringLiteral("Invalid freeform thumb arguments");
        }
        return false;
    }

    bool ok = true;
    QString last_err;
    for (const int tier : k_media_thumb_tiers) {
        const auto dest = freeform_thumb_path(media_dir, freeform_id, tier);
        if (dest.empty()) {
            ok = false;
            last_err = QStringLiteral("Invalid thumb path");
            continue;
        }
        if (!write_thumb_jpeg(image, tier, dest, &last_err)) {
            ok = false;
        }
    }

    // The stamp says every tier is current, so it goes last and only when
    // they all are; after a failed tier, drop it so the next look re-renders.
    const auto stamp = stamp_path(media_dir, freeform_id);
    if (!stamp.empty()) {
        std::error_code ec;
        if (!ok) {
            std::filesystem::remove(stamp, ec);
        } else if (!util::write_cache_file(stamp, stamp_text(preview_key, theme_identity))) {
            ok = false;
            last_err = QStringLiteral("Stamp write failed");
        }
    }

    if (!ok && error_out) {
        *error_out = last_err;
    }
    return ok;
}

} // namespace

std::filesystem::path freeform_thumb_path(const std::filesystem::path& media_dir,
                                          std::int64_t freeform_id,
                                          int max_edge)
{
    if (media_dir.empty() || freeform_id < 0 || max_edge <= 0) {
        return {};
    }
    return freeform_thumb_dir(media_dir) / std::to_string(max_edge)
         / (std::to_string(freeform_id) + ".jpg");
}

bool freeform_thumb_fresh(const std::filesystem::path& media_dir,
                          std::int64_t freeform_id,
                          std::string_view preview_key,
                          std::string_view theme_identity)
{
    if (read_stamp(media_dir, freeform_id) != stamp_text(preview_key, theme_identity)) {
        return false;
    }
    std::error_code ec;
    for (const int tier : k_media_thumb_tiers) {
        const auto dest = freeform_thumb_path(media_dir, freeform_id, tier);
        if (dest.empty() || !std::filesystem::is_regular_file(dest, ec) || ec) {
            return false;
        }
    }
    return true;
}

bool write_freeform_thumbs(const std::filesystem::path& media_dir,
                           std::int64_t freeform_id,
                           std::string_view preview_key,
                           std::string_view theme_identity,
                           const QImage& image,
                           QString* error_out)
{
    const std::lock_guard lock(g_write_mutex);
    return write_locked(media_dir, freeform_id, preview_key, theme_identity, image, error_out);
}

std::optional<std::string> freeform_thumb_stamp(const std::filesystem::path& media_dir,
                                                std::int64_t freeform_id)
{
    // No lock: a stamp is replaced whole (written aside, then renamed), so a
    // read sees one stamp or another, and the GUI thread preparing a tile
    // never waits on a worker's encode.
    return read_stamp(media_dir, freeform_id);
}

bool write_freeform_thumbs_unless_changed(const std::filesystem::path& media_dir,
                                          std::int64_t freeform_id,
                                          const std::optional<std::string>& seen,
                                          std::string_view preview_key,
                                          std::string_view theme_identity,
                                          const QImage& image)
{
    const std::lock_guard lock(g_write_mutex);
    if (read_stamp(media_dir, freeform_id) != seen) {
        return false;
    }
    return write_locked(media_dir, freeform_id, preview_key, theme_identity, image, nullptr);
}

QImage load_freeform_thumb(const std::filesystem::path& media_dir,
                           std::int64_t freeform_id,
                           int max_edge)
{
    const int tier = media_thumb_tier_for_request(max_edge, max_edge);
    const auto dest = freeform_thumb_path(media_dir, freeform_id, tier);
    if (dest.empty()) {
        return {};
    }
    QImage img;
    if (!img.load(QString::fromStdString(dest.string()))) {
        return {};
    }
    return img;
}

QImage load_freeform_thumb_fitted(const std::filesystem::path& media_dir,
                                  std::int64_t freeform_id,
                                  const QSize& size)
{
    const int edge = std::max(1, std::max(size.width(), size.height()));
    QImage loaded = load_freeform_thumb(media_dir, freeform_id, edge);
    if (loaded.isNull() || size.isEmpty()) {
        return loaded;
    }
    if (loaded.width() > size.width() || loaded.height() > size.height()) {
        return loaded.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return loaded;
}

void remove_freeform_thumbs(const std::filesystem::path& media_dir, std::int64_t freeform_id)
{
    if (media_dir.empty() || freeform_id < 0) {
        return;
    }
    const std::lock_guard lock(g_write_mutex);
    std::error_code ec;
    for (const int tier : k_media_thumb_tiers) {
        const auto path = freeform_thumb_path(media_dir, freeform_id, tier);
        if (!path.empty()) {
            std::filesystem::remove(path, ec);
        }
    }
    const auto stamp = stamp_path(media_dir, freeform_id);
    if (!stamp.empty()) {
        std::filesystem::remove(stamp, ec);
    }
}

} // namespace seventhslide::app
