#pragma once

// Canvas letterbox / pillarbox fit.
// Uniform scale = min(vw/cw, vh/ch), centred in the viewport.
// Seam: plain C++20 (no Qt) - painters apply the result.

namespace seventhslide::render {

/// Result of fitting a fixed design canvas into a viewport.
struct CanvasFit {
    double scale = 1.0;
    /// Top-left of the *scaled* canvas inside the viewport (logical px).
    double offset_x = 0.0;
    double offset_y = 0.0;
    int canvas_width = 0;
    int canvas_height = 0;
    int viewport_width = 0;
    int viewport_height = 0;

    [[nodiscard]] bool operator==(const CanvasFit&) const noexcept = default;
};

/**
 * Uniformly scale ``canvas_w``x``canvas_h`` to fit ``viewport_w``x``viewport_h``.
 *
 * Matches JS ``Math.min(vw/cw, vh/ch)`` (and ``scale > 0 ? scale : 1``).
 * When any dimension is <= 0, returns scale 1 with zero offsets (JS early-return
 * leaves the prior CSS var; callers with no prior state get identity).
 * Offsets center the scaled stage (letterbox / pillarbox).
 */
[[nodiscard]] CanvasFit fit_canvas(
    int canvas_w,
    int canvas_h,
    int viewport_w,
    int viewport_h) noexcept;

}  // namespace seventhslide::render
