#pragma once

// Canvas sizes and the mapping between them.
//
// Everything a designer places - theme elements, Freeform objects - is authored
// in the pixels of one canvas, and every output paints at its own resolution. A
// layout therefore records the canvas it was authored on, and the one mapping
// below carries it onto whichever canvas paints it:
//
//   - positions and box extents follow each axis, so an element keeps its place
//     relative to the canvas edges (a full-canvas area still fills the canvas);
//   - type and line quantities (font sizes, strokes, gaps, tracking) follow the
//     smaller axis factor, so text never outgrows its box when the aspect ratio
//     differs. Between canvases of the same aspect both are the same uniform
//     scale, so a 1080p design is exact at 720p, 1440p and 4K.
//
// That is ``stretch``, the mapping a layout gets when it paints on a canvas it
// was not laid out for. A layout is kept in its output's own canvas, though,
// and when the output changes shape the layout is carried over once, by
// ``resize``: positions and extents per axis as above, type and lengths by the
// geometric mean of the two factors, so a resize and its reverse multiply type
// by exactly 1 and a 90 degree turn leaves it as it was. Stock layouts are
// authored once on a reference canvas and carried onto the output's by
// ``resize`` too.
//
// Plain C++20 (no Qt, no JSON).

#include <algorithm>
#include <cmath>

namespace seventhslide::geom {

struct CanvasSize {
    int width = 1920;
    int height = 1080;

    [[nodiscard]] constexpr bool valid() const noexcept { return width > 0 && height > 0; }
    [[nodiscard]] bool operator==(const CanvasSize&) const noexcept = default;
};

/// The canvas stock designs are authored on.
inline constexpr CanvasSize k_reference_canvas{1920, 1080};

/// Largest canvas an output may declare (8K UHD).
inline constexpr CanvasSize k_max_canvas{7680, 4320};

/// ``canvas`` when both sides are positive, else the reference canvas.
[[nodiscard]] constexpr CanvasSize canvas_or_reference(CanvasSize canvas) noexcept
{
    return canvas.valid() ? canvas : k_reference_canvas;
}

/// ``{width, height}`` when both are positive, else the reference canvas.
[[nodiscard]] constexpr CanvasSize canvas_or_reference(int width, int height) noexcept
{
    return canvas_or_reference(CanvasSize{width, height});
}

/// True when the two canvases have exactly the same aspect ratio.
[[nodiscard]] constexpr bool same_aspect(CanvasSize a, CanvasSize b) noexcept
{
    return a.valid() && b.valid()
        && static_cast<long long>(a.width) * b.height == static_cast<long long>(b.width) * a.height;
}

/// How a layout authored on one canvas lands on another.
class CanvasMapping {
public:
    constexpr CanvasMapping() noexcept = default;

    /// Positions and extents per axis; type by the smaller axis factor. The
    /// mapping a layout gets when it paints on another output.
    [[nodiscard]] static CanvasMapping stretch(CanvasSize from, CanvasSize to) noexcept
    {
        const auto [sx, sy] = factors(from, to);
        return CanvasMapping(sx, sy, 0.0, 0.0, std::min(sx, sy));
    }

    /// Positions and extents per axis; type by the geometric mean of the two
    /// factors. The mapping a layout gets, once, when its output changes
    /// canvas: the same uniform scale as ``stretch`` while the shape holds, and
    /// one whose reverse undoes it exactly when the shape changes.
    [[nodiscard]] static CanvasMapping resize(CanvasSize from, CanvasSize to) noexcept
    {
        const auto [sx, sy] = factors(from, to);
        return CanvasMapping(sx, sy, 0.0, 0.0, std::sqrt(sx * sy));
    }

    /// Uniform scale that shows the whole layout, centred (letterboxed).
    [[nodiscard]] static CanvasMapping fit(CanvasSize from, CanvasSize to) noexcept
    {
        return uniform(from, to, false);
    }

    /// Uniform scale that covers the whole canvas, centred (cropped).
    [[nodiscard]] static CanvasMapping fill(CanvasSize from, CanvasSize to) noexcept
    {
        return uniform(from, to, true);
    }

    [[nodiscard]] constexpr bool is_identity() const noexcept
    {
        return sx_ == 1.0 && sy_ == 1.0 && ox_ == 0.0 && oy_ == 0.0 && type_ == 1.0;
    }

    [[nodiscard]] constexpr double x(double v) const noexcept { return v * sx_ + ox_; }
    [[nodiscard]] constexpr double y(double v) const noexcept { return v * sy_ + oy_; }
    [[nodiscard]] constexpr double width(double v) const noexcept { return v * sx_; }
    [[nodiscard]] constexpr double height(double v) const noexcept { return v * sy_; }

    /// A typographic or stroke length (letter spacing, border width).
    [[nodiscard]] constexpr double length(double v) const noexcept { return v * type_; }

    /// A whole-pixel typographic length (line gap, verse gap). Stays at least 1
    /// when the source was positive, so a gap never silently disappears.
    [[nodiscard]] int pixels(int v) const noexcept
    {
        if (v <= 0) {
            return v;
        }
        return std::max(1, static_cast<int>(std::lround(v * type_)));
    }

    /// A font size in pixels; ``0`` ("same as the box") is kept.
    [[nodiscard]] int font_size(int v) const noexcept { return pixels(v); }

private:
    constexpr CanvasMapping(double sx, double sy, double ox, double oy, double type) noexcept
        : sx_(sx), sy_(sy), ox_(ox), oy_(oy), type_(type)
    {
    }

    struct Factors {
        double sx;
        double sy;
    };

    [[nodiscard]] static Factors factors(CanvasSize from, CanvasSize to) noexcept
    {
        const CanvasSize f = from.valid() ? from : k_reference_canvas;
        const CanvasSize t = to.valid() ? to : f;
        return {static_cast<double>(t.width) / f.width, static_cast<double>(t.height) / f.height};
    }

    [[nodiscard]] static CanvasMapping uniform(CanvasSize from, CanvasSize to, bool cover) noexcept
    {
        const CanvasSize f = from.valid() ? from : k_reference_canvas;
        const CanvasSize t = to.valid() ? to : f;
        const auto [sx, sy] = factors(f, t);
        const double s = cover ? std::max(sx, sy) : std::min(sx, sy);
        // The axis the scale came from fills exactly: its offset is 0, not the
        // rounding residue a fused multiply-add leaves.
        const double ox = s == sx ? 0.0 : (t.width - f.width * s) * 0.5;
        const double oy = s == sy ? 0.0 : (t.height - f.height * s) * 0.5;
        return CanvasMapping(s, s, ox, oy, s);
    }

    double sx_ = 1.0;
    double sy_ = 1.0;
    double ox_ = 0.0;
    double oy_ = 0.0;
    double type_ = 1.0;
};

}  // namespace seventhslide::geom
