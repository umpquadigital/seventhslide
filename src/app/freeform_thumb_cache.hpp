#pragma once

// Persistent JPEG thumbnails for Freeform library items.
// Path: ``<media_dir>/thumbs/freeform/<tier>/<freeformId>.jpg``
// Stamp: ``<media_dir>/thumbs/freeform/<freeformId>.stamp`` (preview key + canvas,
// and the theme identity: text that stays the same across runs while the
// themes do, see FreeformLibraryController).

#include <QImage>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::app {

[[nodiscard]] std::filesystem::path freeform_thumb_path(const std::filesystem::path& media_dir,
                                                        std::int64_t freeform_id,
                                                        int max_edge);

[[nodiscard]] bool freeform_thumb_fresh(const std::filesystem::path& media_dir,
                                        std::int64_t freeform_id,
                                        std::string_view preview_key,
                                        std::string_view theme_identity);

/// Write sm/md/lg JPEGs from ``image`` (already rendered; scaled per tier).
/// Writes and removals never interleave: the stamp always describes the
/// tiers beside it.
bool write_freeform_thumbs(const std::filesystem::path& media_dir,
                           std::int64_t freeform_id,
                           std::string_view preview_key,
                           std::string_view theme_identity,
                           const QImage& image,
                           QString* error_out = nullptr);

/// The stamp on disk now; nullopt with none.
[[nodiscard]] std::optional<std::string> freeform_thumb_stamp(
    const std::filesystem::path& media_dir,
    std::int64_t freeform_id);

/// ``write_freeform_thumbs`` for a tile painted away from the GUI thread from
/// a document read when the stamp was ``seen`` (``freeform_thumb_stamp``).
/// Skipped, returning false, when the stamp has changed since: a save stored
/// a newer picture, another worker stored this one, or the item was removed.
bool write_freeform_thumbs_unless_changed(const std::filesystem::path& media_dir,
                                          std::int64_t freeform_id,
                                          const std::optional<std::string>& seen,
                                          std::string_view preview_key,
                                          std::string_view theme_identity,
                                          const QImage& image);

[[nodiscard]] QImage load_freeform_thumb(const std::filesystem::path& media_dir,
                                         std::int64_t freeform_id,
                                         int max_edge);

/// Disk thumb for ``freeform_id`` scaled down to fit ``size`` (null if not on disk).
/// Touches only the file system, so it is safe on an image-provider thread.
[[nodiscard]] QImage load_freeform_thumb_fitted(const std::filesystem::path& media_dir,
                                                std::int64_t freeform_id,
                                                const QSize& size);

void remove_freeform_thumbs(const std::filesystem::path& media_dir, std::int64_t freeform_id);

} // namespace seventhslide::app
