#include "seventhslide/model/transition.hpp"

#include "seventhslide/json/read.hpp"

#include <algorithm>

namespace seventhslide::model {

Transition transition_from_fade(bool enable, int duration_ms) noexcept
{
    Transition spec;
    spec.duration_ms = std::max(0, duration_ms);
    spec.type = enable && spec.duration_ms > 0 ? TransitionType::Fade : TransitionType::Cut;
    return spec;
}

std::string_view to_string(TransitionType type) noexcept
{
    switch (type) {
    case TransitionType::Fade:
        return "fade";
    case TransitionType::FadeThroughBlack:
        return "fade_through_black";
    case TransitionType::Push:
        return "push";
    case TransitionType::Wipe:
        return "wipe";
    case TransitionType::Split:
        return "split";
    case TransitionType::Zoom:
        return "zoom";
    case TransitionType::Cover:
        return "cover";
    case TransitionType::Reveal:
        return "reveal";
    case TransitionType::Cut:
        break;
    }
    return "cut";
}

TransitionType transition_type_from_string(std::string_view s) noexcept
{
    for (const auto type :
         {TransitionType::Fade, TransitionType::FadeThroughBlack, TransitionType::Push,
          TransitionType::Wipe, TransitionType::Split, TransitionType::Zoom,
          TransitionType::Cover, TransitionType::Reveal}) {
        if (s == to_string(type)) {
            return type;
        }
    }
    return TransitionType::Cut;
}

std::string_view to_string(TransitionDirection direction) noexcept
{
    switch (direction) {
    case TransitionDirection::Left:
        return "left";
    case TransitionDirection::Up:
        return "up";
    case TransitionDirection::Down:
        return "down";
    case TransitionDirection::Right:
        break;
    }
    return "right";
}

TransitionDirection transition_direction_from_string(std::string_view s) noexcept
{
    if (s == "left") {
        return TransitionDirection::Left;
    }
    if (s == "up") {
        return TransitionDirection::Up;
    }
    if (s == "down") {
        return TransitionDirection::Down;
    }
    return TransitionDirection::Right;
}

std::string_view to_string(TransitionEasing easing) noexcept
{
    return easing == TransitionEasing::Linear ? "linear" : "ease_in_out";
}

TransitionEasing transition_easing_from_string(std::string_view s) noexcept
{
    return s == "linear" ? TransitionEasing::Linear : TransitionEasing::EaseInOut;
}

TransitionBlend blend_at(TransitionType type, double progress) noexcept
{
    const double p = std::clamp(progress, 0.0, 1.0);
    if (type == TransitionType::FadeThroughBlack) {
        return {std::max(0.0, 1.0 - 2.0 * p), std::max(0.0, 2.0 * p - 1.0)};
    }
    if (transition_moves(type) && type != TransitionType::Zoom) {
        // Both stay whole; where each is, is the motion's (render's).
        return {1.0, 1.0};
    }
    return {1.0 - p, p};
}

double eased(TransitionEasing easing, double linear_t) noexcept
{
    const double t = std::clamp(linear_t, 0.0, 1.0);
    if (easing == TransitionEasing::Linear) {
        return t;
    }
    // In-out quadratic: QEasingCurve::InOutQuad, which the presenter used.
    return t < 0.5 ? 2.0 * t * t : 1.0 - 2.0 * (1.0 - t) * (1.0 - t);
}

void to_json(nlohmann::json& j, const Transition& spec)
{
    j = nlohmann::json{
        {"type", std::string(to_string(spec.type))},
        {"duration_ms", spec.duration_ms},
    };
    if (spec.easing != TransitionEasing::EaseInOut) {
        j["easing"] = std::string(to_string(spec.easing));
    }
    if (spec.direction != TransitionDirection::Right) {
        j["direction"] = std::string(to_string(spec.direction));
    }
}

void from_json(const nlohmann::json& j, Transition& spec)
{
    spec = {};
    if (!j.is_object()) {
        return;
    }
    spec.type = transition_type_from_string(seventhslide::json::str_or(j, "type"));
    spec.duration_ms = std::max(0, seventhslide::json::int_or(j, "duration_ms", k_default_transition_ms));
    spec.easing = transition_easing_from_string(seventhslide::json::str_or(j, "easing"));
    spec.direction = transition_direction_from_string(seventhslide::json::str_or(j, "direction"));
}

std::optional<Transition> optional_transition_from_json(const nlohmann::json& j)
{
    if (j.is_null() || !j.is_object() || j.empty()) {
        return std::nullopt;
    }
    Transition spec;
    from_json(j, spec);
    return spec;
}

void optional_transition_to_json(nlohmann::json& parent,
                                 const char* key,
                                 const std::optional<Transition>& spec)
{
    if (!spec) {
        return;
    }
    parent[key] = *spec;
}

} // namespace seventhslide::model
