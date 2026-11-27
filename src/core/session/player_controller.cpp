#include "seventhslide/session/player_controller.hpp"

#include <algorithm>
#include <utility>

namespace seventhslide::session {

int PlayerController::slide_for_line(const OutputNavState& oc, int line) noexcept
{
    if (oc.line_to_slide.empty() || line < 0
        || line >= static_cast<int>(oc.line_to_slide.size())) {
        return 0;
    }
    return oc.line_to_slide[static_cast<std::size_t>(line)];
}

const OutputNavState* PlayerController::find_output(std::string_view output_id) const noexcept
{
    if (output_id.empty()) {
        return nullptr;
    }
    const auto it = std::find_if(outputs_.begin(), outputs_.end(),
                                 [output_id](const OutputNavState& oc) {
                                     return oc.output_id == output_id;
                                 });
    return it != outputs_.end() ? &*it : nullptr;
}

void PlayerController::set_line_cursor(int line_index)
{
    line_cursor_ = line_index;
    for (auto& oc : outputs_) {
        if (!oc.line_to_slide.empty() && line_index >= 0
            && line_index < static_cast<int>(oc.line_to_slide.size())) {
            oc.index = oc.line_to_slide[static_cast<std::size_t>(line_index)];
        }
    }
}

void PlayerController::reset_maps(std::vector<OutputNavState> outputs,
                                  int total_lines,
                                  std::vector<std::string> all_lines,
                                  std::vector<std::string> all_line_labels)
{
    outputs_ = std::move(outputs);
    total_lines_ = std::max(0, total_lines);
    all_lines_ = std::move(all_lines);
    all_line_labels_ = std::move(all_line_labels);
    set_line_cursor(0);
    for (auto& oc : outputs_) {
        oc.step = 0;
        oc.animate = true;
        oc.within = false;
        oc.transition.reset();
    }
}

void PlayerController::apply_build_result(const build::SlideBuildResult& built)
{
    std::vector<OutputNavState> next;
    next.reserve(built.outputs.size());
    for (const auto& rt : built.outputs) {
        std::vector<int> steps;
        steps.reserve(rt.compiled_slides.size());
        for (const auto& slide : rt.compiled_slides) {
            steps.push_back(std::max(1, static_cast<int>(slide.steps.size())));
        }
        next.push_back({
            .output_id = rt.output_id,
            .line_to_slide = rt.line_to_slide,
            .index = rt.index,
            .slide_steps = std::move(steps),
        });
    }

    reset_maps(std::move(next),
               built.player.total_lines,
               built.player.all_lines,
               built.player.all_line_labels);
}

std::optional<int> PlayerController::find_next_change_line(int start, int direction) const
{
    if (outputs_.empty() || total_lines_ <= 0) {
        return std::nullopt;
    }
    if (direction == 0) {
        return std::nullopt;
    }

    std::vector<int> cur_slides;
    cur_slides.reserve(outputs_.size());
    for (const auto& oc : outputs_) {
        cur_slides.push_back(slide_for_line(oc, start));
    }

    int candidate = start + direction;
    while (candidate >= 0 && candidate < total_lines_) {
        bool changed = false;
        for (std::size_t i = 0; i < outputs_.size(); ++i) {
            if (slide_for_line(outputs_[i], candidate) != cur_slides[i]) {
                changed = true;
                break;
            }
        }
        if (changed) {
            return candidate;
        }
        candidate += direction;
    }
    return std::nullopt;
}

int PlayerController::slide_at(const OutputNavState& oc, int line) noexcept
{
    if (line < 0 || line >= static_cast<int>(oc.line_to_slide.size())) {
        return oc.index;
    }
    return oc.line_to_slide[static_cast<std::size_t>(line)];
}

int PlayerController::steps_of(const OutputNavState& oc, int slide) noexcept
{
    if (slide < 0 || slide >= static_cast<int>(oc.slide_steps.size())) {
        return 1;
    }
    return std::max(1, oc.slide_steps[static_cast<std::size_t>(slide)]);
}

int PlayerController::steps_at(int line) const noexcept
{
    int most = 1;
    for (const auto& oc : outputs_) {
        most = std::max(most, steps_of(oc, slide_at(oc, line)));
    }
    return most;
}

int PlayerController::step() const noexcept
{
    int most = 0;
    int step = 0;
    for (const auto& oc : outputs_) {
        const int n = steps_of(oc, oc.index);
        if (n > most) {
            most = n;
            step = oc.step;
        }
    }
    return step;
}

void PlayerController::move_to(int line, Arrive arrive)
{
    std::vector<int> was;
    was.reserve(outputs_.size());
    for (const auto& oc : outputs_) {
        was.push_back(oc.index);
    }
    set_line_cursor(line);
    for (std::size_t i = 0; i < outputs_.size(); ++i) {
        auto& oc = outputs_[i];
        oc.within = false;
        oc.transition.reset();
        if (oc.index == was[i]) {
            // Its slide stays: as it was left, nothing playing again.
            oc.animate = false;
            continue;
        }
        oc.step = arrive == Arrive::Start ? 0 : steps_of(oc, oc.index) - 1;
        oc.animate = arrive == Arrive::Start;
    }
}

bool PlayerController::next_slide()
{
    if (outputs_.empty() || total_lines_ <= 0) {
        return false;
    }
    // A step first: each output whose slide has one left takes it.
    const bool stepping = std::any_of(outputs_.begin(), outputs_.end(), [](const OutputNavState& oc) {
        return oc.step + 1 < steps_of(oc, oc.index);
    });
    if (stepping) {
        for (auto& oc : outputs_) {
            oc.animate = oc.step + 1 < steps_of(oc, oc.index);
            oc.step += oc.animate ? 1 : 0;
            oc.within = true;
            oc.transition.reset();
        }
        return true;
    }
    if (line_cursor_ >= total_lines_ - 1) {
        return false;
    }
    const auto target = find_next_change_line(line_cursor_, 1);
    if (!target) {
        return false;
    }
    move_to(*target, Arrive::Start);
    return true;
}

bool PlayerController::prev_slide()
{
    if (outputs_.empty() || total_lines_ <= 0) {
        return false;
    }
    const bool stepping = std::any_of(outputs_.begin(), outputs_.end(), [](const OutputNavState& oc) {
        return oc.step > 0;
    });
    if (stepping) {
        for (auto& oc : outputs_) {
            oc.step = std::max(0, oc.step - 1);
            oc.animate = false;
            oc.within = true;
            oc.transition.reset();
        }
        return true;
    }
    if (line_cursor_ <= 0) {
        return false;
    }
    auto target = find_next_change_line(line_cursor_, -1);
    if (!target) {
        target = 0;
    }
    // The slide before, as it was left: fully built.
    move_to(*target, Arrive::Built);
    return true;
}

bool PlayerController::jump_to_line(int line_index)
{
    if (outputs_.empty() || line_index < 0 || line_index >= total_lines_) {
        return false;
    }
    move_to(line_index, Arrive::Start);
    return true;
}

void PlayerController::arrive(std::optional<model::Transition> how)
{
    for (auto& oc : outputs_) {
        oc.within = false;
        oc.animate = oc.step == 0;
        oc.transition = how;
    }
}

void PlayerController::settle(std::string_view output_id)
{
    for (auto& oc : outputs_) {
        if (output_id.empty() || oc.output_id == output_id) {
            oc.animate = false;
            oc.within = false;
            oc.transition.reset();
        }
    }
}

bool PlayerController::jump_to_step(int line, int step)
{
    if (outputs_.empty() || line < 0 || line >= total_lines_ || step < 0
        || step >= steps_at(line)) {
        return false;
    }
    std::vector<int> was;
    was.reserve(outputs_.size());
    for (const auto& oc : outputs_) {
        was.push_back(oc.index);
    }
    set_line_cursor(line);
    for (std::size_t i = 0; i < outputs_.size(); ++i) {
        auto& oc = outputs_[i];
        oc.within = oc.index == was[i];
        oc.step = std::min(step, steps_of(oc, oc.index) - 1);
        oc.animate = false;
        oc.transition.reset();
    }
    return true;
}

std::vector<std::pair<std::string, int>> PlayerController::steps() const
{
    std::vector<std::pair<std::string, int>> out;
    out.reserve(outputs_.size());
    for (const auto& oc : outputs_) {
        out.emplace_back(oc.output_id, oc.step);
    }
    return out;
}

void PlayerController::restore(int line, const std::vector<std::pair<std::string, int>>& steps)
{
    if (outputs_.empty() || total_lines_ <= 0) {
        return;
    }
    set_line_cursor(std::clamp(line, 0, total_lines_ - 1));
    for (auto& oc : outputs_) {
        const auto it = std::find_if(steps.begin(), steps.end(), [&oc](const auto& s) {
            return s.first == oc.output_id;
        });
        oc.step = it != steps.end() ? std::clamp(it->second, 0, steps_of(oc, oc.index) - 1) : 0;
        oc.animate = false;
        oc.within = false;
        oc.transition.reset();
    }
}

} // namespace seventhslide::session
