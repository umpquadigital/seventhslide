#include "seventhslide/geom/canvas_presets.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <numeric>

namespace seventhslide::geom {
namespace {

// Every preset can be an output's canvas, and is even on both sides, as the
// encoders behind NDI, SRT and a virtual camera want.
constexpr bool presets_fit()
{
    return std::ranges::all_of(k_canvas_presets, [](const CanvasPreset& p) {
        return p.size.valid() && p.size.width <= k_max_canvas.width
            && p.size.height <= k_max_canvas.height && p.size.width % 2 == 0
            && p.size.height % 2 == 0;
    });
}
static_assert(presets_fit());

// A new output's canvas has a name.
static_assert(std::ranges::find(k_canvas_presets, k_reference_canvas, &CanvasPreset::size)
              != std::end(k_canvas_presets));

// Each shape's presets are together, the shapes in menu order: a menu starts
// a group where the shape changes.
static_assert(std::ranges::is_sorted(k_canvas_presets, {}, &CanvasPreset::shape));

struct ShapeWords {
    std::string_view heading;
    /// What follows a preset's name to say its shape.
    std::string_view tag;
};

[[nodiscard]] constexpr ShapeWords words_of(CanvasShape shape) noexcept
{
    switch (shape) {
    case CanvasShape::Widescreen:
        return {"16:9 Widescreen", "16:9"};
    case CanvasShape::Widescreen16x10:
        return {"16:10 Projector", "16:10"};
    case CanvasShape::Standard:
        return {"4:3 Standard", "4:3"};
    case CanvasShape::Vertical:
        return {"9:16 Vertical", "Vertical"};
    case CanvasShape::Square:
        return {"1:1 Square", "Square"};
    }
    return {};
}

/// The ratios with a usual name, in lowest terms, each with its name.
struct NamedRatio {
    int width;
    int height;
    std::string_view name;
};
constexpr NamedRatio k_named_ratios[] = {
    {16, 9, "16:9"}, {9, 16, "9:16"}, {8, 5, "16:10"}, {5, 8, "10:16"},
    {4, 3, "4:3"},   {3, 4, "3:4"},   {5, 4, "5:4"},   {4, 5, "4:5"},
    {3, 2, "3:2"},   {2, 3, "2:3"},   {1, 1, "1:1"},   {32, 9, "32:9"},
    {9, 32, "9:32"},
};

} // namespace

std::string_view canvas_shape_heading(CanvasShape shape) noexcept
{
    return words_of(shape).heading;
}

std::string canvas_preset_title(const CanvasPreset& preset)
{
    std::string out(preset.name);
    out += ' ';
    out += words_of(preset.shape).tag;
    return out;
}

const CanvasPreset* find_canvas_preset(CanvasSize size) noexcept
{
    const auto it = std::ranges::find(k_canvas_presets, size, &CanvasPreset::size);
    return it == std::end(k_canvas_presets) ? nullptr : &*it;
}

std::string canvas_aspect_label(CanvasSize size)
{
    if (!size.valid()) {
        return {};
    }
    const int d = std::gcd(size.width, size.height);
    const int w = size.width / d;
    const int h = size.height / d;
    for (const auto& r : k_named_ratios) {
        if (r.width == w && r.height == h) {
            return std::string(r.name);
        }
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f:1", static_cast<double>(size.width) / size.height);
    return buf;
}

std::string canvas_size_problem(CanvasSize size)
{
    if (!size.valid()) {
        return "The canvas needs a width and a height";
    }
    if (size.width > k_max_canvas.width || size.height > k_max_canvas.height) {
        return "The canvas can be up to " + std::to_string(k_max_canvas.width) + " × "
             + std::to_string(k_max_canvas.height);
    }
    return {};
}

} // namespace seventhslide::geom
