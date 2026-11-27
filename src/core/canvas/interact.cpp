#include "seventhslide/canvas/interact.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <optional>
#include <string>
#include <utility>

namespace seventhslide::canvas {
namespace {

/// The sides of a box a handle drags.
struct HandleSides {
    bool west = false;
    bool east = false;
    bool north = false;
    bool south = false;
};

[[nodiscard]] constexpr HandleSides sides_of(Handle handle) noexcept
{
    switch (handle) {
    case Handle::N:
        return {.north = true};
    case Handle::NE:
        return {.east = true, .north = true};
    case Handle::E:
        return {.east = true};
    case Handle::SE:
        return {.east = true, .south = true};
    case Handle::S:
        return {.south = true};
    case Handle::SW:
        return {.west = true, .south = true};
    case Handle::W:
        return {.west = true};
    case Handle::NW:
        return {.west = true, .north = true};
    case Handle::None:
    case Handle::Pending:
    case Handle::Move:
        break;
    }
    return {};
}

[[nodiscard]] bool finite_pos(double v) noexcept
{
    return std::isfinite(v);
}

void rotate_about(double cx, double cy, double degrees, double& x, double& y) noexcept
{
    if (degrees == 0.0) {
        return;
    }
    const double rad = degrees * (std::numbers::pi / 180.0);
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    const double dx = x - cx;
    const double dy = y - cy;
    x = cx + dx * c - dy * s;
    y = cy + dx * s + dy * c;
}

} // namespace

std::string_view to_string(Handle handle) noexcept
{
    switch (handle) {
    case Handle::Pending:
        return "pending";
    case Handle::Move:
        return "move";
    case Handle::N:
        return "n";
    case Handle::NE:
        return "ne";
    case Handle::E:
        return "e";
    case Handle::SE:
        return "se";
    case Handle::S:
        return "s";
    case Handle::SW:
        return "sw";
    case Handle::W:
        return "w";
    case Handle::NW:
        return "nw";
    case Handle::None:
        break;
    }
    return "";
}

Handle handle_from_string(std::string_view s) noexcept
{
    if (s == "pending") {
        return Handle::Pending;
    }
    if (s == "move") {
        return Handle::Move;
    }
    if (s == "n") {
        return Handle::N;
    }
    if (s == "ne") {
        return Handle::NE;
    }
    if (s == "e") {
        return Handle::E;
    }
    if (s == "se") {
        return Handle::SE;
    }
    if (s == "s") {
        return Handle::S;
    }
    if (s == "sw") {
        return Handle::SW;
    }
    if (s == "w") {
        return Handle::W;
    }
    if (s == "nw") {
        return Handle::NW;
    }
    return Handle::None;
}

bool point_in_rect(double cx, double cy, const Rect& rect) noexcept
{
    double x = cx;
    double y = cy;
    if (rect.rotation != 0.0) {
        const double ocx = rect.x + rect.w * 0.5;
        const double ocy = rect.y + rect.h * 0.5;
        rotate_about(ocx, ocy, -rect.rotation, x, y);
    }
    return x >= rect.x && y >= rect.y && x <= rect.x + rect.w && y <= rect.y + rect.h;
}

Handle hit_handle(double stage_x, double stage_y, const Rect& canvas_rect, double scale,
                  double hit_px) noexcept
{
    const double s = scale > 0.0 && finite_pos(scale) ? scale : 1.0;
    const double hit = hit_px > 0.0 ? hit_px : k_default_handle_hit;
    const double x = canvas_rect.x;
    const double y = canvas_rect.y;
    const double w = canvas_rect.w;
    const double h = canvas_rect.h;
    const double cx = (x + w / 2.0) * s;
    const double cy = (y + h / 2.0) * s;
    struct Spec {
        Handle handle;
        double hx;
        double hy;
    };
    Spec specs[] = {
        {Handle::NW, x * s, y * s},
        {Handle::N, (x + w / 2.0) * s, y * s},
        {Handle::NE, (x + w) * s, y * s},
        {Handle::E, (x + w) * s, (y + h / 2.0) * s},
        {Handle::SE, (x + w) * s, (y + h) * s},
        {Handle::S, (x + w / 2.0) * s, (y + h) * s},
        {Handle::SW, x * s, (y + h) * s},
        {Handle::W, x * s, (y + h / 2.0) * s},
    };
    if (canvas_rect.rotation != 0.0) {
        for (auto& spec : specs) {
            rotate_about(cx, cy, canvas_rect.rotation, spec.hx, spec.hy);
        }
    }
    for (const auto& spec : specs) {
        if (std::abs(stage_x - spec.hx) <= hit && std::abs(stage_y - spec.hy) <= hit) {
            return spec.handle;
        }
    }
    return Handle::None;
}

const Rect* hit_object(double cx, double cy, const std::vector<Rect>& objects)
{
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
        if (point_in_rect(cx, cy, *it)) {
            return &*it;
        }
    }
    return nullptr;
}

Rect clamp_rect(Rect rect, geom::CanvasSize canvas, const Limits& limits)
{
    const double min_w = limits.min_w > 0.0 ? limits.min_w : k_min_width;
    const double min_h = limits.min_h > 0.0 ? limits.min_h : k_min_height;
    const double hang = limits.overhang >= 0.0 ? limits.overhang : k_overhang;
    const auto [cw, ch] = geom::canvas_or_reference(canvas);
    rect.w = std::max(min_w, rect.w);
    rect.h = std::max(min_h, rect.h);
    if (rect.x + rect.w < hang) {
        rect.x = hang - rect.w;
    }
    if (rect.y + rect.h < hang) {
        rect.y = hang - rect.h;
    }
    if (rect.x > cw - hang) {
        rect.x = cw - hang;
    }
    if (rect.y > ch - hang) {
        rect.y = ch - hang;
    }
    return rect;
}

Rect geometry_from_drag(Handle mode, const Rect& start, double dx, double dy,
                        geom::CanvasSize canvas, const Limits& limits)
{
    const double min_w = limits.min_w > 0.0 ? limits.min_w : k_min_width;
    const double min_h = limits.min_h > 0.0 ? limits.min_h : k_min_height;
    Handle effective = mode;
    if (effective == Handle::Pending || effective == Handle::None) {
        effective = Handle::Move;
    }
    Rect next = start;
    if (effective == Handle::Move) {
        next.x = start.x + dx;
        next.y = start.y + dy;
        return clamp_rect(std::move(next), canvas, limits);
    }
    const auto [west, east, north, south] = sides_of(effective);
    if (west) {
        next.x = start.x + dx;
        next.w = start.w - dx;
    }
    if (east) {
        next.w = start.w + dx;
    }
    if (north) {
        next.y = start.y + dy;
        next.h = start.h - dy;
    }
    if (south) {
        next.h = start.h + dy;
    }
    if (next.w < min_w) {
        if (west) {
            next.x = start.x + start.w - min_w;
        }
        next.w = min_w;
    }
    if (next.h < min_h) {
        if (north) {
            next.y = start.y + start.h - min_h;
        }
        next.h = min_h;
    }
    return clamp_rect(std::move(next), canvas, limits);
}

AxisTargets snap_axis_targets(geom::CanvasSize canvas, const std::vector<Rect>& objects,
                              const std::vector<std::string>& exclude_ids)
{
    const auto [cw, ch] = geom::canvas_or_reference(canvas);
    AxisTargets out;
    // Canvas targets come first, so on an equal-distance tie the canvas guide
    // wins over a sibling's.
    out.xs = {0.0, cw / 2.0, static_cast<double>(cw)};
    out.ys = {0.0, ch / 2.0, static_cast<double>(ch)};
    for (const auto& obj : objects) {
        if (std::find(exclude_ids.begin(), exclude_ids.end(), obj.id) != exclude_ids.end()) {
            continue;
        }
        out.xs.push_back(obj.x);
        out.xs.push_back(obj.x + obj.w / 2.0);
        out.xs.push_back(obj.x + obj.w);
        out.ys.push_back(obj.y);
        out.ys.push_back(obj.y + obj.h / 2.0);
        out.ys.push_back(obj.y + obj.h);
    }
    return out;
}

std::optional<double> nearest_snap(double value, const std::vector<double>& targets,
                                   double threshold) noexcept
{
    std::optional<double> best;
    double best_dist = threshold + 1.0;
    for (double t : targets) {
        const double d = std::abs(value - t);
        if (d <= threshold && d < best_dist) {
            best_dist = d;
            best = t;
        }
    }
    return best;
}

namespace {

/// Where one axis of a moving box snaps: the guide it meets, and the box's
/// new start on that axis.
struct SpanSnap {
    double guide = 0;
    double start = 0;
};

/// Snap the span ``start`` .. ``start + size`` on one axis: its start, centre
/// and end are tried against ``guides``, and the closest within ``threshold``
/// wins, the earlier of those three on a tie. Nothing when none is in reach.
[[nodiscard]] std::optional<SpanSnap> snap_span(double start, double size,
                                                const std::vector<double>& guides,
                                                double threshold)
{
    std::optional<SpanSnap> best;
    double best_dist = threshold + 1.0;
    for (const double offset : {0.0, size / 2.0, size}) {
        const double edge = start + offset;
        const auto g = nearest_snap(edge, guides, threshold);
        if (!g) {
            continue;
        }
        const double dist = std::abs(edge - *g);
        if (dist < best_dist) {
            best_dist = dist;
            best = SpanSnap{*g, *g - offset};
        }
    }
    return best;
}

} // namespace

SnapResult snap_rect(const Rect& rect, Handle mode, const AxisTargets& targets,
                     geom::CanvasSize canvas, bool enabled, const Limits& limits)
{
    SnapResult out;
    out.rect = rect;
    if (!enabled) {
        out.rect = clamp_rect(out.rect, canvas, limits);
        return out;
    }
    const double thr = limits.snap_threshold > 0.0 ? limits.snap_threshold : k_default_snap;
    const double min_w = limits.min_w > 0.0 ? limits.min_w : k_min_width;
    const double min_h = limits.min_h > 0.0 ? limits.min_h : k_min_height;
    Handle effective = mode;
    if (effective == Handle::Pending) {
        effective = Handle::Move;
    }
    if (effective == Handle::Move || effective == Handle::None) {
        if (const auto hit = snap_span(rect.x, rect.w, targets.xs, thr)) {
            out.v_guide = hit->guide;
            out.rect.x = hit->start;
        }
        if (const auto hit = snap_span(rect.y, rect.h, targets.ys, thr)) {
            out.h_guide = hit->guide;
            out.rect.y = hit->start;
        }
    } else {
        const auto sides = sides_of(effective);
        if (sides.west) {
            if (const auto g = nearest_snap(rect.x, targets.xs, thr)) {
                const double right = rect.x + rect.w;
                out.rect.x = *g;
                out.rect.w = std::max(min_w, right - *g);
                out.v_guide = *g;
            }
        }
        if (sides.east) {
            const double right = rect.x + rect.w;
            if (const auto g = nearest_snap(right, targets.xs, thr)) {
                out.rect.w = std::max(min_w, *g - rect.x);
                out.v_guide = *g;
            }
        }
        if (sides.north) {
            if (const auto g = nearest_snap(rect.y, targets.ys, thr)) {
                const double bottom = rect.y + rect.h;
                out.rect.y = *g;
                out.rect.h = std::max(min_h, bottom - *g);
                out.h_guide = *g;
            }
        }
        if (sides.south) {
            const double bottom = rect.y + rect.h;
            if (const auto g = nearest_snap(bottom, targets.ys, thr)) {
                out.rect.h = std::max(min_h, *g - rect.y);
                out.h_guide = *g;
            }
        }
    }
    out.rect = clamp_rect(out.rect, canvas, limits);
    return out;
}

} // namespace seventhslide::canvas
