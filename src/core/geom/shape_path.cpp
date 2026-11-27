#include "seventhslide/geom/shape_path.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace seventhslide::geom {
namespace {

/// Control-point ratio for a quarter arc drawn as one cubic.
constexpr double k_kappa = 0.5522847498307936;

[[nodiscard]] ShapeSegment line_to(double x, double y)
{
    return ShapeSegment{SegmentKind::Line, {}, {}, ShapePoint{x, y}};
}

[[nodiscard]] ShapeSegment cubic_to(double c1x, double c1y, double c2x, double c2y, double x,
                                    double y)
{
    return ShapeSegment{SegmentKind::Cubic, ShapePoint{c1x, c1y}, ShapePoint{c2x, c2y},
                        ShapePoint{x, y}};
}

/// Closed polygon through ``pts`` (first point becomes the subpath start).
[[nodiscard]] ShapePath polygon_path(const std::vector<ShapePoint>& pts)
{
    ShapePath path;
    if (pts.size() < 3) {
        return path;
    }
    ShapeSubpath sub;
    sub.start = pts.front();
    sub.segments.reserve(pts.size() - 1);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        sub.segments.push_back(line_to(pts[i].x, pts[i].y));
    }
    path.subpaths.push_back(std::move(sub));
    return path;
}

/// Vertices of a regular ``n``-gon inscribed in the box's ellipse, first at top.
/// ``radius_scale`` alternates outer / inner radii for stars.
void radial_vertices(std::vector<ShapePoint>& out, double w, double h, int n, double outer,
                     double inner, bool alternate)
{
    const double cx = w * 0.5;
    const double cy = h * 0.5;
    const double rx = w * 0.5;
    const double ry = h * 0.5;
    const int steps = alternate ? n * 2 : n;
    const double sweep = (2.0 * std::numbers::pi) / static_cast<double>(steps);
    const double start = -std::numbers::pi / 2.0; // 12 o'clock
    out.reserve(static_cast<std::size_t>(steps));
    for (int i = 0; i < steps; ++i) {
        const double r = (alternate && (i % 2 == 1)) ? inner : outer;
        const double a = start + sweep * static_cast<double>(i);
        out.push_back(ShapePoint{cx + rx * r * std::cos(a), cy + ry * r * std::sin(a)});
    }
}

/// A closed ellipse in the box (``x``, ``y``, ``w``, ``h``).
[[nodiscard]] ShapeSubpath ellipse_subpath(double x, double y, double w, double h)
{
    const double rx = w * 0.5;
    const double ry = h * 0.5;
    const double cx = x + rx;
    const double cy = y + ry;
    const double kx = rx * k_kappa;
    const double ky = ry * k_kappa;
    ShapeSubpath sub;
    sub.start = ShapePoint{cx, y};
    sub.segments = {
        cubic_to(cx + kx, y, x + w, cy - ky, x + w, cy),
        cubic_to(x + w, cy + ky, cx + kx, y + h, cx, y + h),
        cubic_to(cx - kx, y + h, x, cy + ky, x, cy),
        cubic_to(x, cy - ky, cx - kx, y, cx, y),
    };
    return sub;
}

/// A cloud callout: lobes round an ellipse filling the box but for its foot,
/// and three bubbles under it, left of the middle, shrinking as they go.
[[nodiscard]] ShapePath cloud_path(double w, double h)
{
    constexpr int k_bumps = 10;
    const double body_h = h * 0.86;
    const double cx = w * 0.5;
    const double cy = body_h * 0.5;
    // The lobes' ends sit inside; each rounds out to the box's edge. A cubic
    // reaches three quarters of the way to its control points, so those go
    // past the edge for the curve to come to it.
    const double rx = w * 0.40;
    const double ry = body_h * 0.40;
    const double bulge = 1.42;
    const double sweep = 2.0 * std::numbers::pi / k_bumps;
    const auto at = [&](double a, double r) {
        return ShapePoint{cx + rx * r * std::cos(a), cy + ry * r * std::sin(a)};
    };
    ShapeSubpath body;
    const double start = -std::numbers::pi / 2.0;
    body.start = at(start, 1.0);
    for (int i = 0; i < k_bumps; ++i) {
        const double a0 = start + sweep * i;
        const double a1 = a0 + sweep;
        const ShapePoint c1 = at(a0, bulge);
        const ShapePoint c2 = at(a1, bulge);
        const ShapePoint to = at(a1, 1.0);
        body.segments.push_back(cubic_to(c1.x, c1.y, c2.x, c2.y, to.x, to.y));
    }
    ShapePath path;
    path.subpaths.push_back(std::move(body));
    // As shares of the box: each bubble's middle, then its width and height.
    constexpr double k_bubbles[3][4] = {
        {0.33, 0.875, 0.09, 0.14}, {0.31, 0.925, 0.058, 0.09}, {0.29, 0.96, 0.036, 0.06}};
    for (const auto& b : k_bubbles) {
        path.subpaths.push_back(
            ellipse_subpath(w * (b[0] - b[2] * 0.5), h * (b[1] - b[3] * 0.5), w * b[2], h * b[3]));
    }
    return path;
}

/// A drawn outline: its points scaled to the box, a curve through each
/// smooth one (Catmull-Rom, as cubics), a corner at each other.
[[nodiscard]] ShapePath drawn_path(double w, double h, const ShapeAdjust& adj)
{
    const auto& pts = adj.path;
    ShapePath path;
    if (pts.size() < 2) {
        return path;
    }
    const auto n = pts.size();
    const auto p = [&](std::size_t i) { return ShapePoint{pts[i].x * w, pts[i].y * h}; };
    // The neighbour before and after a point, round the loop when closed.
    const auto before = [&](std::size_t i) {
        return i > 0 ? p(i - 1) : (adj.closed ? p(n - 1) : p(0));
    };
    const auto after = [&](std::size_t i) {
        return i + 1 < n ? p(i + 1) : (adj.closed ? p(0) : p(n - 1));
    };
    ShapeSubpath sub;
    sub.start = p(0);
    sub.closed = adj.closed;
    const std::size_t count = adj.closed ? n : n - 1;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t j = (i + 1) % n;
        const ShapePoint a = p(i);
        const ShapePoint b = p(j);
        if (!pts[i].smooth && !pts[j].smooth) {
            sub.segments.push_back(line_to(b.x, b.y));
            continue;
        }
        ShapePoint c1 = a;
        ShapePoint c2 = b;
        if (pts[i].smooth) {
            const ShapePoint pa = before(i);
            c1 = ShapePoint{a.x + (b.x - pa.x) / 6.0, a.y + (b.y - pa.y) / 6.0};
        }
        if (pts[j].smooth) {
            const ShapePoint nb = after(j);
            c2 = ShapePoint{b.x - (nb.x - a.x) / 6.0, b.y - (nb.y - a.y) / 6.0};
        }
        sub.segments.push_back(cubic_to(c1.x, c1.y, c2.x, c2.y, b.x, b.y));
    }
    path.subpaths.push_back(std::move(sub));
    return path;
}

} // namespace

std::string_view to_string(ShapeKind kind) noexcept
{
    switch (kind) {
    case ShapeKind::Rectangle:
        return "rectangle";
    case ShapeKind::RoundedRect:
        return "rounded_rect";
    case ShapeKind::Ellipse:
        return "ellipse";
    case ShapeKind::Triangle:
        return "triangle";
    case ShapeKind::RightTriangle:
        return "right_triangle";
    case ShapeKind::Rhombus:
        return "rhombus";
    case ShapeKind::Polygon:
        return "polygon";
    case ShapeKind::Star:
        return "star";
    case ShapeKind::Arrow:
        return "arrow";
    case ShapeKind::DoubleArrow:
        return "double_arrow";
    case ShapeKind::Cloud:
        return "cloud";
    case ShapeKind::Drawn:
        return "drawn";
    }
    return "rectangle";
}

ShapeKind shape_kind_from_string(std::string_view s) noexcept
{
    if (s == "rounded_rect") {
        return ShapeKind::RoundedRect;
    }
    if (s == "ellipse") {
        return ShapeKind::Ellipse;
    }
    if (s == "triangle") {
        return ShapeKind::Triangle;
    }
    if (s == "right_triangle") {
        return ShapeKind::RightTriangle;
    }
    if (s == "rhombus") {
        return ShapeKind::Rhombus;
    }
    if (s == "polygon") {
        return ShapeKind::Polygon;
    }
    if (s == "star") {
        return ShapeKind::Star;
    }
    if (s == "arrow") {
        return ShapeKind::Arrow;
    }
    if (s == "double_arrow") {
        return ShapeKind::DoubleArrow;
    }
    if (s == "cloud") {
        return ShapeKind::Cloud;
    }
    if (s == "drawn") {
        return ShapeKind::Drawn;
    }
    return ShapeKind::Rectangle;
}

const std::vector<ShapeKind>& all_shape_kinds()
{
    static const std::vector<ShapeKind> kinds{
        ShapeKind::Rectangle, ShapeKind::RoundedRect,   ShapeKind::Ellipse,
        ShapeKind::Triangle,  ShapeKind::RightTriangle, ShapeKind::Rhombus,
        ShapeKind::Polygon,   ShapeKind::Star,          ShapeKind::Arrow,
        ShapeKind::DoubleArrow, ShapeKind::Cloud,
    };
    return kinds;
}

bool uses_corner(ShapeKind kind) noexcept
{
    return kind == ShapeKind::RoundedRect;
}

bool uses_points(ShapeKind kind) noexcept
{
    return kind == ShapeKind::Polygon || kind == ShapeKind::Star;
}

bool uses_inner(ShapeKind kind) noexcept
{
    return kind == ShapeKind::Star;
}

bool uses_arrow(ShapeKind kind) noexcept
{
    return kind == ShapeKind::Arrow || kind == ShapeKind::DoubleArrow;
}

bool uses_path(ShapeKind kind) noexcept
{
    return kind == ShapeKind::Drawn;
}

bool is_open(ShapeKind kind, const ShapeAdjust& adjust) noexcept
{
    return uses_path(kind) && !adjust.closed;
}

double head_max(ShapeKind kind) noexcept
{
    // A double arrow spends its head budget twice; cap each at half the width.
    return kind == ShapeKind::DoubleArrow ? 0.5 : 1.0;
}

ShapeAdjust clamp_adjust(ShapeKind kind, ShapeAdjust adjust) noexcept
{
    adjust.corner = std::clamp(adjust.corner, k_corner_min, k_corner_max);
    adjust.points = std::clamp(adjust.points, k_points_min, k_points_max);
    adjust.inner = std::clamp(adjust.inner, k_inner_min, k_inner_max);
    adjust.head = std::clamp(adjust.head, k_head_min, head_max(kind));
    adjust.thickness = std::clamp(adjust.thickness, k_thickness_min, k_thickness_max);
    return adjust;
}

ShapePath build_shape_path(ShapeKind kind, double w, double h, const ShapeAdjust& raw)
{
    if (!(w > 0.0) || !(h > 0.0)) {
        return {};
    }
    const ShapeAdjust adj = clamp_adjust(kind, raw);

    switch (kind) {
    case ShapeKind::Rectangle:
        return polygon_path({{0, 0}, {w, 0}, {w, h}, {0, h}});

    case ShapeKind::RoundedRect: {
        const double r = adj.corner * std::min(w, h);
        if (r <= 0.0) {
            return polygon_path({{0, 0}, {w, 0}, {w, h}, {0, h}});
        }
        const double k = r * k_kappa;
        ShapeSubpath sub;
        sub.start = ShapePoint{r, 0};
        sub.segments = {
            line_to(w - r, 0),
            cubic_to(w - r + k, 0, w, r - k, w, r),
            line_to(w, h - r),
            cubic_to(w, h - r + k, w - r + k, h, w - r, h),
            line_to(r, h),
            cubic_to(r - k, h, 0, h - r + k, 0, h - r),
            line_to(0, r),
            cubic_to(0, r - k, r - k, 0, r, 0),
        };
        ShapePath path;
        path.subpaths.push_back(std::move(sub));
        return path;
    }

    case ShapeKind::Ellipse: {
        const double rx = w * 0.5;
        const double ry = h * 0.5;
        const double cx = rx;
        const double cy = ry;
        const double kx = rx * k_kappa;
        const double ky = ry * k_kappa;
        ShapeSubpath sub;
        sub.start = ShapePoint{cx, 0};
        sub.segments = {
            cubic_to(cx + kx, 0, w, cy - ky, w, cy),
            cubic_to(w, cy + ky, cx + kx, h, cx, h),
            cubic_to(cx - kx, h, 0, cy + ky, 0, cy),
            cubic_to(0, cy - ky, cx - kx, 0, cx, 0),
        };
        ShapePath path;
        path.subpaths.push_back(std::move(sub));
        return path;
    }

    case ShapeKind::Triangle:
        return polygon_path({{w * 0.5, 0}, {w, h}, {0, h}});

    case ShapeKind::RightTriangle:
        // Right angle at the bottom-left; hypotenuse top-left -> bottom-right.
        return polygon_path({{0, 0}, {w, h}, {0, h}});

    case ShapeKind::Rhombus:
        return polygon_path({{w * 0.5, 0}, {w, h * 0.5}, {w * 0.5, h}, {0, h * 0.5}});

    case ShapeKind::Polygon: {
        std::vector<ShapePoint> pts;
        radial_vertices(pts, w, h, adj.points, 1.0, 1.0, false);
        return polygon_path(pts);
    }

    case ShapeKind::Star: {
        std::vector<ShapePoint> pts;
        radial_vertices(pts, w, h, adj.points, 1.0, adj.inner, true);
        return polygon_path(pts);
    }

    case ShapeKind::Arrow: {
        const double shaft = adj.thickness * h;
        const double top = (h - shaft) * 0.5;
        const double bottom = top + shaft;
        const double neck = w - adj.head * w;
        return polygon_path({{0, top},
                             {neck, top},
                             {neck, 0},
                             {w, h * 0.5},
                             {neck, h},
                             {neck, bottom},
                             {0, bottom}});
    }

    case ShapeKind::DoubleArrow: {
        const double shaft = adj.thickness * h;
        const double top = (h - shaft) * 0.5;
        const double bottom = top + shaft;
        const double head = adj.head * w;
        const double left_neck = head;
        const double right_neck = w - head;
        return polygon_path({{0, h * 0.5},
                             {left_neck, 0},
                             {left_neck, top},
                             {right_neck, top},
                             {right_neck, 0},
                             {w, h * 0.5},
                             {right_neck, h},
                             {right_neck, bottom},
                             {left_neck, bottom},
                             {left_neck, h}});
    }

    case ShapeKind::Cloud:
        return cloud_path(w, h);

    case ShapeKind::Drawn:
        return drawn_path(w, h, adj);
    }
    return polygon_path({{0, 0}, {w, 0}, {w, h}, {0, h}});
}

} // namespace seventhslide::geom
