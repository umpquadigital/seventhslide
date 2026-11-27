#pragma once

// PlayerController - one stack's shared line cursor: next / prev / jump.
// Seam: plain C++20 (no Qt). What each output shows on top of that (blank,
// freeze) is Session's, not the player's.

#include "seventhslide/build/slide_builder.hpp"
#include "seventhslide/model/transition.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::session {

/// Where one output is in the current build.
struct OutputNavState {
    /// ``OutputConfig::id``.
    std::string output_id;
    std::vector<int> line_to_slide;
    int index = 0;
    /// Each slide's build steps; a slide not listed has one.
    std::vector<int> slide_steps;
    /// The build step on its slide: 0 is what the slide comes in with.
    int step = 0;
    /// The last move plays this output's builds on the way in (a step
    /// forward, or coming to a slide); otherwise it shows them as built.
    bool animate = true;
    /// The last move stayed on this output's slide: only its builds change.
    bool within = false;
    /// The last move was a take with a transition of its own (``arrive``):
    /// the frames come in by it rather than as the theme or slide says.
    /// The next move replaces it, as every move records how it came.
    std::optional<model::Transition> transition;
};

/**
 * Shared logical-line cursor across all outputs.
 *
 * ``next_slide`` / ``prev_slide`` skip lines where no output's slide index
 * changes. The cursor moves whether or not an output is frozen: freeze holds
 * the output's picture, not the cursor.
 */
class PlayerController {
public:
    /// Replace line lists + maps from a rebuild. Resets cursor to 0 and syncs indices.
    void apply_build_result(const build::SlideBuildResult& built);

    /// Install maps directly (tests / Layer D scripts). Resets cursor to 0.
    void reset_maps(std::vector<OutputNavState> outputs,
                    int total_lines,
                    std::vector<std::string> all_lines = {},
                    std::vector<std::string> all_line_labels = {});

    [[nodiscard]] int line_cursor() const noexcept { return line_cursor_; }
    [[nodiscard]] int total_lines() const noexcept { return total_lines_; }
    [[nodiscard]] const std::vector<std::string>& all_lines() const noexcept
    {
        return all_lines_;
    }
    [[nodiscard]] const std::vector<std::string>& all_line_labels() const noexcept
    {
        return all_line_labels_;
    }
    [[nodiscard]] const std::vector<OutputNavState>& outputs() const noexcept
    {
        return outputs_;
    }
    /// The output with ``OutputConfig::id`` ``output_id``, or null (always for
    /// an empty id).
    [[nodiscard]] const OutputNavState* find_output(std::string_view output_id) const noexcept;

    /// Slide index for ``line``, or 0 if out of range / empty map.
    [[nodiscard]] static int slide_for_line(const OutputNavState& oc, int line) noexcept;

    /// Next line where any output's slide differs from ``start`` (dir +/-1), or nullopt.
    [[nodiscard]] std::optional<int> find_next_change_line(int start, int direction) const;

    /// Advance to next change line. False at end / empty.
    [[nodiscard]] bool next_slide();
    /// Move to previous change line (or 0). False at start / empty.
    [[nodiscard]] bool prev_slide();
    /// Jump to an absolute logical line. False if out of range.
    [[nodiscard]] bool jump_to_line(int line_index);
    /// Arrive on the outputs where it stands, as a take brings it: each
    /// output's slide comes in at the step it was left on, its builds playing
    /// when that is the first, by ``how`` when given.
    void arrive(std::optional<model::Transition> how);
    /// Leave the slide of output ``output_id`` (every output's, for an empty
    /// id) where it stands, as built and by its own transition, as a move
    /// that does not change it would: what a blank coming or going does, so
    /// the blank goes in by the slide's transition and nothing plays again
    /// when it lifts.
    void settle(std::string_view output_id = {});

    // --- build steps ---
    // Each output keeps the step on its own slide: a move that leaves an
    // output's slide where it was leaves its step too, as built.
    /// The step on the current line: that of the output whose slide has the
    /// most steps. Next walks the slides' steps before the line; previous
    /// walks them back, and lands on the slide before fully built.
    [[nodiscard]] int step() const noexcept;
    /// The most steps any output's slide has on ``line``; at least one.
    [[nodiscard]] int steps_at(int line) const noexcept;
    /// ``line`` at ``step`` (each output's slide at most at its last), shown
    /// as built (the controller's step marks).
    [[nodiscard]] bool jump_to_step(int line, int step);
    /// Each output's step, by id: what ``restore`` takes back.
    [[nodiscard]] std::vector<std::pair<std::string, int>> steps() const;
    /// Back to ``line`` and each output's ``steps`` after a rebuild, as built.
    void restore(int line, const std::vector<std::pair<std::string, int>>& steps);

private:
    /// Where an output arrives on its slide when the line moves onto it.
    enum class Arrive {
        /// At step 0, its builds playing.
        Start,
        /// At its last step, as built.
        Built,
    };

    void set_line_cursor(int line_index);
    /// To ``line``: each output whose slide changes arrives as ``arrive``
    /// says; one whose slide stays keeps its step.
    void move_to(int line, Arrive arrive);
    /// ``oc``'s slide on ``line``: its map's, or where it is when its map
    /// does not reach the line (as ``set_line_cursor`` leaves it).
    [[nodiscard]] static int slide_at(const OutputNavState& oc, int line) noexcept;
    [[nodiscard]] static int steps_of(const OutputNavState& oc, int slide) noexcept;

    int line_cursor_ = 0;
    int total_lines_ = 0;
    std::vector<std::string> all_lines_;
    std::vector<std::string> all_line_labels_;
    std::vector<OutputNavState> outputs_;
};

} // namespace seventhslide::session
