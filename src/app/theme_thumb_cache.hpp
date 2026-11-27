#pragma once

// Persistent JPEG thumbnails for the theme galleries, so a restart does not
// paint every theme again.
// Path: ``<media_dir>/thumbs/themes/<tier>/<identity>.jpg``
//
// The name is a hash of the theme and the pictures it names
// (theme_thumb_identity): an edited theme simply looks for another file. Fonts
// are left out on purpose: a font that changes shows in a thumb when the theme
// is next edited or the app updated. Old files are pruned as the library opens.

#include <nlohmann/json.hpp>

#include <QDateTime>
#include <QImage>
#include <QSize>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::render {
class BackgroundImageCache;
}

namespace seventhslide::app {

/// What a thumb shows besides the theme's style.
struct ThemeThumbView {
    /// The design context painted (``scene::DraftContext``).
    int context = 0;
    int canvas_width = 0;
    int canvas_height = 0;
    std::string_view output_name;
    /// The size it is painted at.
    QSize size;
};

/// The hash naming a thumb of ``style`` (as painted, after any isolation) in
/// ``view``. Covers the app version and the pictures the style names under
/// ``media_dir`` (their size and time), so a replaced picture or a new painter
/// makes a new thumb.
[[nodiscard]] std::string theme_thumb_identity(const nlohmann::json& style,
                                               const ThemeThumbView& view,
                                               const std::filesystem::path& media_dir);

/// The size a request for ``want`` is painted and stored at: its shape, at the
/// smallest tier covering it, so a resized gallery does not paint again. A card
/// rounds its height, so anything within a pixel of 16:9 is exactly 16:9.
/// Nothing when ``want`` is larger than any tier (those are not stored).
[[nodiscard]] std::optional<QSize> theme_thumb_stored_size(const QSize& want);

/// Empty without a media folder or a valid ``size``.
[[nodiscard]] std::filesystem::path theme_thumb_path(const std::filesystem::path& media_dir,
                                                     std::string_view identity,
                                                     const QSize& size);

/// ``image`` at exactly ``size`` (unchanged when it already is).
[[nodiscard]] QImage fit_theme_thumb(const QImage& image, const QSize& size);

/// The stored picture at ``size``; null when there is none or it cannot be read.
/// A hit counts as use: see ``prune_theme_thumbs``.
[[nodiscard]] QImage load_theme_thumb(const std::filesystem::path& path, const QSize& size);

/// Remove all but the ``keep`` most recently used thumbs.
void prune_theme_thumbs(const std::filesystem::path& media_dir, std::size_t keep = 4096);

/// The time a thumb's clock shows: a thumb is a picture of a theme, not a clock.
[[nodiscard]] QDateTime theme_thumb_clock_time();

/// The picture at ``path``, decoded just large enough to cover ``paint_size``
/// (whatever its shape) and never enlarged; null when it cannot be read.
[[nodiscard]] QImage decode_theme_thumb_picture(const std::filesystem::path& path,
                                                const QSize& paint_size);

/// A decode cache of one thumb's own. A thumb outlives the shared one, which
/// knows a picture only by its URL, so might hold one since replaced; and which
/// a closing library takes away mid-paint.
[[nodiscard]] std::shared_ptr<render::BackgroundImageCache> theme_thumb_pictures(
    const std::filesystem::path& media_dir,
    const QSize& paint_size);

} // namespace seventhslide::app
