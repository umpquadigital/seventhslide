#pragma once

// Vector outlines for the Freeform shape palette (rect / ellipse / star / arrow ...).
// One geometry source shared by the Qt painter, the editor canvas item, and tests.
// Seam: plain C++20 (no Qt, no JSON).
//
// Every outline is generated from the object's box (``w`` x ``h``) plus normalized
// adjustments, so a shape *reflows* when the box resizes - the same behaviour as
// ProPresenter / PowerPoint / Keynote adjustment handles.

#include <string_view>
#include <vector>

namespace seventhslide::geom {

enum class ShapeKind {
    Rectangle,
    RoundedRect,
    Ellipse,
    Triangle,
    RightTriangle,
    Rhombus,
    Polygon,
    Star,
    Arrow,
    DoubleArrow,
    /// A cloud callout: a cloud of lobes, and its trail of bubbles under it,
    /// left of the middle.
    Cloud,
    /// Drawn point by point (4.7's 8): ``ShapeAdjust::path``. A line is one
    /// with two points.
    Drawn,
};

[[nodiscard]] std::string_view to_string(ShapeKind kind) noexcept;
[[nodiscard]] ShapeKind shape_kind_from_string(std::string_view s) noexcept;

/// Every kind the palette offers, in its order: all but Drawn, which the
/// drawing tools make.
[[nodiscard]] const std::vector<ShapeKind>& all_shape_kinds();

/// One point of a drawn outline, as shares of its box. A smooth point is
/// passed through by a curve; any other is a corner.
struct PathPoint {
    double x = 0;
    double y = 0;
    bool smooth = false;

    [[nodiscard]] bool operator==(const PathPoint&) const noexcept = default;
};

/**
 * Resolution-independent shape adjustments (PowerPoint's ``adj`` values).
 *
 * Each field is a ratio of the bounding box - never an absolute length - so the
 * outline stays proportional as the object is resized.
 */
struct ShapeAdjust {
    /// RoundedRect: corner radius / ``min(w, h)``. Kept off the smaller edge so
    /// corners stay circular instead of shearing with the box.
    double corner = 0.18;
    /// Polygon sides / Star points.
    int points = 5;
    /// Star: inner radius / outer radius.
    double inner = 0.38;
    /// Arrow: head length / ``w`` (DoubleArrow: per head).
    double head = 0.35;
    /// Arrow: shaft thickness / ``h``.
    double thickness = 0.45;
    /// Drawn: its points, in order (at least two to draw anything).
    std::vector<PathPoint> path;
    /// Drawn: the last point joins the first, and it fills.
    bool closed = false;

    [[nodiscard]] bool operator==(const ShapeAdjust&) const noexcept = default;
};

// --- Which adjustments a kind actually uses (drives editor chrome) ---------

[[nodiscard]] bool uses_corner(ShapeKind kind) noexcept;
[[nodiscard]] bool uses_points(ShapeKind kind) noexcept;
[[nodiscard]] bool uses_inner(ShapeKind kind) noexcept;
[[nodiscard]] bool uses_arrow(ShapeKind kind) noexcept;
/// A drawn shape: its points are its outline.
[[nodiscard]] bool uses_path(ShapeKind kind) noexcept;
/// Its outline is open (a drawn shape not closed): it has ends and no fill.
[[nodiscard]] bool is_open(ShapeKind kind, const ShapeAdjust& adjust) noexcept;

// --- Per-kind legal ranges (inclusive) ------------------------------------

inline constexpr double k_corner_min = 0.0;
inline constexpr double k_corner_max = 0.5;
inline constexpr int k_points_min = 3;
inline constexpr int k_points_max = 24;
inline constexpr double k_inner_min = 0.05;
inline constexpr double k_inner_max = 0.95;
inline constexpr double k_head_min = 0.05;
inline constexpr double k_thickness_min = 0.05;
inline constexpr double k_thickness_max = 1.0;

/// Largest legal ``head`` for this kind (DoubleArrow must fit two heads).
[[nodiscard]] double head_max(ShapeKind kind) noexcept;

/// Fold out-of-range adjustments into the legal window for ``kind``.
[[nodiscard]] ShapeAdjust clamp_adjust(ShapeKind kind, ShapeAdjust adjust) noexcept;

// --- Outline ---------------------------------------------------------------

struct ShapePoint {
    double x = 0;
    double y = 0;

    [[nodiscard]] bool operator==(const ShapePoint&) const noexcept = default;
};

enum class SegmentKind {
    Line,
    Cubic,
};

/// ``Line``: ``to`` only. ``Cubic``: ``c1``/``c2`` control points, then ``to``.
struct ShapeSegment {
    SegmentKind kind = SegmentKind::Line;
    ShapePoint c1;
    ShapePoint c2;
    ShapePoint to;

    [[nodiscard]] bool operator==(const ShapeSegment&) const noexcept = default;
};

struct ShapeSubpath {
    ShapePoint start;
    std::vector<ShapeSegment> segments;
    bool closed = true;

    [[nodiscard]] bool operator==(const ShapeSubpath&) const noexcept = default;
};

struct ShapePath {
    std::vector<ShapeSubpath> subpaths;

    [[nodiscard]] bool empty() const noexcept { return subpaths.empty(); }
    [[nodiscard]] bool operator==(const ShapePath&) const noexcept = default;
};

/**
 * Build the outline for ``kind`` inside the box ``(0, 0, w, h)``.
 *
 * ``adjust`` is clamped first, so callers may pass raw document values. Returns an
 * empty path when the box has no area. Circular arcs are emitted as quarter-arc
 * cubics (kappa), which every 2D backend draws exactly.
 */
[[nodiscard]] ShapePath build_shape_path(ShapeKind kind, double w, double h,
                                         const ShapeAdjust& adjust);

} // namespace seventhslide::geom
