#include <seventhslide/render/cover_fit.hpp>

#include <algorithm>

namespace seventhslide::render {

CoverSource cover_source(int img_w, int img_h, double dest_w, double dest_h) noexcept
{
    CoverSource out;
    if (img_w <= 0 || img_h <= 0 || !(dest_w > 0) || !(dest_h > 0)) {
        return out;
    }
    const double scale = std::max(dest_w / img_w, dest_h / img_h);
    out.src_w = dest_w / scale;
    out.src_h = dest_h / scale;
    out.src_x = (img_w - out.src_w) * 0.5;
    out.src_y = (img_h - out.src_h) * 0.5;
    return out;
}

}  // namespace seventhslide::render
