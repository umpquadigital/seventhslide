#pragma once

// What every operator-side preview and thumbnail is rendered with: the fonts,
// the text measurer over them, the decode cache for library images, and the
// theme revision their caches are keyed on.
//
// One set for the application, so a font is resolved and an image decoded
// once however many previews ask. SessionFacade owns it; the controllers that
// render previews (themes, the Freeform editor and library) borrow it.
//
// Seam: Qt Gui. Set up and re-rooted on the GUI thread (see gui_thread.hpp);
// a preview finishing on another thread may use the measurer, its faces and
// the image cache (each locks itself) and read image_cache().

#include <seventhslide/fonts/font_registry.hpp>
#include <seventhslide/layout/qt_text_measurer.hpp>
#include <seventhslide/render/background_painter.hpp>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace seventhslide::app {

class PreviewResources {
public:
    PreviewResources();
    PreviewResources(const PreviewResources&) = delete;
    PreviewResources& operator=(const PreviewResources&) = delete;
    PreviewResources(PreviewResources&&) = delete;
    PreviewResources& operator=(PreviewResources&&) = delete;
    ~PreviewResources() = default;

    [[nodiscard]] fonts::FontRegistry& fonts() noexcept { return fonts_; }
    [[nodiscard]] layout::QtTextMeasurer& measurer() noexcept { return measurer_; }
    [[nodiscard]] const layout::QtTextMeasurer& measurer() const noexcept { return measurer_; }

    /// The image decode cache, rooted at the open library's media folder.
    /// Null while there is no root yet. A cache, so reading through a const
    /// owner still fills it; it locks internally. Any thread.
    [[nodiscard]] render::BackgroundImageCache* image_cache() const noexcept;
    /// The same cache, whether rooted or not, for a presenter to draw from
    /// (``ScenePresenter::share_image_cache``). The presenter holds it for as
    /// long as it paints, and it follows every re-rooting and
    /// ``forget_missing_pictures``; with no root it finds no pictures.
    [[nodiscard]] std::shared_ptr<render::BackgroundImageCache> shared_image_cache() const
    {
        return image_cache_;
    }
    /// Root the cache at ``media_root``. Re-rooting empties it, so a library
    /// switch cannot serve the previous library's images.
    void set_media_root(const std::filesystem::path& media_root);
    void clear_image_cache();
    /// Pictures ``urls`` arrived: those found missing are looked for again,
    /// and if a preview was drawn without one ``pictures_revision`` moves, so
    /// thumbnails are asked for again. True if it moved.
    bool forget_missing_pictures(const std::vector<std::string>& urls);
    /// Bumped by ``forget_missing_pictures``. Thumbnail URLs that may have
    /// been drawn without a picture carry it.
    [[nodiscard]] int pictures_revision() const noexcept { return pictures_revision_; }

    /// Bumped whenever a theme changes. Anything rendered against themes
    /// (theme and slide thumbnails) keys its cache on it.
    [[nodiscard]] int theme_revision() const noexcept { return theme_revision_; }
    void bump_theme_revision() noexcept { ++theme_revision_; }

private:
    fonts::FontRegistry fonts_;
    layout::QtTextMeasurer measurer_;
    const std::shared_ptr<render::BackgroundImageCache> image_cache_ =
        std::make_shared<render::BackgroundImageCache>();
    /// Written on the GUI thread, read by previews finishing elsewhere.
    std::atomic<bool> rooted_ = false;
    int theme_revision_ = 0;
    int pictures_revision_ = 0;
};

}  // namespace seventhslide::app
