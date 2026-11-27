#include <seventhslide/render/canvas_fit.hpp>

#include <algorithm>

namespace seventhslide::render {

CanvasFit fit_canvas(int canvas_w, int canvas_h, int viewport_w, int viewport_h) noexcept
{
    CanvasFit out;
    out.canvas_width = canvas_w;
    out.canvas_height = canvas_h;
    out.viewport_width = viewport_w;
    out.viewport_height = viewport_h;

    // A degenerate canvas or viewport is drawn unscaled at the origin.
    if (canvas_w <= 0 || canvas_h <= 0 || viewport_w <= 0 || viewport_h <= 0) {
        out.scale = 1.0;
        out.offset_x = 0.0;
        out.offset_y = 0.0;
        return out;
    }

    // const scale = Math.min(vw / cw, vh / ch);
    // canvas.style.setProperty('--fit-scale', scale > 0 ? String(scale) : '1');
    const double sx = static_cast<double>(viewport_w) / static_cast<double>(canvas_w);
    const double sy = static_cast<double>(viewport_h) / static_cast<double>(canvas_h);
    double scale = std::min(sx, sy);
    if (!(scale > 0.0)) {
        scale = 1.0;
    }
    out.scale = scale;

    // CSS: body flex center + transform-origin center -> stage centered in viewport.
    // The axis the scale came from is filled exactly: its offset is 0, not
    // what w - W * (w / W) rounds to (which differs when a*b+c is fused, as
    // AppleClang does on arm64).
    out.offset_x = scale == sx
        ? 0.0
        : (static_cast<double>(viewport_w) - static_cast<double>(canvas_w) * scale) * 0.5;
    out.offset_y = scale == sy
        ? 0.0
        : (static_cast<double>(viewport_h) - static_cast<double>(canvas_h) * scale) * 0.5;
    return out;
}

}  // namespace seventhslide::render
