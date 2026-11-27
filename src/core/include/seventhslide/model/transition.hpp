#pragma once

// How a picture changes into the next: the transition of a slide, a
// background, an object, or a text theme context. Unset
// optional transitions inherit where something says what from; nothing set
// anywhere is a cut. Seam: plain C++20.

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::model {

/// What a transition does. Fade cross-fades the two pictures; Fade through
/// black takes the outgoing one down to nothing over the first half and
/// brings the incoming one up over the second. The rest move,
/// each from its direction's side:
///   Push    the incoming pushes the outgoing off
///   Wipe    an edge sweeps across, the incoming behind it
///   Split   the incoming opens out from the middle
///   Zoom    the incoming grows from the middle as the outgoing fades
///   Cover   the incoming slides in over the outgoing
///   Reveal  the outgoing slides off the incoming
enum class TransitionType {
    Cut,
    Fade,
    FadeThroughBlack,
    Push,
    Wipe,
    Split,
    Zoom,
    Cover,
    Reveal,
};

/// The side a moving transition comes from. Split reads Left and Right as
/// opening sideways, Up and Down as opening up and down.
enum class TransitionDirection {
    Left,
    Right,
    Up,
    Down,
};

/// Whether ``type`` moves the pictures (and so has a direction), rather
/// than fading them where they are.
[[nodiscard]] constexpr bool transition_moves(TransitionType type) noexcept
{
    return type == TransitionType::Push || type == TransitionType::Wipe
           || type == TransitionType::Split || type == TransitionType::Zoom
           || type == TransitionType::Cover || type == TransitionType::Reveal;
}

/// How a transition's time runs.
enum class TransitionEasing {
    EaseInOut,
    Linear,
};

/// How long a transition runs when nothing says otherwise.
inline constexpr int k_default_transition_ms = 500;

struct Transition {
    TransitionType type = TransitionType::Cut;
    int duration_ms = k_default_transition_ms;
    TransitionEasing easing = TransitionEasing::EaseInOut;
    /// Where a moving one comes from; the incoming picture enters from it.
    TransitionDirection direction = TransitionDirection::Right;

    /// True when it takes time: not a cut, and longer than nothing.
    [[nodiscard]] bool plays() const noexcept
    {
        return type != TransitionType::Cut && duration_ms > 0;
    }

    [[nodiscard]] bool operator==(const Transition&) const noexcept = default;
};

[[nodiscard]] std::string_view to_string(TransitionType type) noexcept;
/// Unknown names are a cut.
[[nodiscard]] TransitionType transition_type_from_string(std::string_view s) noexcept;
[[nodiscard]] std::string_view to_string(TransitionEasing easing) noexcept;
/// An unknown name reads as ease in and out.
[[nodiscard]] TransitionEasing transition_easing_from_string(std::string_view s) noexcept;
[[nodiscard]] std::string_view to_string(TransitionDirection direction) noexcept;
/// Unknown names are Right.
[[nodiscard]] TransitionDirection transition_direction_from_string(std::string_view s) noexcept;

/// ``linear_t`` (0 to 1, clamped) as ``easing`` runs it.
[[nodiscard]] double eased(TransitionEasing easing, double linear_t) noexcept;

/// The outgoing and incoming pictures' opacities at ``progress`` (eased, 0
/// to 1) of a ``type`` transition: a fade crosses them; fade through black
/// takes the outgoing one to nothing by the half and brings the incoming one
/// up from there.
struct TransitionBlend {
    double outgoing = 0.0;
    double incoming = 1.0;
};
[[nodiscard]] TransitionBlend blend_at(TransitionType type, double progress) noexcept;

/// Stored as ``{type, duration_ms}``, and ``easing`` and ``direction`` when
/// they are not the defaults, so what was stored before reads and writes
/// back the same.
void to_json(nlohmann::json& j, const Transition& spec);
void from_json(const nlohmann::json& j, Transition& spec);

/// Absent / null / empty object -> inherit (nullopt).
[[nodiscard]] std::optional<Transition> optional_transition_from_json(const nlohmann::json& j);
void optional_transition_to_json(nlohmann::json& parent,
                                 const char* key,
                                 const std::optional<Transition>& spec);

/// What a stored ``enable`` / ``duration`` pair means: a fade when enabled
/// with a positive duration, otherwise a cut.
[[nodiscard]] Transition transition_from_fade(bool enable, int duration_ms) noexcept;

} // namespace seventhslide::model
