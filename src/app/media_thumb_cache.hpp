#pragma once

// Persistent JPEG thumbnails under ``<media_dir>/thumbs/<tier>/<stem>.jpg`` for
// images and ``<media_dir>/thumbs/videos/<tier>/<filename>.jpg`` for videos.
// Generated at import (and lazily on first UI request) so library / gallery
// browsing does not decode full-resolution originals after a restart.

#include <seventhslide/library/library.hpp>

#include <QImage>
#include <QSize>
#include <QString>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace seventhslide::app {

inline constexpr int k_media_thumb_tier_sm = 160;
inline constexpr int k_media_thumb_tier_md = 320;
inline constexpr int k_media_thumb_tier_lg = 512;
/// Every tier, smallest first.
inline constexpr std::array<int, 3> k_media_thumb_tiers{
    k_media_thumb_tier_sm, k_media_thumb_tier_md, k_media_thumb_tier_lg};

/// Pick the smallest tier whose max edge covers the requested paint size.
[[nodiscard]] int media_thumb_tier_for_request(int requested_w, int requested_h);

/// Size for an image-provider request: 160x90 when unspecified, a missing side
/// derived at 16:9, and capped at 512 so a bad QML binding cannot ask for a
/// full-resolution decode.
[[nodiscard]] QSize clamp_thumb_request(const QSize& requested);

/// Write ``image`` as a JPEG thumb at ``dest``, scaled down (aspect kept) when
/// its longest edge exceeds ``max_edge``. Creates the parent directory.
bool write_thumb_jpeg(const QImage& image,
                      int max_edge,
                      const std::filesystem::path& dest,
                      QString* error_out = nullptr);

/// The folder every library thumbnail lives under (image tiers, and
/// ``freeform/`` for the Freeform library's).
[[nodiscard]] inline std::filesystem::path media_thumbs_dir(const std::filesystem::path& media_dir)
{
    return media_dir / library::k_thumbs_dirname;
}

[[nodiscard]] std::filesystem::path media_thumb_path(const std::filesystem::path& media_dir,
                                                     std::string_view image_filename,
                                                     int max_edge);

/// Ensure a JPEG thumb exists (and is not older than the source). Returns true
/// when ``media_thumb_path`` is readable afterward.
bool ensure_media_thumb(const std::filesystem::path& media_dir,
                        const std::filesystem::path& source_path,
                        std::string_view image_filename,
                        int max_edge,
                        QString* error_out = nullptr);

/// Write sm/md/lg tiers for one library image.
bool ensure_media_thumbs_all_tiers(const std::filesystem::path& media_dir,
                                   const std::filesystem::path& source_path,
                                   std::string_view image_filename,
                                   QString* error_out = nullptr);

void remove_media_thumbs(const std::filesystem::path& media_dir,
                         std::string_view image_filename);

/// A video's thumb at one tier. The whole filename names it, so ``a.mp4``,
/// ``a.mov`` and an image ``a.png`` never share one.
[[nodiscard]] std::filesystem::path video_thumb_path(const std::filesystem::path& media_dir,
                                                     std::string_view video_filename,
                                                     int max_edge);

/// Ensure every tier of a video's thumb exists and is not older than the
/// video: one frame (grab_video_frame) makes all three. False when the video
/// has no picture SeventhSlide can decode.
bool ensure_video_thumbs(const std::filesystem::path& media_dir,
                         const std::filesystem::path& source_path,
                         std::string_view video_filename,
                         QString* error_out = nullptr);

void remove_video_thumbs(const std::filesystem::path& media_dir,
                         std::string_view video_filename);

} // namespace seventhslide::app
