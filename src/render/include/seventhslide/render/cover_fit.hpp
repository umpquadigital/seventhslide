#pragma once

// CSS ``background-size: cover`` source rect.
// Uniform scale = max(dw/iw, dh/ih); crop centered. Plain C++20 (no Qt).

namespace seventhslide::render {

/// Sub-rect of the source image that fills ``dest_w``x``dest_h`` without letterboxing.
struct CoverSource {
    double src_x = 0;
    double src_y = 0;
    double src_w = 0;
    double src_h = 0;

    [[nodiscard]] bool operator==(const CoverSource&) const noexcept = default;
};

/**
 * Compute the centered crop of ``img_w``x``img_h`` that covers ``dest_w``x``dest_h``.
 *
 * The destination is fractional: a box need not land on whole pixels, and
 * the crop takes its exact aspect. When any dimension is <= 0, returns a
 * zero rect.
 */
[[nodiscard]] CoverSource cover_source(
    int img_w,
    int img_h,
    double dest_w,
    double dest_h) noexcept;

}  // namespace seventhslide::render
