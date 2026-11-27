#include "seventhslide/db/media_kind.hpp"

#include "seventhslide/model/static_media.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <stdexcept>

namespace seventhslide::db {
namespace {

constexpr std::array<std::string_view, 7> k_image_extensions = {
    ".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".svg"};
constexpr std::array<std::string_view, 6> k_video_extensions = {
    ".mp4", ".webm", ".ogg", ".mov", ".avi", ".mkv"};
// ``.ogg`` is both: an Ogg file can hold either, so its tab decides.
constexpr std::array<std::string_view, 10> k_audio_extensions = {
    ".mp3", ".wav", ".m4a", ".aac", ".flac", ".ogg", ".oga", ".opus", ".aif", ".aiff"};

}  // namespace

MediaTables media_tables(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Image:
        return {"image_files", "image_folders", "image_folder_items"};
    case MediaKind::Video:
        return {"video_files", "video_folders", "video_folder_items"};
    case MediaKind::Audio:
        return {"audio_files", "audio_folders", "audio_folder_items"};
    }
    throw std::invalid_argument("unknown media kind");
}

std::string_view media_subdir(MediaKind kind) noexcept
{
    switch (kind) {
    case MediaKind::Image:
        return model::k_images_subdir;
    case MediaKind::Video:
        return model::k_videos_subdir;
    case MediaKind::Audio:
        return model::k_audio_subdir;
    }
    return {};
}

std::span<const std::string_view> media_extensions(MediaKind kind) noexcept
{
    switch (kind) {
    case MediaKind::Image:
        return k_image_extensions;
    case MediaKind::Video:
        return k_video_extensions;
    case MediaKind::Audio:
        return k_audio_extensions;
    }
    return {};
}

bool is_media_filename(MediaKind kind, std::string_view name)
{
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos) {
        return false;
    }
    const auto ext = util::to_lower(name.substr(dot));
    const auto allowed = media_extensions(kind);
    return std::find(allowed.begin(), allowed.end(), ext) != allowed.end();
}

bool is_any_media_filename(std::string_view name)
{
    return std::any_of(k_media_kinds.begin(), k_media_kinds.end(),
                       [&](MediaKind kind) { return is_media_filename(kind, name); });
}

}  // namespace seventhslide::db
