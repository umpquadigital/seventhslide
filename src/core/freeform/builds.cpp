#include "seventhslide/freeform/builds.hpp"

#include <algorithm>

namespace seventhslide::freeform {

std::vector<StepPlan> plan_steps(const std::vector<ObjectAnimations>& objects)
{
    struct Entry {
        const std::string* object_id;
        const canvas::Animation* animation;
        std::size_t seen;  // keeps equal orders in the objects' order
    };
    std::vector<Entry> entries;
    std::set<std::string> shown;
    for (const auto& object : objects) {
        // There from the start unless it comes in by an entrance.
        const auto first = std::min_element(
            object.animations.begin(), object.animations.end(),
            [](const auto& a, const auto& b) { return a.order < b.order; });
        if (first == object.animations.end()
            || canvas::animation_kind(first->effect) != canvas::AnimationKind::Entrance) {
            shown.insert(object.object_id);
        }
        for (const auto& animation : object.animations) {
            entries.push_back({&object.object_id, &animation, entries.size()});
        }
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.animation->order < b.animation->order;
    });

    std::vector<StepPlan> steps(1);
    steps.front().shown_before = shown;
    int previous_start = 0;
    int previous_end = 0;
    for (const auto& entry : entries) {
        const auto& animation = *entry.animation;
        if (animation.trigger == canvas::AnimationTrigger::OnClick) {
            // A step of its own: what was shown is where it starts.
            steps.back().shown_after = shown;
            StepPlan next;
            next.shown_before = shown;
            steps.push_back(std::move(next));
            previous_start = 0;
            previous_end = 0;
        }
        StepPlan& step = steps.back();
        int start = 0;
        switch (animation.trigger) {
        case canvas::AnimationTrigger::OnClick:
            start = animation.delay_ms;
            break;
        case canvas::AnimationTrigger::WithPrevious:
            start = previous_start + animation.delay_ms;
            break;
        case canvas::AnimationTrigger::AfterPrevious:
            start = previous_end + animation.delay_ms;
            break;
        }
        TimedAnimation play{*entry.object_id, animation, start};
        previous_start = start;
        previous_end = std::max(previous_end, play.end_ms());
        step.end_ms = std::max(step.end_ms, play.end_ms());
        step.plays.push_back(std::move(play));
        switch (canvas::animation_kind(animation.effect)) {
        case canvas::AnimationKind::Entrance:
            shown.insert(*entry.object_id);
            break;
        case canvas::AnimationKind::Exit:
            shown.erase(*entry.object_id);
            break;
        case canvas::AnimationKind::Emphasis:
            break;
        }
    }
    steps.back().shown_after = shown;
    // Each video started on a click: a click of its own, after the rest, or
    // while it is still there if an exit takes it off. One never there takes
    // none.
    for (const auto& object : objects) {
        if (!object.plays_on_click) {
            continue;
        }
        const auto last_shown = std::find_if(steps.rbegin(), steps.rend(), [&](const StepPlan& s) {
            return s.shown_after.contains(object.object_id);
        });
        if (last_shown == steps.rend()) {
            continue;
        }
        StepPlan click;
        click.shown_before = last_shown->shown_after;
        click.shown_after = last_shown->shown_after;
        click.playing_after = last_shown->playing_after;
        const auto at = last_shown.base();
        for (auto later = steps.insert(at, std::move(click)); later != steps.end(); ++later) {
            later->playing_after.insert(object.object_id);
        }
    }
    return steps;
}

}  // namespace seventhslide::freeform
