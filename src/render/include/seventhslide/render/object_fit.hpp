#pragma once

// CSS ``object-fit`` placement math.
// ``contain`` / ``cover`` / ``fill`` into a destination rect. Plain C++20.

#include <string_view>

namespace seventhslide::render {

/// Source crop + destination draw rect for one object-fit placement.
struct ObjectFitPlacement {
    double src_x = 0;
    double src_y = 0;
    double src_w = 0;
    double src_h = 0;
    double dst_x = 0;
    double dst_y = 0;
    double dst_w = 0;
    double dst_h = 0;

    [[nodiscard]] bool empty() const noexcept { return src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0; }
    [[nodiscard]] bool operator==(const ObjectFitPlacement&) const noexcept = default;
};

/**
 * Place ``img_w``x``img_h`` into dest ``(dx,dy,dw,dh)`` using CSS object-fit.
 *
 * - ``contain`` - letterbox inside dest (default / unknown)
 * - ``cover`` - fill dest, crop centered source
 * - ``fill`` - stretch to dest (may distort)
 *
 * Zero / negative dims -> empty placement.
 */
[[nodiscard]] ObjectFitPlacement object_fit(std::string_view fit,
                                           int img_w,
                                           int img_h,
                                           double dest_x,
                                           double dest_y,
                                           double dest_w,
                                           double dest_h) noexcept;

} // namespace seventhslide::render
