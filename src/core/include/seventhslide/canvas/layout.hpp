#pragma once

// Selection-set geometry: bounds, marquee, align, distribute, flip, grouping.
//
// One implementation for the Freeform editor and the text-theme designer.
// Coordinates are canvas (design) pixels. Seam: plain C++20 (no Qt).

#include "seventhslide/canvas/interact.hpp"
#include "seventhslide/geom/canvas.hpp"

#include <string>
#include <utility>
#include <vector>

namespace seventhslide::canvas {

enum class Align {
    Left,
    Center,
    Right,
    Top,
    Middle,
    Bottom,
};

enum class Distribute {
    Horizontal,
    Vertical,
};

enum class FlipAxis {
    Horizontal,
    Vertical,
};

/// Axis-aligned bounds of a possibly rotated rect.
[[nodiscard]] Rect aabb(const Rect& rect) noexcept;

/// Union AABB of ``rects``. Empty input -> a zero rect.
[[nodiscard]] Rect union_bounds(const std::vector<Rect>& rects) noexcept;

/// ``{x,y,w,h}`` with positive extents, regardless of drag direction.
[[nodiscard]] Rect normalized_rect(Rect rect) noexcept;

/// True when the AABBs overlap (touching counts).
[[nodiscard]] bool rects_intersect(const Rect& a, const Rect& b) noexcept;

/// Ids whose AABB intersects the marquee, back-to-front order of ``objects``.
[[nodiscard]] std::vector<std::string> hit_marquee(const Rect& marquee,
                                                   const std::vector<Rect>& objects);

/// Members that share a non-empty ``group_id`` with any picked id. Picked
/// ungrouped ids pass through. Result is unique and keeps ``members`` order.
[[nodiscard]] std::vector<std::string> expand_groups(
    const std::vector<std::pair<std::string, std::string>>& members,
    const std::vector<std::string>& picked);

/// Translate / scale ``start`` as a block. Resize scales around the union AABB
/// (the dragged handle's opposite corner stays put). Locked members freeze the
/// whole set: the start rects are returned unchanged.
[[nodiscard]] std::vector<Rect> geometry_from_drag_many(Handle mode, const std::vector<Rect>& start,
                                                        double dx, double dy,
                                                        geom::CanvasSize canvas,
                                                        const Limits& limits = {});

/// Snap the union of ``rects`` and apply the same delta to every member.
struct SnapManyResult {
    std::vector<Rect> rects;
    double v_guide = -1;
    double h_guide = -1;
};

[[nodiscard]] SnapManyResult snap_many(const std::vector<Rect>& rects, Handle mode,
                                       const AxisTargets& targets, geom::CanvasSize canvas,
                                       bool enabled, const Limits& limits = {});

/// One selected -> align to ``frame`` (the canvas). Two or more -> align to the
/// selection's union. Locked members freeze the set.
[[nodiscard]] std::vector<Rect> align_rects(std::vector<Rect> rects, Align align, const Rect& frame);

/// Even spacing of 3+ boxes along an axis. No-op for fewer, or if any is locked.
[[nodiscard]] std::vector<Rect> distribute_rects(std::vector<Rect> rects, Distribute dist);

/// Translate by ``dx``/``dy`` and clamp. Locked members freeze the set.
[[nodiscard]] std::vector<Rect> nudge_rects(std::vector<Rect> rects, double dx, double dy,
                                            geom::CanvasSize canvas, const Limits& limits = {});

/// Mirror positions around the union centre and toggle each rect's flip flag.
/// A single object's box stays put; its ``flip_h`` / ``flip_v`` still toggles.
[[nodiscard]] std::vector<Rect> flip_rects(std::vector<Rect> rects, FlipAxis axis);

} // namespace seventhslide::canvas
