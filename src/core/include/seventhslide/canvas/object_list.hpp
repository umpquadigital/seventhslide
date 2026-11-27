#pragma once

// Operations on one stacking list of canvas objects, which ObjectEditor runs
// for both canvas editors: a Freeform slide (``std::vector<freeform::Object>``)
// and a text theme's design context (``std::vector<theme::Element>``, each
// wrapping an Object).
//
// A list item is anything ``object_of(item)`` maps to a ``canvas::Object``;
// the overloads for Object are below, and each editor provides one for its
// item, found by argument-dependent lookup.
//
// Lists are kept back-to-front: the ends of the vector are the ends of the
// stack.
//
// Seam: plain C++20 (no Qt).

#include "seventhslide/canvas/edit_result.hpp"
#include "seventhslide/canvas/layout.hpp"
#include "seventhslide/canvas/object.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace seventhslide::canvas {

[[nodiscard]] constexpr Object& object_of(Object& obj) noexcept
{
    return obj;
}

[[nodiscard]] constexpr const Object& object_of(const Object& obj) noexcept
{
    return obj;
}

namespace detail {

[[nodiscard]] inline bool holds_id(const std::vector<std::string>& ids, const std::string& id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace detail

/// Any of ``ids`` is locked.
template <class List>
[[nodiscard]] bool list_selection_locked(const List& list, const std::vector<std::string>& ids)
{
    return std::any_of(list.begin(), list.end(), [&](const auto& item) {
        const auto& obj = object_of(item);
        return obj.locked && detail::holds_id(ids, obj.id);
    });
}

/// Any of ``ids`` belongs to a group.
template <class List>
[[nodiscard]] bool list_selection_grouped(const List& list, const std::vector<std::string>& ids)
{
    return std::any_of(list.begin(), list.end(), [&](const auto& item) {
        const auto& obj = object_of(item);
        return !obj.group_id.empty() && detail::holds_id(ids, obj.id);
    });
}

/// True when ``list_reorder(list, ids, op)`` would change the stack.
template <class List>
[[nodiscard]] bool list_can_reorder(const List& list, const std::vector<std::string>& ids,
                                    ZOrderOp op)
{
    const std::size_t n = list.size();
    const auto is_picked = [&](std::size_t i) {
        return detail::holds_id(ids, object_of(list[i]).id);
    };
    std::size_t count = 0;
    for (std::size_t i = 0; i < n; ++i) {
        count += is_picked(i) ? 1 : 0;
    }
    if (count == 0 || count == n) {
        return false;
    }
    // Already flush against the end it would move toward.
    const bool up = op == ZOrderOp::ToFront || op == ZOrderOp::Forward;
    const std::size_t from = up ? n - count : 0;
    for (std::size_t i = from; i < from + count; ++i) {
        if (!is_picked(i)) {
            return true;
        }
    }
    return false;
}

/// Move ``ids`` through the stack. The moved items keep their relative order;
/// ``Forward``/``Backward`` step one unselected neighbour at a time so a
/// multi-selection travels as a block. Leaves ``z`` to the caller. False when
/// no item is in ``ids``.
template <class List>
bool list_reorder(List& list, const std::vector<std::string>& ids, ZOrderOp op)
{
    const std::size_t n = list.size();
    // ``char`` rather than ``bool``: this is swapped alongside the items.
    std::vector<char> picked(n, 0);
    std::size_t count = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (detail::holds_id(ids, object_of(list[i]).id)) {
            picked[i] = 1;
            ++count;
        }
    }
    if (count == 0) {
        return false;
    }
    switch (op) {
    case ZOrderOp::ToFront:
    case ZOrderOp::ToBack: {
        const bool front = op == ZOrderOp::ToFront;
        List next;
        next.reserve(n);
        // The moved run goes last for ToFront, first for ToBack.
        for (std::size_t pass = 0; pass < 2; ++pass) {
            const bool want = front ? pass == 1 : pass == 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (static_cast<bool>(picked[i]) == want) {
                    next.push_back(std::move(list[i]));
                }
            }
        }
        list = std::move(next);
        break;
    }
    case ZOrderOp::Forward:
        // Top-down so a contiguous selection shifts up together.
        for (std::size_t i = n - 1; i-- > 0;) {
            if (picked[i] && !picked[i + 1]) {
                std::swap(list[i], list[i + 1]);
                std::swap(picked[i], picked[i + 1]);
            }
        }
        break;
    case ZOrderOp::Backward:
        for (std::size_t i = 1; i < n; ++i) {
            if (picked[i] && !picked[i - 1]) {
                std::swap(list[i], list[i - 1]);
                std::swap(picked[i], picked[i - 1]);
            }
        }
        break;
    }
    return true;
}

/// Groups that would be left with one member lose the tag.
template <class List>
void list_prune_singleton_groups(List& list)
{
    std::map<std::string, int> counts;
    for (const auto& item : list) {
        if (const auto& gid = object_of(item).group_id; !gid.empty()) {
            counts[gid] += 1;
        }
    }
    for (auto& item : list) {
        auto& obj = object_of(item);
        if (!obj.group_id.empty() && counts[obj.group_id] < 2) {
            obj.group_id.clear();
        }
    }
}

/// Tag ``ids`` with ``group_id`` (flattening any groups they belonged to).
template <class List>
[[nodiscard]] EditResult list_group(List& list, const std::vector<std::string>& ids,
                                    const std::string& group_id)
{
    // Count before writing: a failed call must leave the list untouched.
    const auto found = std::count_if(list.begin(), list.end(), [&](const auto& item) {
        return detail::holds_id(ids, object_of(item).id);
    });
    if (found < 2 || group_id.empty()) {
        return EditResult::fail("select two or more objects to group");
    }
    for (auto& item : list) {
        auto& obj = object_of(item);
        if (detail::holds_id(ids, obj.id)) {
            obj.group_id = group_id;
        }
    }
    list_prune_singleton_groups(list);
    return EditResult::success();
}

/// Clear ``group_id`` on every member of any group that intersects ``ids``.
template <class List>
[[nodiscard]] EditResult list_ungroup(List& list, const std::vector<std::string>& ids)
{
    if (ids.empty()) {
        return EditResult::fail("nothing selected");
    }
    std::vector<std::string> groups;
    for (const auto& item : list) {
        const auto& obj = object_of(item);
        if (!obj.group_id.empty() && detail::holds_id(ids, obj.id)
            && !detail::holds_id(groups, obj.group_id)) {
            groups.push_back(obj.group_id);
        }
    }
    if (groups.empty()) {
        return EditResult::fail("selection is not grouped");
    }
    for (auto& item : list) {
        auto& obj = object_of(item);
        if (detail::holds_id(groups, obj.group_id)) {
            obj.group_id.clear();
        }
    }
    return EditResult::success();
}

template <class List>
[[nodiscard]] EditResult list_set_locked(List& list, const std::vector<std::string>& ids,
                                         bool locked)
{
    if (ids.empty()) {
        return EditResult::fail("nothing selected");
    }
    bool any = false;
    for (auto& item : list) {
        auto& obj = object_of(item);
        if (detail::holds_id(ids, obj.id)) {
            obj.locked = locked;
            any = true;
        }
    }
    return any ? EditResult::success() : EditResult::fail("object not found");
}

/// Remove ``ids``; a group left with one member is dissolved.
template <class List>
[[nodiscard]] EditResult list_erase(List& list, const std::vector<std::string>& ids)
{
    if (ids.empty()) {
        return EditResult::fail("nothing selected");
    }
    const auto before = list.size();
    std::erase_if(list,
                  [&](const auto& item) { return detail::holds_id(ids, object_of(item).id); });
    if (list.size() == before) {
        return EditResult::fail("object not found");
    }
    list_prune_singleton_groups(list);
    return EditResult::success();
}

/// How ``list_set_geometries`` stores coordinates.

/// Write geometry for many objects in one pass. Missing ids are skipped; a
/// locked target fails the whole call before anything is written. Sizes never
/// drop below 8 px.
template <class List>
[[nodiscard]] EditResult list_set_geometries(List& list,
                                             const std::vector<ObjectGeometry>& geometries,
                                             GeometryUnits units)
{
    if (geometries.empty()) {
        return EditResult::fail("nothing selected");
    }
    std::vector<Object*> targets;
    targets.reserve(geometries.size());
    bool any = false;
    for (const auto& g : geometries) {
        Object* found = nullptr;
        for (auto& item : list) {
            auto& obj = object_of(item);
            if (obj.id == g.id) {
                if (obj.locked) {
                    return EditResult::fail("selection is locked");
                }
                found = &obj;
                any = true;
                break;
            }
        }
        targets.push_back(found);
    }
    if (!any) {
        return EditResult::fail("object not found");
    }
    const auto unit = [&](double v) {
        return units == GeometryUnits::WholePixels ? std::round(v) : v;
    };
    for (std::size_t i = 0; i < geometries.size(); ++i) {
        Object* obj = targets[i];
        if (obj == nullptr) {
            continue;
        }
        const auto& g = geometries[i];
        obj->x = unit(g.x);
        obj->y = unit(g.y);
        obj->w = std::max(8.0, unit(g.w));
        obj->h = std::max(8.0, unit(g.h));
        obj->rotation = g.rotation;
        if (g.has_flip) {
            obj->flip_h = g.flip_h;
            obj->flip_v = g.flip_v;
        }
    }
    return EditResult::success();
}

[[nodiscard]] inline canvas::Rect rect_of(const Object& obj)
{
    canvas::Rect r;
    r.id = obj.id;
    r.x = obj.x;
    r.y = obj.y;
    r.w = obj.w;
    r.h = obj.h;
    r.rotation = obj.rotation;
    r.locked = obj.locked;
    r.flip_h = obj.flip_h;
    r.flip_v = obj.flip_v;
    return r;
}

/// Rects of ``ids``, in selection order; ids not in the list are skipped.
template <class List>
[[nodiscard]] std::vector<canvas::Rect> list_rects(const List& list,
                                                   const std::vector<std::string>& ids)
{
    std::vector<canvas::Rect> out;
    out.reserve(ids.size());
    for (const auto& id : ids) {
        const auto it = std::find_if(list.begin(), list.end(),
                                     [&](const auto& item) { return object_of(item).id == id; });
        if (it != list.end()) {
            out.push_back(rect_of(object_of(*it)));
        }
    }
    return out;
}

/// ``canvas::*_rects`` output back as geometry; ``include_flip`` for a flip.
[[nodiscard]] inline std::vector<ObjectGeometry> geometries_from_rects(
    const std::vector<canvas::Rect>& rects, bool include_flip)
{
    std::vector<ObjectGeometry> out;
    out.reserve(rects.size());
    for (const auto& r : rects) {
        ObjectGeometry g;
        g.id = r.id;
        g.x = r.x;
        g.y = r.y;
        g.w = r.w;
        g.h = r.h;
        g.rotation = r.rotation;
        g.flip_h = r.flip_h;
        g.flip_v = r.flip_v;
        g.has_flip = include_flip;
        out.push_back(std::move(g));
    }
    return out;
}

/// A flipped text box mirrors its alignment too, so the words follow the box.
inline void flip_text_alignment(Object& obj, canvas::FlipAxis axis)
{
    if (obj.type != ObjectType::Text && obj.type != ObjectType::Indicator
        && obj.type != ObjectType::Clock) {
        return;
    }
    if (axis == canvas::FlipAxis::Horizontal) {
        if (obj.text_align == "left") {
            obj.text_align = "right";
        } else if (obj.text_align == "right") {
            obj.text_align = "left";
        }
    } else if (obj.vertical_align == "top") {
        obj.vertical_align = "bottom";
    } else if (obj.vertical_align == "bottom") {
        obj.vertical_align = "top";
    }
}

/// Every id in ``list``, in stacking order.
template <class List>
[[nodiscard]] std::vector<std::string> list_ids(const List& list)
{
    std::vector<std::string> out;
    out.reserve(list.size());
    for (const auto& item : list) {
        out.push_back(object_of(item).id);
    }
    return out;
}

/// ``(id, group_id)`` pairs for ``canvas::expand_groups``.
template <class List>
[[nodiscard]] std::vector<std::pair<std::string, std::string>> list_group_members(const List& list)
{
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(list.size());
    for (const auto& item : list) {
        const auto& obj = object_of(item);
        out.emplace_back(obj.id, obj.group_id);
    }
    return out;
}

/// Apply a click / marquee to ``selection``: ``ids`` present in ``list`` are
/// expanded to their whole groups. Not additive: they become the selection.
/// Additive: toggled off when all are already selected, otherwise added.
template <class List>
void list_select(const List& list, std::vector<std::string>& selection,
                 const std::vector<std::string>& ids, bool additive)
{
    std::vector<std::string> valid;
    valid.reserve(ids.size());
    for (const auto& id : ids) {
        if (std::any_of(list.begin(), list.end(),
                        [&](const auto& item) { return object_of(item).id == id; })) {
            valid.push_back(id);
        }
    }
    auto expanded = canvas::expand_groups(list_group_members(list), valid);
    if (!additive) {
        selection = std::move(expanded);
        return;
    }
    if (expanded.empty()) {
        return;
    }
    const bool all_in = std::all_of(expanded.begin(), expanded.end(), [&](const std::string& id) {
        return detail::holds_id(selection, id);
    });
    if (all_in) {
        std::erase_if(selection,
                      [&](const std::string& id) { return detail::holds_id(expanded, id); });
        return;
    }
    for (auto& id : expanded) {
        if (!detail::holds_id(selection, id)) {
            selection.push_back(std::move(id));
        }
    }
}

}  // namespace seventhslide::canvas
