#include "seventhslide/canvas/layout.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace seventhslide::canvas {
namespace {

constexpr double k_pi = 3.14159265358979323846;

[[nodiscard]] double deg_to_rad(double deg) noexcept
{
    return deg * (k_pi / 180.0);
}

[[nodiscard]] bool any_locked(const std::vector<Rect>& rects) noexcept
{
    return std::any_of(rects.begin(), rects.end(), [](const Rect& r) { return r.locked; });
}

void rotate_about(double cx, double cy, double degrees, double& x, double& y) noexcept
{
    if (degrees == 0.0) {
        return;
    }
    const double rad = deg_to_rad(degrees);
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    const double dx = x - cx;
    const double dy = y - cy;
    x = cx + dx * c - dy * s;
    y = cy + dx * s + dy * c;
}

[[nodiscard]] Rect with_delta(Rect r, double dx, double dy) noexcept
{
    r.x += dx;
    r.y += dy;
    return r;
}

} // namespace

Rect aabb(const Rect& rect) noexcept
{
    if (rect.rotation == 0.0) {
        return rect;
    }
    const double cx = rect.x + rect.w * 0.5;
    const double cy = rect.y + rect.h * 0.5;
    const double xs[4] = {rect.x, rect.x + rect.w, rect.x + rect.w, rect.x};
    const double ys[4] = {rect.y, rect.y, rect.y + rect.h, rect.y + rect.h};
    double min_x = cx;
    double min_y = cy;
    double max_x = cx;
    double max_y = cy;
    for (int i = 0; i < 4; ++i) {
        double x = xs[i];
        double y = ys[i];
        rotate_about(cx, cy, rect.rotation, x, y);
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
    }
    Rect out = rect;
    out.x = min_x;
    out.y = min_y;
    out.w = max_x - min_x;
    out.h = max_y - min_y;
    out.rotation = 0;
    return out;
}

Rect union_bounds(const std::vector<Rect>& rects) noexcept
{
    if (rects.empty()) {
        return {};
    }
    Rect box = aabb(rects.front());
    double min_x = box.x;
    double min_y = box.y;
    double max_x = box.x + box.w;
    double max_y = box.y + box.h;
    for (std::size_t i = 1; i < rects.size(); ++i) {
        const Rect b = aabb(rects[i]);
        min_x = std::min(min_x, b.x);
        min_y = std::min(min_y, b.y);
        max_x = std::max(max_x, b.x + b.w);
        max_y = std::max(max_y, b.y + b.h);
    }
    Rect out;
    out.x = min_x;
    out.y = min_y;
    out.w = std::max(0.0, max_x - min_x);
    out.h = std::max(0.0, max_y - min_y);
    return out;
}

Rect normalized_rect(Rect rect) noexcept
{
    if (rect.w < 0.0) {
        rect.x += rect.w;
        rect.w = -rect.w;
    }
    if (rect.h < 0.0) {
        rect.y += rect.h;
        rect.h = -rect.h;
    }
    return rect;
}

bool rects_intersect(const Rect& a, const Rect& b) noexcept
{
    const Rect aa = aabb(a);
    const Rect bb = aabb(b);
    return aa.x <= bb.x + bb.w && bb.x <= aa.x + aa.w && aa.y <= bb.y + bb.h && bb.y <= aa.y + aa.h;
}

std::vector<std::string> hit_marquee(const Rect& marquee, const std::vector<Rect>& objects)
{
    const Rect box = normalized_rect(marquee);
    std::vector<std::string> ids;
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
        if (rects_intersect(box, *it)) {
            ids.push_back(it->id);
        }
    }
    return ids;
}

std::vector<std::string> expand_groups(
    const std::vector<std::pair<std::string, std::string>>& members,
    const std::vector<std::string>& picked)
{
    std::unordered_set<std::string> want(picked.begin(), picked.end());
    std::unordered_set<std::string> groups;
    for (const auto& [id, group] : members) {
        if (want.contains(id) && !group.empty()) {
            groups.insert(group);
        }
    }
    if (groups.empty() && picked.size() <= 1) {
        return picked;
    }
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& [id, group] : members) {
        const bool take = want.contains(id) || (!group.empty() && groups.contains(group));
        if (!take || seen.contains(id)) {
            continue;
        }
        seen.insert(id);
        out.push_back(id);
    }
    return out;
}

std::vector<Rect> geometry_from_drag_many(Handle mode, const std::vector<Rect>& start, double dx,
                                          double dy, geom::CanvasSize canvas, const Limits& limits)
{
    if (start.empty() || any_locked(start)) {
        return start;
    }
    Handle effective = mode;
    if (effective == Handle::Pending || effective == Handle::None) {
        effective = Handle::Move;
    }
    if (start.size() == 1) {
        return {geometry_from_drag(effective, start.front(), dx, dy, canvas, limits)};
    }
    const Rect origin = union_bounds(start);
    if (origin.w <= 0.0 || origin.h <= 0.0) {
        return start;
    }
    const Rect next = geometry_from_drag(effective, origin, dx, dy, canvas, limits);
    if (effective == Handle::Move) {
        const double mx = next.x - origin.x;
        const double my = next.y - origin.y;
        std::vector<Rect> out;
        out.reserve(start.size());
        for (const Rect& r : start) {
            out.push_back(clamp_rect(with_delta(r, mx, my), canvas, limits));
        }
        return out;
    }
    const double sx = next.w / origin.w;
    const double sy = next.h / origin.h;
    std::vector<Rect> out;
    out.reserve(start.size());
    for (const Rect& r : start) {
        Rect n = r;
        n.x = next.x + (r.x - origin.x) * sx;
        n.y = next.y + (r.y - origin.y) * sy;
        n.w = r.w * sx;
        n.h = r.h * sy;
        out.push_back(clamp_rect(std::move(n), canvas, limits));
    }
    return out;
}

SnapManyResult snap_many(const std::vector<Rect>& rects, Handle mode, const AxisTargets& targets,
                         geom::CanvasSize canvas, bool enabled, const Limits& limits)
{
    SnapManyResult out;
    if (rects.empty()) {
        return out;
    }
    if (rects.size() == 1) {
        const auto one = snap_rect(rects.front(), mode, targets, canvas, enabled, limits);
        out.rects = {one.rect};
        out.v_guide = one.v_guide;
        out.h_guide = one.h_guide;
        return out;
    }
    const Rect origin = union_bounds(rects);
    const auto snapped = snap_rect(origin, mode, targets, canvas, enabled, limits);
    out.v_guide = snapped.v_guide;
    out.h_guide = snapped.h_guide;
    out.rects = geometry_from_drag_many(mode, rects, snapped.rect.x - origin.x,
                                        snapped.rect.y - origin.y, canvas, limits);
    Handle effective = mode;
    if (effective == Handle::Pending || effective == Handle::None) {
        effective = Handle::Move;
    }
    if (effective != Handle::Move && origin.w > 0.0 && origin.h > 0.0) {
        out.rects.clear();
        const double sx = snapped.rect.w / origin.w;
        const double sy = snapped.rect.h / origin.h;
        for (const Rect& r : rects) {
            Rect n = r;
            n.x = snapped.rect.x + (r.x - origin.x) * sx;
            n.y = snapped.rect.y + (r.y - origin.y) * sy;
            n.w = r.w * sx;
            n.h = r.h * sy;
            out.rects.push_back(clamp_rect(std::move(n), canvas, limits));
        }
    }
    return out;
}

std::vector<Rect> align_rects(std::vector<Rect> rects, Align align, const Rect& frame)
{
    if (rects.empty() || any_locked(rects)) {
        return rects;
    }
    const Rect relative = rects.size() == 1 ? frame : union_bounds(rects);
    for (Rect& r : rects) {
        const Rect box = aabb(r);
        double dx = 0;
        double dy = 0;
        switch (align) {
        case Align::Left:
            dx = relative.x - box.x;
            break;
        case Align::Center:
            dx = (relative.x + relative.w * 0.5) - (box.x + box.w * 0.5);
            break;
        case Align::Right:
            dx = (relative.x + relative.w) - (box.x + box.w);
            break;
        case Align::Top:
            dy = relative.y - box.y;
            break;
        case Align::Middle:
            dy = (relative.y + relative.h * 0.5) - (box.y + box.h * 0.5);
            break;
        case Align::Bottom:
            dy = (relative.y + relative.h) - (box.y + box.h);
            break;
        }
        r.x += dx;
        r.y += dy;
    }
    return rects;
}

std::vector<Rect> distribute_rects(std::vector<Rect> rects, Distribute dist)
{
    if (rects.size() < 3 || any_locked(rects)) {
        return rects;
    }
    std::vector<std::size_t> order(rects.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    if (dist == Distribute::Horizontal) {
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return aabb(rects[a]).x < aabb(rects[b]).x;
        });
        const Rect first = aabb(rects[order.front()]);
        const Rect last = aabb(rects[order.back()]);
        double span = 0;
        for (std::size_t i = 1; i + 1 < order.size(); ++i) {
            span += aabb(rects[order[i]]).w;
        }
        const double gap_run = (last.x) - (first.x + first.w);
        const double gap =
            (order.size() > 1) ? (gap_run - span) / static_cast<double>(order.size() - 1) : 0;
        double cursor = first.x + first.w + gap;
        for (std::size_t i = 1; i + 1 < order.size(); ++i) {
            Rect& r = rects[order[i]];
            const Rect box = aabb(r);
            r.x += cursor - box.x;
            cursor += box.w + gap;
        }
    } else {
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return aabb(rects[a]).y < aabb(rects[b]).y;
        });
        const Rect first = aabb(rects[order.front()]);
        const Rect last = aabb(rects[order.back()]);
        double span = 0;
        for (std::size_t i = 1; i + 1 < order.size(); ++i) {
            span += aabb(rects[order[i]]).h;
        }
        const double gap_run = (last.y) - (first.y + first.h);
        const double gap =
            (order.size() > 1) ? (gap_run - span) / static_cast<double>(order.size() - 1) : 0;
        double cursor = first.y + first.h + gap;
        for (std::size_t i = 1; i + 1 < order.size(); ++i) {
            Rect& r = rects[order[i]];
            const Rect box = aabb(r);
            r.y += cursor - box.y;
            cursor += box.h + gap;
        }
    }
    return rects;
}

std::vector<Rect> nudge_rects(std::vector<Rect> rects, double dx, double dy,
                              geom::CanvasSize canvas, const Limits& limits)
{
    if (rects.empty() || any_locked(rects) || (dx == 0.0 && dy == 0.0)) {
        return rects;
    }
    for (Rect& r : rects) {
        r = clamp_rect(with_delta(r, dx, dy), canvas, limits);
    }
    return rects;
}

std::vector<Rect> flip_rects(std::vector<Rect> rects, FlipAxis axis)
{
    if (rects.empty() || any_locked(rects)) {
        return rects;
    }
    const Rect box = union_bounds(rects);
    const double cx = box.x + box.w * 0.5;
    const double cy = box.y + box.h * 0.5;
    for (Rect& r : rects) {
        if (axis == FlipAxis::Horizontal) {
            r.x = 2.0 * cx - r.x - r.w;
            r.rotation = -r.rotation;
            r.flip_h = !r.flip_h;
        } else {
            r.y = 2.0 * cy - r.y - r.h;
            r.rotation = -r.rotation;
            r.flip_v = !r.flip_v;
        }
    }
    return rects;
}

} // namespace seventhslide::canvas
