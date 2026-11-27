#pragma once

// Undo / redo for a canvas editor whose document is a value type.
// Seam: plain C++20 (no Qt, no JSON).

#include <cstddef>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace seventhslide::canvas {

/**
 * Two stacks, a baseline, and the flags that make one gesture one undo step.
 *
 * canvas::ObjectEditor holds one for both canvas editors. It knows nothing
 * of selections or views: what an editor does after a restore is the editor's.
 *
 * ``Doc`` must be copyable and equality-comparable; ``dirty()`` is defined as
 * "differs from the document this history was reset to".
 *
 * A step is the whole document as it was or, for an edit that changed one
 * place in it alone (one slide, say), a ``Part``: that place as it was. So
 * the edits made most often cost what they changed, not the document. A
 * ``Part`` is equality-comparable and has:
 *
 *   bool swap_with(Doc&);           trade what it holds with what the
 *                                   document holds at its place; false, and
 *                                   nothing traded, where it has no such place
 *   bool same_place(const Part&) const;
 *   Part hollow() const;            the same place, holding nothing
 */
template <typename Doc, typename Part>
class EditHistory {
public:
    /// Deepest undo stack kept. Older entries fall off the bottom.
    static constexpr std::size_t k_max_depth = 50;

    /// Begin editing ``doc``: it becomes the baseline ``dirty()`` compares to,
    /// and both stacks are emptied.
    void reset(const Doc& doc)
    {
        baseline_ = doc;
        undo_.clear();
        redo_.clear();
        dirty_ = false;
        rest_.reset();
        grouping_ = false;
        group_pushed_ = false;
    }

    /// Forget everything, baseline included.
    void clear() { reset(Doc{}); }

    [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }

    /// Recompute ``dirty()``. Call after mutating the document in place.
    void refresh_dirty(const Doc& doc)
    {
        rest_.reset();
        dirty_ = !(doc == baseline_);
    }

    /// Recompute ``dirty()`` after a change at ``where``'s place alone. The
    /// rest of the document is compared once for each place, not each time.
    void refresh_dirty(Doc& doc, const Part& where)
    {
        Part theirs = where.hollow();
        if (!theirs.swap_with(baseline_)) {
            // The baseline has no such place, so the two differ.
            rest_.reset();
            dirty_ = true;
            return;
        }
        Part mine = where.hollow();
        (void)mine.swap_with(doc);
        if (!rest_ || !rest_->place.same_place(where)) {
            rest_ = Rest{where.hollow(), doc == baseline_};
        }
        dirty_ = !rest_->same || !(mine == theirs);
        (void)mine.swap_with(doc);
        (void)theirs.swap_with(baseline_);
    }

    /**
     * Snapshot ``doc`` before mutating it: a step of its own, which ``keep``
     * keeps once the edit is kept and ``rollback`` takes back if it is not.
     *
     * Inside a gesture only the first call adds a step, so a drag is one undo
     * step. A later call instead makes that step the whole document, where
     * it held one place alone. Returns whether this call added a step, which
     * is what ``rollback`` needs: a failed edit must not take the gesture's
     * step, which holds the edits already made inside it.
     */
    [[nodiscard]] bool push(const Doc& doc)
    {
        if (grouping_ && group_pushed_) {
            widen(doc);
            return false;
        }
        add(Step{std::in_place_type<Doc>, doc});
        return true;
    }

    /// ``push`` for an edit of ``part``'s place alone: ``part`` is that place
    /// as it is now. Takes ``part`` when it pushes; otherwise the caller keeps
    /// it, to put back an edit that fails. A gesture whose step holds another
    /// place has its step made the whole document first.
    [[nodiscard]] bool push(Part& part, const Doc& doc)
    {
        if (grouping_ && group_pushed_) {
            const auto* held = undo_.empty() ? nullptr : std::get_if<Part>(&undo_.back());
            if (held != nullptr && !held->same_place(part)) {
                widen(doc);
            }
            return false;
        }
        add(Step{std::in_place_type<Part>, std::move(part)});
        return true;
    }

    /// The edit after a ``push`` is kept: the redo stack goes, and the oldest
    /// step past ``k_max_depth``. Until then both stay, for a ``rollback``.
    void keep()
    {
        if (undo_.size() > k_max_depth) {
            undo_.erase(undo_.begin());
        }
        redo_.clear();
    }

    /// Take back the step a failed edit pushed, putting what it holds back into
    /// ``doc``: the edit may have written before giving up. ``pushed`` must be
    /// what that ``push`` returned.
    void rollback(bool pushed, Doc& doc)
    {
        if (!pushed || undo_.empty()) {
            return;
        }
        trade(undo_.back(), doc);
        undo_.pop_back();
        if (grouping_) {
            group_pushed_ = false;
        }
    }

    /// Restore the previous state into ``doc``. False when there was nothing to
    /// restore, and then ``doc`` is untouched. Inside a gesture, the next
    /// edit makes a step of its own.
    [[nodiscard]] bool undo(Doc& doc) { return move_step(undo_, redo_, doc); }

    [[nodiscard]] bool redo(Doc& doc) { return move_step(redo_, undo_, doc); }

    /// Coalesce every ``push`` until ``end_gesture`` into one undo step.
    /// Re-entrant calls are ignored, so a nested begin cannot split a drag.
    void begin_gesture() noexcept
    {
        if (grouping_) {
            return;
        }
        grouping_ = true;
        group_pushed_ = false;
    }

    void end_gesture() noexcept
    {
        grouping_ = false;
        group_pushed_ = false;
    }

    [[nodiscard]] bool in_gesture() const noexcept { return grouping_; }

private:
    using Step = std::variant<Doc, Part>;

    /// Whether the document is the baseline's apart from ``place``.
    struct Rest {
        Part place;
        bool same = false;
    };

    void add(Step step)
    {
        undo_.push_back(std::move(step));
        if (grouping_) {
            group_pushed_ = true;
        }
    }

    /// The gesture's step as the whole document, for an edit beyond its place.
    /// Only that place has changed since the gesture began.
    void widen(const Doc& doc)
    {
        auto* held = undo_.empty() ? nullptr : std::get_if<Part>(&undo_.back());
        if (held == nullptr) {
            return;
        }
        Doc whole = doc;
        (void)held->swap_with(whole);
        undo_.back() = std::move(whole);
    }

    /// Trade what ``step`` holds with ``doc``.
    static void trade(Step& step, Doc& doc)
    {
        if (auto* part = std::get_if<Part>(&step)) {
            (void)part->swap_with(doc);
        } else {
            std::swap(std::get<Doc>(step), doc);
        }
    }

    bool move_step(std::vector<Step>& from, std::vector<Step>& to, Doc& doc)
    {
        if (from.empty()) {
            return false;
        }
        Step step = std::move(from.back());
        from.pop_back();
        trade(step, doc);
        if (const auto* part = std::get_if<Part>(&step)) {
            refresh_dirty(doc, *part);
        } else {
            refresh_dirty(doc);
        }
        to.push_back(std::move(step));
        group_pushed_ = false;
        return true;
    }

    Doc baseline_{};
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    bool dirty_ = false;
    std::optional<Rest> rest_;
    bool grouping_ = false;
    bool group_pushed_ = false;
};

}  // namespace seventhslide::canvas
