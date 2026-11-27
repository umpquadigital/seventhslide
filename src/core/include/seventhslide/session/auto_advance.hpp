#pragma once

// Per-stack auto-advance config. Seam: plain C++20.
//
// Living here lets the web remote both read and drive it, and keeps one
// stack's timer settings alive across a focus change.

#include <string>
#include <string_view>

namespace seventhslide::session {

/// One stack's timer settings. A stack with no entry reports these defaults.
struct AutoAdvanceState {
    /// Timer is running. Cleared by Session when the stack runs out of content.
    bool enabled = false;
    /// Tick period. Mirrors the operator UI's whole-second control.
    int interval_ms = 5000;
    /// On exhaustion, wrap instead of stopping - within the current item, or
    /// within the rundown (or section, with ``stop_at_dividers``) when running
    /// ``through``.
    bool loop = false;
    /// Run on past the end of the current item into the following rundown rows
    /// that resolve to this stack, skipping items that belong to other stacks,
    /// instead of stopping at the item boundary.
    bool through = false;
    /// Treat divider rows as a stop while running ``through``. Also bounds a
    /// ``loop`` rewind to the current section rather than the whole rundown.
    bool stop_at_dividers = false;

    [[nodiscard]] bool operator==(const AutoAdvanceState&) const noexcept = default;
};

/// Outcome of one ``Session::tick_auto_advance`` step.
struct AdvanceTick {
    /// Live content moved - a line, an image, or a new rundown row.
    bool did_advance = false;
    /// Nothing left to advance to; the stack has been switched off.
    bool exhausted = false;
    /// The step selected a different rundown row, not just a line within the
    /// current one. Callers rebuild list models on this.
    bool changed_item = false;

    [[nodiscard]] bool operator==(const AdvanceTick&) const noexcept = default;
};

} // namespace seventhslide::session
