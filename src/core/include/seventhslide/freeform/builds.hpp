#pragma once

// A slide's builds as steps.
//
// Every animation on a slide has its place in the slide's order. Step 0 is
// what the slide comes in with: the animations before its first on-click
// one, which play once the slide is in. Each on-click animation then starts
// a step of its own, with the with-previous and after-previous animations
// that follow it. An object whose first animation is an entrance is not
// there until that plays; an exit takes it off; an emphasis leaves it as it
// was. A video that starts on a click (4.7's 6) takes a click of its own,
// after the slide's animations (before its exit, if it has one), and plays
// from it on. Seam: plain C++20.

#include "seventhslide/canvas/parts.hpp"

#include <set>
#include <string>
#include <vector>

namespace seventhslide::freeform {

/// An object's animations, by its id: what the planner reads.
struct ObjectAnimations {
    std::string object_id;
    std::vector<canvas::Animation> animations;
    /// A video that waits for a click to play.
    bool plays_on_click = false;
};

/// One animation as its step plays it: when it starts, from the step's start.
struct TimedAnimation {
    std::string object_id;
    canvas::Animation animation;
    int start_ms = 0;

    [[nodiscard]] int end_ms() const noexcept { return start_ms + animation.duration_ms; }
    [[nodiscard]] bool operator==(const TimedAnimation&) const noexcept = default;
};

struct StepPlan {
    /// The objects shown before the step plays, and once it has.
    std::set<std::string> shown_before;
    std::set<std::string> shown_after;
    /// The videos started on a click that play once the step has.
    std::set<std::string> playing_after;
    /// What the step plays, in order.
    std::vector<TimedAnimation> plays;
    /// When the last of it is done, from the step's start; 0 for nothing.
    int end_ms = 0;

    [[nodiscard]] bool operator==(const StepPlan&) const noexcept = default;
};

/// The steps of a slide whose objects are ``objects``: always at least one.
[[nodiscard]] std::vector<StepPlan> plan_steps(const std::vector<ObjectAnimations>& objects);

}  // namespace seventhslide::freeform
