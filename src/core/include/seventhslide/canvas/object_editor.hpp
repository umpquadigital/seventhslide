#pragma once

// The editing core both canvas editors share: the text-theme designer
// (theme::Editor, a list of theme::Element per design context) and the
// Freeform editor (freeform::Editor, a list of its Object per slide).
//
// It owns the document, the undo history, the selection and the object
// clipboard, and implements every command that is the same in both:
// selecting, gestures, undo / redo, delete, duplicate, copy / paste,
// stacking, geometry, align / distribute / nudge / flip, group / lock, and
// setting a property by the design ribbon's name. What an editor adds on top
// (bindings and contexts; slides and cues) stays in the editor.
//
// An editor derives from ObjectEditor<Editor, Doc, Item, Part> and supplies,
// as private members the base reaches as a friend:
//
//   std::vector<Item>* stack();              the list being edited, or null
//   const std::vector<Item>* stack() const;
//   std::optional<Place> here() const;       where the stack is (its slide,
//                                            its context), or none
//   geom::CanvasSize canvas() const;         what align / nudge fit within
//   EditResult writable() const;             why the stack may not change now
//   void after_edit();                       after a committed edit
//   void after_restore();                    after undo / redo
//   void after_gesture();                    after end_gesture
//   std::string mint_id(char kind);          a new object ('o') or group ('g') id
//   void prepare_copy(Item&);                a duplicated / pasted item, before it lands
//   EditResult apply_property(Item&, name, value);
//   static constexpr GeometryUnits k_units;  how geometry is stored
//   static constexpr int k_z_step;           the gap between stacked z values
//
// ``Part`` is a ``DocPart`` (below): the place an edit of the stack may
// change, which the history keeps in place of the whole document. ``Doc``
// keeps the counter ``mint_id`` draws from in ``next_id``.
//
// Lists are kept back-to-front by ``z`` (object_list.hpp).
//
// Seam: plain C++20 + nlohmann/json (no Qt).

#include "seventhslide/canvas/edit_history.hpp"
#include "seventhslide/canvas/edit_result.hpp"
#include "seventhslide/canvas/layout.hpp"
#include "seventhslide/canvas/object.hpp"
#include "seventhslide/canvas/object_list.hpp"
#include "seventhslide/canvas/property_value.hpp"
#include "seventhslide/geom/canvas.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::canvas {

/// One place in a document (a slide, a context's list) and the id counter
/// beside it: what an edit of the stack being edited may change. ``At``
/// finds the place in a document, or gives null where it has none.
template <class Doc, class Place, class Piece, Piece* (*At)(Doc&, const Place&)>
struct DocPart {
    Place place{};
    Piece piece{};
    int next_id = 0;

    /// A copy of what ``doc`` holds at ``place``, which it must have.
    [[nodiscard]] static DocPart of(Doc& doc, const Place& place)
    {
        return DocPart{place, *At(doc, place), doc.next_id};
    }

    /// Trade what this holds with what ``doc`` holds at its place.
    [[nodiscard]] bool swap_with(Doc& doc) noexcept
    {
        auto* there = At(doc, place);
        if (there == nullptr) {
            return false;
        }
        std::swap(*there, piece);
        std::swap(doc.next_id, next_id);
        return true;
    }

    [[nodiscard]] bool same_place(const DocPart& other) const noexcept
    {
        return place == other.place;
    }

    [[nodiscard]] DocPart hollow() const { return DocPart{place}; }

    [[nodiscard]] bool operator==(const DocPart&) const noexcept = default;
};

template <class Derived, class Doc, class Item, class Part>
class ObjectEditor {
public:
    using List = std::vector<Item>;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool dirty() const noexcept { return history_.dirty(); }
    [[nodiscard]] bool can_undo() const noexcept { return history_.can_undo(); }
    [[nodiscard]] bool can_redo() const noexcept { return history_.can_redo(); }

    // --- selection ---------------------------------------------------------

    [[nodiscard]] const std::vector<std::string>& selected_ids() const noexcept
    {
        return selected_ids_;
    }

    /// The first selected id; empty when nothing is selected.
    [[nodiscard]] const std::string& selected_id() const noexcept
    {
        static const std::string none;
        return selected_ids_.empty() ? none : selected_ids_.front();
    }

    [[nodiscard]] const Item* find(std::string_view id) const
    {
        const auto* list = self().stack();
        if (list == nullptr || id.empty()) {
            return nullptr;
        }
        const auto it = std::find_if(list->begin(), list->end(), [&](const Item& item) {
            return object_of(item).id == id;
        });
        return it != list->end() ? &*it : nullptr;
    }

    [[nodiscard]] Item* find(std::string_view id)
    {
        return const_cast<Item*>(std::as_const(*this).find(id));
    }

    /// The first selected item, which the property commands edit.
    [[nodiscard]] const Item* selected_item() const { return find(selected_id()); }
    [[nodiscard]] Item* selected_item() { return find(selected_id()); }

    /// Click / marquee: ``id`` expanded to its group. Additive toggles.
    void select(std::string_view id, bool additive = false)
    {
        if (id.empty()) {
            if (!additive) {
                selected_ids_.clear();
            }
            return;
        }
        select_ids({std::string(id)}, additive);
    }

    void select_ids(const std::vector<std::string>& ids, bool additive = false)
    {
        const auto* list = self().stack();
        if (list == nullptr) {
            if (!additive) {
                selected_ids_.clear();
            }
            return;
        }
        list_select(*list, selected_ids_, ids, additive);
    }

    void select_all()
    {
        const auto* list = self().stack();
        selected_ids_ = list != nullptr ? list_ids(*list) : std::vector<std::string>{};
    }

    void clear_selection() { selected_ids_.clear(); }

    // --- history -----------------------------------------------------------

    /// Everything until ``end_gesture`` is one undo step (a drag, a resize).
    void begin_gesture() { history_.begin_gesture(); }

    void end_gesture()
    {
        history_.end_gesture();
        self().after_gesture();
    }

    [[nodiscard]] EditResult undo()
    {
        if (!history_.undo(doc_)) {
            return EditResult::fail("nothing to undo");
        }
        restored();
        return EditResult::success();
    }

    [[nodiscard]] EditResult redo()
    {
        if (!history_.redo(doc_)) {
            return EditResult::fail("nothing to redo");
        }
        restored();
        return EditResult::success();
    }

    // --- the selection as a whole -------------------------------------------

    /// Delete the selection. Nothing is selected afterwards.
    [[nodiscard]] EditResult delete_selected()
    {
        if (selected_ids_.empty()) {
            return EditResult::fail("nothing selected");
        }
        const auto ids = selected_ids_;
        const auto r = commit_stack([&](List& list) {
            if (list_selection_locked(list, ids)) {
                return EditResult::fail("selection is locked");
            }
            return list_erase(list, ids);
        });
        if (r) {
            selected_ids_.clear();
        }
        return r;
    }

    /// Copies of the selection, offset down and right, on top of the stack.
    /// The copies become the selection.
    [[nodiscard]] EditResult duplicate_selected()
    {
        constexpr double k_offset = 24;
        return land_copies(selected_items(), k_offset);
    }

    /// Put the selection on the clipboard.
    [[nodiscard]] EditResult copy_selected()
    {
        auto items = selected_items();
        if (items.empty()) {
            return EditResult::fail("nothing selected");
        }
        clipboard_ = std::move(items);
        return EditResult::success();
    }

    /// Copies of the clipboard on top of the stack, where they were. The
    /// copies become the selection.
    [[nodiscard]] EditResult paste()
    {
        if (clipboard_.empty()) {
            return EditResult::fail("nothing to paste");
        }
        return land_copies(clipboard_, 0);
    }

    [[nodiscard]] bool can_paste() const noexcept { return !clipboard_.empty(); }

    [[nodiscard]] bool can_reorder_selected(ZOrderOp op) const
    {
        const auto* list = self().stack();
        return list != nullptr && !selected_ids_.empty() && self().writable()
            && list_can_reorder(*list, selected_ids_, op);
    }

    /// Move the selection through the stack, as a block. A selection already
    /// where it would go (the whole stack, say) stays put: success, and no
    /// undo step.
    [[nodiscard]] EditResult reorder_selected(ZOrderOp op)
    {
        if (selected_ids_.empty()) {
            return EditResult::fail("nothing selected");
        }
        const auto* current = self().stack();
        if (current == nullptr) {
            return EditResult::fail("no slide selected");
        }
        if (const auto ok = self().writable(); !ok) {
            return ok;
        }
        if (!list_can_reorder(*current, selected_ids_, op)) {
            return EditResult::success();
        }
        const auto ids = selected_ids_;
        return commit_stack([&](List& list) {
            sort_by_z(list);
            (void)list_reorder(list, ids, op);
            renumber_z(list);
            return EditResult::success();
        });
    }

    // --- geometry --------------------------------------------------------------

    [[nodiscard]] EditResult set_geometry(std::string_view id, double x, double y, double w,
                                          double h, double rotation)
    {
        ObjectGeometry g;
        g.id = std::string(id);
        g.x = x;
        g.y = y;
        g.w = w;
        g.h = h;
        g.rotation = rotation;
        return set_geometries({std::move(g)});
    }

    /// Many objects in one step. A locked target refuses the whole call.
    [[nodiscard]] EditResult set_geometries(const std::vector<ObjectGeometry>& geometries)
    {
        return commit_stack([&](List& list) {
            return list_set_geometries(list, geometries, Derived::k_units);
        });
    }

    [[nodiscard]] EditResult align_selected(canvas::Align align)
    {
        if (selected_ids_.empty()) {
            return EditResult::fail("nothing selected");
        }
        const auto size = self().canvas();
        const canvas::Rect frame{"", 0, 0, static_cast<double>(size.width),
                                 static_cast<double>(size.height)};
        return set_geometries(
            geometries_from_rects(canvas::align_rects(selected_rects(), align, frame), false));
    }

    [[nodiscard]] EditResult distribute_selected(canvas::Distribute dist)
    {
        if (selected_ids_.size() < 3) {
            return EditResult::fail("select three or more objects to distribute");
        }
        return set_geometries(
            geometries_from_rects(canvas::distribute_rects(selected_rects(), dist), false));
    }

    [[nodiscard]] EditResult nudge_selected(double dx, double dy)
    {
        if (selected_ids_.empty()) {
            return EditResult::fail("nothing selected");
        }
        return set_geometries(geometries_from_rects(
            canvas::nudge_rects(selected_rects(), dx, dy, self().canvas()), false));
    }

    /// Mirror the selection; a text box's alignment mirrors with it, so the
    /// words follow the box. One undo step.
    [[nodiscard]] EditResult flip_selected(canvas::FlipAxis axis)
    {
        if (selected_ids_.empty()) {
            return EditResult::fail("nothing selected");
        }
        const auto geometries =
            geometries_from_rects(canvas::flip_rects(selected_rects(), axis), true);
        const auto ids = selected_ids_;
        return commit_stack([&](List& list) {
            const auto r = list_set_geometries(list, geometries, Derived::k_units);
            if (r) {
                for (auto& item : list) {
                    if (detail::holds_id(ids, object_of(item).id)) {
                        flip_text_alignment(object_of(item), axis);
                    }
                }
            }
            return r;
        });
    }

    // --- grouping and locking ---------------------------------------------------

    [[nodiscard]] EditResult group_selected()
    {
        const auto ids = selected_ids_;
        // The id is minted inside the step, so a refused group gives it back.
        return commit_stack(
            [&](List& list) { return list_group(list, ids, self().mint_id('g')); });
    }

    [[nodiscard]] EditResult ungroup_selected()
    {
        const auto ids = selected_ids_;
        return commit_stack([&](List& list) { return list_ungroup(list, ids); });
    }

    [[nodiscard]] EditResult set_selected_locked(bool locked)
    {
        const auto ids = selected_ids_;
        return commit_stack([&](List& list) { return list_set_locked(list, ids, locked); });
    }

    [[nodiscard]] bool selection_locked() const
    {
        const auto* list = self().stack();
        return list != nullptr && list_selection_locked(*list, selected_ids_);
    }

    [[nodiscard]] bool can_group_selected() const
    {
        return selected_ids_.size() >= 2 && self().writable();
    }

    [[nodiscard]] bool can_ungroup_selected() const
    {
        const auto* list = self().stack();
        return list != nullptr && self().writable()
            && list_selection_grouped(*list, selected_ids_);
    }

    // --- properties -------------------------------------------------------------

    /// Property of the selected item by the design ribbon's name (``x``,
    /// ``font_size``, ``fill``, ...): geometry through ``set_geometry`` in the
    /// editor's units, everything else through the editor's property table.
    /// A refused value, or one the item already has, leaves no undo step.
    [[nodiscard]] EditResult set_property(std::string_view name, const nlohmann::json& value)
    {
        const Item* item = selected_item();
        if (item == nullptr) {
            return EditResult::fail("nothing selected");
        }
        if (!is_geometry_property(name)) {
            return mutate_selected(
                [&](Item& target) { return self().apply_property(target, name, value); });
        }
        const auto n = property_value::number(value);
        if (!n) {
            return EditResult::fail("expected a number");
        }
        const Object& o = object_of(*item);
        const auto g = with_geometry_property(o, name, *n);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): is_geometry_property(name) held above.
        return set_geometry(o.id, g->x, g->y, g->w, g->h, g->rotation);
    }

protected:
    ObjectEditor() = default;

    /// Begin editing ``doc``: a fresh history, nothing selected.
    void start(Doc doc)
    {
        doc_ = std::move(doc);
        history_.reset(doc_);
        selected_ids_.clear();
        active_ = true;
    }

    /// Close the document: everything back to empty, clipboard included.
    void stop()
    {
        doc_ = {};
        history_.clear();
        selected_ids_.clear();
        clipboard_.clear();
        active_ = false;
    }

    /**
     * One undoable edit of the document: snapshot, ``fn(doc)``, and keep the
     * step only when it succeeds. A refused edit, or one that throws, puts
     * the document back as it was, so it may write before giving up; inside a
     * gesture too. Refused while the editor is inactive or ``writable()``
     * says no.
     */
    template <class Fn>
    [[nodiscard]] EditResult commit(Fn&& fn)
    {
        if (const auto ok = self().writable(); !ok) {
            return ok;
        }
        return commit_document(std::forward<Fn>(fn));
    }

    /// ``commit`` without the ``writable()`` check, for edits a read-only
    /// stack still allows (the Freeform document's name).
    template <class Fn>
    [[nodiscard]] EditResult commit_document(Fn&& fn)
    {
        return commit_whole(std::forward<Fn>(fn), [] { return true; });
    }

    /// ``commit`` of an edit that changes the stack's place alone (its
    /// slide, its context) and the id counter, and elsewhere only reads.
    /// The history keeps that place, not the whole document, and a failed
    /// edit inside a gesture puts back only it. With no place, ``commit``.
    template <class Fn>
    [[nodiscard]] EditResult commit_here(Fn&& fn)
    {
        return commit_part(std::forward<Fn>(fn), [] { return true; });
    }

    /// ``commit_here`` on the stack being edited.
    template <class Fn>
    [[nodiscard]] EditResult commit_stack(Fn&& fn)
    {
        return commit_here([&](Doc&) {
            auto* list = self().stack();
            if (list == nullptr) {
                return EditResult::fail("no slide selected");
            }
            return fn(*list);
        });
    }

    /// ``commit_here`` on the selected item. Refused, or no change: no undo
    /// step.
    template <class Fn>
    [[nodiscard]] EditResult mutate_selected(Fn&& fn)
    {
        Item* item = selected_item();
        if (item == nullptr) {
            return EditResult::fail("nothing selected");
        }
        const Item before = *item;
        return commit_part([&](Doc&) { return fn(*item); },
                           [&] { return !(*item == before); });
    }

    /// A new item from ``make(doc)``, on top of the stack and selected.
    /// ``make`` runs inside the step, so ids it mints roll back with it.
    template <class Make>
    [[nodiscard]] EditResult add_item(Make&& make)
    {
        std::string new_id;
        const auto r = commit_here([&](Doc& doc) {
            Item item = make(doc);
            auto* list = self().stack();
            if (list == nullptr) {
                return EditResult::fail("no slide selected");
            }
            Object& obj = object_of(item);
            if (obj.id.empty()) {
                obj.id = self().mint_id('o');
            }
            obj.z = next_z(*list);
            new_id = obj.id;
            list->push_back(std::move(item));
            return EditResult::success();
        });
        if (r) {
            selected_ids_ = {new_id};
        }
        return r;
    }

    /// Empty the object clipboard (an editor filling a clipboard of its own).
    void clear_clipboard() noexcept { clipboard_.clear(); }

    /// Bring the dirty flag up to date after editing the document in place.
    void refresh_dirty() { history_.refresh_dirty(doc_); }

    /// ``next_z`` for a stack: one step above its top.
    [[nodiscard]] static int next_z(const List& list)
    {
        int top = -Derived::k_z_step;
        for (const auto& item : list) {
            top = std::max(top, object_of(item).z);
        }
        return top + Derived::k_z_step;
    }

    Doc doc_{};
    canvas::EditHistory<Doc, Part> history_{};
    std::vector<std::string> selected_ids_;
    bool active_ = false;

private:
    [[nodiscard]] Derived& self() noexcept { return static_cast<Derived&>(*this); }
    [[nodiscard]] const Derived& self() const noexcept
    {
        return static_cast<const Derived&>(*this);
    }

    /// ``edit()``; if it throws, ``put_back()`` first.
    template <class Edit, class PutBack>
    [[nodiscard]] static EditResult run_or_put_back(Edit&& edit, PutBack&& put_back)
    {
        try {
            return edit();
        } catch (...) {
            put_back();
            throw;
        }
    }

    /// ``commit_document``, keeping the step only while ``kept()`` says so.
    template <class Fn, class Kept>
    [[nodiscard]] EditResult commit_whole(Fn&& fn, Kept&& kept)
    {
        if (!active_) {
            return EditResult::fail("editor inactive");
        }
        const bool pushed = history_.push(doc_);
        // Inside a gesture the history's snapshot is from before the gesture:
        // this edit keeps its own.
        std::optional<Doc> before;
        if (!pushed) {
            before = doc_;
        }
        const auto put_back = [&] {
            if (pushed) {
                history_.rollback(pushed, doc_);
            } else {
                doc_ = std::move(*before);
            }
        };
        const EditResult result = run_or_put_back([&] { return fn(doc_); }, put_back);
        if (!result || !kept()) {
            put_back();
            return result;
        }
        history_.keep();
        self().after_edit();
        history_.refresh_dirty(doc_);
        return result;
    }

    /// ``commit_here``, keeping the step only while ``kept()`` says so.
    template <class Fn, class Kept>
    [[nodiscard]] EditResult commit_part(Fn&& fn, Kept&& kept)
    {
        if (const auto ok = self().writable(); !ok) {
            return ok;
        }
        if (!active_) {
            return EditResult::fail("editor inactive");
        }
        const auto place = self().here();
        if (!place) {
            return commit_whole(std::forward<Fn>(fn), std::forward<Kept>(kept));
        }
        Part part = Part::of(doc_, *place);
        const Part spot = part.hollow();
        const bool pushed = history_.push(part, doc_);
        const auto put_back = [&] {
            if (pushed) {
                history_.rollback(pushed, doc_);
            } else {
                (void)part.swap_with(doc_);
            }
        };
        const EditResult result = run_or_put_back([&] { return fn(doc_); }, put_back);
        if (!result || !kept()) {
            put_back();
            return result;
        }
        history_.keep();
        self().after_edit();
        history_.refresh_dirty(doc_, spot);
        return result;
    }

    /// After undo / redo: the editor settles its view, then the selection
    /// keeps whatever still exists.
    void restored()
    {
        self().after_restore();
        const auto* list = self().stack();
        if (list == nullptr) {
            selected_ids_.clear();
            return;
        }
        std::erase_if(selected_ids_, [&](const std::string& id) { return find(id) == nullptr; });
    }

    /// Copies of the selected items, in selection order.
    [[nodiscard]] List selected_items() const
    {
        List out;
        for (const auto& id : selected_ids_) {
            if (const Item* item = find(id)) {
                out.push_back(*item);
            }
        }
        return out;
    }

    [[nodiscard]] std::vector<canvas::Rect> selected_rects() const
    {
        const auto* list = self().stack();
        return list != nullptr ? list_rects(*list, selected_ids_) : std::vector<canvas::Rect>{};
    }

    /// ``items`` as new items on top of the stack, moved by ``offset``: fresh
    /// ids, groups kept together under fresh group ids. They become the
    /// selection.
    [[nodiscard]] EditResult land_copies(const List& items, double offset)
    {
        if (items.empty()) {
            return EditResult::fail("nothing selected");
        }
        std::vector<std::string> new_ids;
        const auto r = commit_stack([&](List& list) {
            std::map<std::string, std::string> group_remap;
            for (Item copy : items) {
                self().prepare_copy(copy);
                Object& obj = object_of(copy);
                obj.id = self().mint_id('o');
                if (!obj.group_id.empty()) {
                    auto it = group_remap.find(obj.group_id);
                    if (it == group_remap.end()) {
                        it = group_remap.emplace(obj.group_id, self().mint_id('g')).first;
                    }
                    obj.group_id = it->second;
                }
                obj.x += offset;
                obj.y += offset;
                obj.z = next_z(list);
                new_ids.push_back(obj.id);
                list.push_back(std::move(copy));
            }
            return EditResult::success();
        });
        if (r) {
            selected_ids_ = std::move(new_ids);
        }
        return r;
    }

    static void sort_by_z(List& list)
    {
        std::stable_sort(list.begin(), list.end(), [](const Item& a, const Item& b) {
            return object_of(a).z < object_of(b).z;
        });
    }

    static void renumber_z(List& list)
    {
        int z = 0;
        for (auto& item : list) {
            object_of(item).z = z;
            z += Derived::k_z_step;
        }
    }

    List clipboard_;
};

}  // namespace seventhslide::canvas
