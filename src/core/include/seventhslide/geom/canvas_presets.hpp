#pragma once

// The canvas sizes an output is offered by name, and how a size is described.
//
// An output's canvas may be any size up to ``k_max_canvas``; these are the
// ones people ask for (screens, projectors, portrait signs), grouped by shape
// in the order the Output window lists them. A size none of them is is a
// custom one there.
//
// Plain C++20 (no Qt, no JSON).

#include <seventhslide/geom/canvas.hpp>

#include <string>
#include <string_view>

namespace seventhslide::geom {

/// The shapes the presets are grouped by, in menu order.
enum class CanvasShape : unsigned char { Widescreen, Widescreen16x10, Standard, Vertical, Square };

/// A canvas size with a name.
struct CanvasPreset {
    /// Its name under its shape's heading: "1080p", "WUXGA".
    std::string_view name;
    CanvasShape shape;
    CanvasSize size;
};

/// The presets, grouped by shape in menu order, largest first in each.
inline constexpr CanvasPreset k_canvas_presets[] = {
    {"4K", CanvasShape::Widescreen, {3840, 2160}},
    {"1440p", CanvasShape::Widescreen, {2560, 1440}},
    {"1080p", CanvasShape::Widescreen, {1920, 1080}},
    {"720p", CanvasShape::Widescreen, {1280, 720}},
    {"WUXGA", CanvasShape::Widescreen16x10, {1920, 1200}},
    {"WXGA", CanvasShape::Widescreen16x10, {1280, 800}},
    {"1080p", CanvasShape::Standard, {1440, 1080}},
    {"XGA", CanvasShape::Standard, {1024, 768}},
    {"4K", CanvasShape::Vertical, {2160, 3840}},
    {"1080p", CanvasShape::Vertical, {1080, 1920}},
    {"1080", CanvasShape::Square, {1080, 1080}},
};

/// The heading a shape's presets are listed under: "16:9 Widescreen".
[[nodiscard]] std::string_view canvas_shape_heading(CanvasShape shape) noexcept;

/// The preset's whole name, its shape after it: "1080p 16:9", "1080p Vertical".
[[nodiscard]] std::string canvas_preset_title(const CanvasPreset& preset);

/// The preset that is exactly ``size``, or null.
[[nodiscard]] const CanvasPreset* find_canvas_preset(CanvasSize size) noexcept;

/// ``size``'s shape as a ratio: its usual name when it is exactly one
/// ("16:9", "16:10", "4:3", "9:16"), else the width over the height to two
/// places ("2.37:1"). Empty when ``size`` is not valid.
[[nodiscard]] std::string canvas_aspect_label(CanvasSize size);

/// Why ``size`` cannot be an output's canvas, or empty when it can.
[[nodiscard]] std::string canvas_size_problem(CanvasSize size);

}  // namespace seventhslide::geom
