#include <seventhslide/render/object_fit.hpp>

#include <seventhslide/render/cover_fit.hpp>

#include <algorithm>

namespace seventhslide::render {

ObjectFitPlacement object_fit(std::string_view fit,
                              int img_w,
                              int img_h,
                              double dest_x,
                              double dest_y,
                              double dest_w,
                              double dest_h) noexcept
{
    ObjectFitPlacement out;
    if (img_w <= 0 || img_h <= 0 || dest_w <= 0 || dest_h <= 0) {
        return out;
    }

    if (fit == "fill") {
        out.src_x = 0;
        out.src_y = 0;
        out.src_w = img_w;
        out.src_h = img_h;
        out.dst_x = dest_x;
        out.dst_y = dest_y;
        out.dst_w = dest_w;
        out.dst_h = dest_h;
        return out;
    }

    if (fit == "cover") {
        const CoverSource crop = cover_source(img_w, img_h, dest_w, dest_h);
        if (crop.src_w <= 0 || crop.src_h <= 0) {
            return out;
        }
        out.src_x = crop.src_x;
        out.src_y = crop.src_y;
        out.src_w = crop.src_w;
        out.src_h = crop.src_h;
        out.dst_x = dest_x;
        out.dst_y = dest_y;
        out.dst_w = dest_w;
        out.dst_h = dest_h;
        return out;
    }

    // contain (default): uniform scale = min(dw/iw, dh/ih), centered in dest
    const double scale = std::min(dest_w / img_w, dest_h / img_h);
    out.src_x = 0;
    out.src_y = 0;
    out.src_w = img_w;
    out.src_h = img_h;
    out.dst_w = img_w * scale;
    out.dst_h = img_h * scale;
    out.dst_x = dest_x + (dest_w - out.dst_w) * 0.5;
    out.dst_y = dest_y + (dest_h - out.dst_h) * 0.5;
    return out;
}

} // namespace seventhslide::render
