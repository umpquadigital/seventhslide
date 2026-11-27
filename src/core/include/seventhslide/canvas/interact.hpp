#pragma once

// Canvas pointer interaction: hit-test, drag/resize, clamp, snap.
//
// One implementation for the Freeform editor and the text-theme designer.
// Coordinates: geometry is in canvas (design) pixels. ``scale`` maps those onto
// the stage; handle hits are tested in stage pixels. Seam: plain C++20 (no Qt).

#include "seventhslide/geom/canvas.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::canvas {

inline constexpr double k_min_width = 40.0;
inline constexpr double k_min_height = 24.0;
/// Keep at least this much of the box on-canvas (allows slight overhang).
inline constexpr double k_overhang = 20.0;
inline constexpr double k_default_snap = 10.0;
inline constexpr double k_default_handle_hit = 10.0;

enum class Handle {
    None,
    Pending,
    Move,
    N,
    NE,
    E,
    SE,
    S,
    SW,
    W,
    NW,
};

[[nodiscard]] std::string_view to_string(Handle handle) noexcept;
[[nodiscard]] Handle handle_from_string(std::string_view s) noexcept;

struct Rect {
    std::string id;
    double x = 0;
    double y = 0;
    double w = 100;
    double h = 100;
    double rotation = 0;
    bool locked = false;
    bool flip_h = false;
    bool flip_v = false;

    [[nodiscard]] bool operator==(const Rect&) const noexcept = default;
};

struct Limits {
    double min_w = k_min_width;
    double min_h = k_min_height;
    double overhang = k_overhang;
    double snap_threshold = k_default_snap;
    double handle_hit = k_default_handle_hit;
};

struct AxisTargets {
    std::vector<double> xs;
    std::vector<double> ys;
};

struct SnapResult {
    Rect rect;
    double v_guide = -1;
    double h_guide = -1;
};

[[nodiscard]] bool point_in_rect(double cx, double cy, const Rect& rect) noexcept;

/// Stage-pixel hit against the eight resize handles of a canvas rect.
/// Handle positions follow ``rotation`` (degrees, Qt-clockwise).
[[nodiscard]] Handle hit_handle(double stage_x, double stage_y, const Rect& canvas_rect,
                                double scale, double hit_px = k_default_handle_hit) noexcept;

/// Front-most object whose box contains ``(cx, cy)``. ``objects`` is back-to-front
/// (paint order); search walks it in reverse. Rotation is honoured.
[[nodiscard]] const Rect* hit_object(double cx, double cy, const std::vector<Rect>& objects);

[[nodiscard]] Rect clamp_rect(Rect rect, geom::CanvasSize canvas, const Limits& limits = {});

/// ``dx``/``dy`` are canvas pixels. ``Pending`` is treated as ``Move``.
[[nodiscard]] Rect geometry_from_drag(Handle mode, const Rect& start, double dx, double dy,
                                      geom::CanvasSize canvas, const Limits& limits = {});

/// Canvas edges and centre plus every other object's edges and centres.
[[nodiscard]] AxisTargets snap_axis_targets(geom::CanvasSize canvas,
                                            const std::vector<Rect>& objects,
                                            const std::vector<std::string>& exclude_ids);

[[nodiscard]] std::optional<double> nearest_snap(double value, const std::vector<double>& targets,
                                                 double threshold) noexcept;

/**
 * ``rect`` snapped to ``targets`` and then clamped to ``canvas``. A move (or a
 * pending press) tries the box's start, centre and end on each axis and takes
 * the closest within the snap distance, the earlier on a tie; a resize snaps
 * only the edges its handle holds, keeping the minimum size. The guides that
 * won are returned for the stage to draw (-1 for none). With ``enabled``
 * false the rect is only clamped.
 */
[[nodiscard]] SnapResult snap_rect(const Rect& rect, Handle mode, const AxisTargets& targets,
                                   geom::CanvasSize canvas, bool enabled,
                                   const Limits& limits = {});

} // namespace seventhslide::canvas
