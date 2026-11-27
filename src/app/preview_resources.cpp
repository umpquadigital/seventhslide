#include "preview_resources.hpp"

namespace seventhslide::app {

PreviewResources::PreviewResources() : measurer_(fonts_) {}

render::BackgroundImageCache* PreviewResources::image_cache() const noexcept
{
    return rooted_ ? image_cache_.get() : nullptr;
}

void PreviewResources::set_media_root(const std::filesystem::path& media_root)
{
    rooted_ = !media_root.empty();
    image_cache_->set_static_root(media_root);
}

void PreviewResources::clear_image_cache()
{
    image_cache_->clear();
}

bool PreviewResources::forget_missing_pictures(const std::vector<std::string>& urls)
{
    if (!image_cache_->forget_missing(urls)) {
        return false;
    }
    ++pictures_revision_;
    return true;
}

}  // namespace seventhslide::app
