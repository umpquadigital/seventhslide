#pragma once

// A slide's own background, painted instead of its theme's: a Freeform slide
// given a colour, an image or a background theme of its own.
// Plain C++20 (no Qt).

#include "seventhslide/model/transition.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::layout {

/// What a colour fill that names no colour paints.
inline constexpr std::string_view k_default_fill_color = "#000000";

/// What a background paints. ``Transparent`` lets the stack underneath show.
enum class BackgroundType {
    Transparent,
    Color,
    Image,
    Video,
};

/// ``transparent``, ``color``, ``image`` or ``video``: the stored
/// ``background_type``.
[[nodiscard]] std::string_view to_string(BackgroundType type) noexcept;

/// The type a stored ``background_type`` names. Missing, blank or unknown is
/// ``Transparent``, which is what paint has always made of them.
[[nodiscard]] BackgroundType background_type_from_string(std::string_view type) noexcept;

/// Replaces the ``background_*`` keys of the slide's resolved style.
struct BackgroundFill {
    /// ``Transparent`` unless the style names another: the same default as a
    /// resolved background theme (which the theme designer shows), so an empty
    /// Base theme is one fill whichever reader runs.
    BackgroundType type = BackgroundType::Transparent;
    /// Solid fill, or the poster behind a video that has no frame yet.
    /// k_default_fill_color only when ``type`` is ``Color`` and the style has
    /// no color.
    /// A blank video poster stays blank.
    std::string color{k_default_fill_color};
    std::string image;
    std::string video;

    [[nodiscard]] bool operator==(const BackgroundFill&) const noexcept = default;
};

/**
 * The fill a background theme's ``style`` paints.
 *
 * ``nullopt`` when ``style`` is not an object. A missing, blank or unknown
 * ``background_type`` is ``Transparent``. k_default_fill_color is filled in
 * only when the type is ``Color`` and the color is missing or blank.
 */
[[nodiscard]] std::optional<BackgroundFill> background_fill_from_style(
    const nlohmann::json& style);

/// How a background theme's fill comes in: ``bg_enable_fade`` and
/// ``bg_fade_duration``.
struct BackgroundFade {
    bool enabled = false;
    int duration_ms = model::k_default_transition_ms;

    [[nodiscard]] bool operator==(const BackgroundFade&) const noexcept = default;
};

/// The fade a background theme's ``style`` names, its defaults for anything
/// it does not. The duration is truncated, as the painters have always read it.
[[nodiscard]] BackgroundFade background_fade_from_style(const nlohmann::json& style);

}  // namespace seventhslide::layout
