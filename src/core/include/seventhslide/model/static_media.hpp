#pragma once

// Static media URL helper - `/static/{subdir}/{basename}`.
// Plain C++20 (no Qt). Percent-encoding is omitted so render ``safe_static_url``
// can resolve the path without a decode step.

#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::model {

/// The library's media folders, which are also the ``/static/{subdir}/``
/// segments their files are served under.
inline constexpr std::string_view k_images_subdir = "images";
inline constexpr std::string_view k_videos_subdir = "videos";
inline constexpr std::string_view k_audio_subdir = "audio";
/// Themes' own backgrounds (``theme::theme_assets_dir``).
inline constexpr std::string_view k_theme_backgrounds_subdir = "theme_backgrounds";
/// The library's fonts (``db::FontRepository``).
inline constexpr std::string_view k_fonts_subdir = "fonts";
/// Each Freeform document's own pictures and videos, a folder per owner
/// beneath it (``freeform/own_media.hpp``). Not the library's: nothing here
/// is listed with the images or videos.
inline constexpr std::string_view k_freeform_media_subdir = "freeform_media";

/// What follows the last '/' or '\\' - media may be stored with either.
[[nodiscard]] constexpr std::string_view media_basename(std::string_view path) noexcept
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

/**
 * Build ``/static/{subdir}/{basename}`` when ``filename`` has a non-empty
 * basename (rejects empty / ``.`` / ``..`` / path separators in the leaf).
 */
[[nodiscard]] std::optional<std::string> static_media_url(std::string_view subdir,
                                                          std::string_view filename);

/**
 * The ``/static/...`` URL a stored file reference shows: ``ref`` itself when
 * it is a URL already (a Freeform's own file, a theme's background), else the
 * library file ``ref`` names in ``subdir`` (``static_media_url``).
 */
[[nodiscard]] std::optional<std::string> media_ref_url(std::string_view subdir,
                                                       std::string_view ref);

} // namespace seventhslide::model
