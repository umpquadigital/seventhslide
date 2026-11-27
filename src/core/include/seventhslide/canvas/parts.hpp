#pragma once

// The parts of a canvas object: what fills it, its outline, its effects, its
// paragraph, picture, video and table. A box that is not an object
// takes the parts that apply to it too (the theme's body text box, box.hpp,
// has a fill and a line), so they live apart from the object.
//
// Each part has a stored form under its own key of the object; a part is
// absent while it is the default. Keys stored objects already had keep their
// names (``fill``, ``stroke``, ``stroke_width``), and those live on the object
// (object.hpp).
//
// Seam: plain C++20 + nlohmann/json.

#include "seventhslide/model/transition.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::canvas {

/// A new shape's fill and border: opaque, so it shows plainly when it is
/// placed. White, outlined in a light grey that still shows on a white slide.
inline constexpr std::string_view k_default_shape_fill = "#ffffff";
inline constexpr std::string_view k_default_shape_stroke = "#bfbfbf";
/// A text box's fill when Solid is turned on with no colour: translucent
/// white (``#aarrggbb``), so text in the default colour still shows on it.
inline constexpr std::string_view k_default_box_fill = "#14ffffff";

enum class FillKind {
    Solid,
    None,
    Gradient,
    Picture,
};

enum class GradientKind {
    Linear,
    Radial,
};

[[nodiscard]] std::string_view to_string(GradientKind kind) noexcept;
[[nodiscard]] GradientKind gradient_kind_from_string(std::string_view s) noexcept;

struct GradientStop {
    /// 0 at the start, 1 at the end.
    double at = 0;
    std::string color;

    [[nodiscard]] bool operator==(const GradientStop&) const noexcept = default;
};

struct Gradient {
    GradientKind kind = GradientKind::Linear;
    /// Linear: the direction, in degrees, 90 running top to bottom.
    double angle = 90;
    /// Radial: where it starts, as shares of the box; the middle by default.
    double center_x = 0.5;
    double center_y = 0.5;
    std::vector<GradientStop> stops;

    [[nodiscard]] bool operator==(const Gradient&) const noexcept = default;
};

enum class Recolor {
    None,
    Grayscale,
    Tint,
};

[[nodiscard]] std::string_view to_string(Recolor recolor) noexcept;
[[nodiscard]] Recolor recolor_from_string(std::string_view s) noexcept;

/// The most a crop takes off one edge: the picture keeps a tenth.
inline constexpr double k_max_crop = 0.9;

/// Fractions of a picture cut off each edge.
struct Crop {
    double left = 0;
    double top = 0;
    double right = 0;
    double bottom = 0;

    [[nodiscard]] bool operator==(const Crop&) const noexcept = default;
};

/// A picture filling a shape or a text box.
struct PictureFill {
    std::string file;
    std::string fit = "cover";
    Crop crop;

    [[nodiscard]] bool operator==(const PictureFill&) const noexcept = default;
};

/// What fills a shape or a text box. ``color`` is the solid fill, stored as
/// ``fill``; empty paints nothing, and that is what every object but a shape
/// starts with (a new shape takes k_default_shape_fill). The kind says
/// whether the colour, a gradient, a picture or nothing paints.
struct Fill {
    FillKind kind = FillKind::Solid;
    std::string color;
    Gradient gradient;
    PictureFill picture;

    [[nodiscard]] bool operator==(const Fill&) const noexcept = default;
};

enum class LineDash {
    Solid,
    Dash,
    Dot,
    DashDot,
};

enum class LineEnd {
    None,
    Arrow,
    Dot,
};

[[nodiscard]] std::string_view to_string(FillKind kind) noexcept;
[[nodiscard]] FillKind fill_kind_from_string(std::string_view s) noexcept;

/// Whether ``fill`` paints anything: a solid fill with a colour, a gradient
/// with two stops or more, a picture fill with a file.
[[nodiscard]] bool fill_paints(const Fill& fill) noexcept;

/// Widest outline a shape takes, in canvas pixels.
inline constexpr double k_max_line_width = 200.0;

/// The outline of a shape or a text box, and a line's ends. ``color`` and
/// ``width`` are stored as ``stroke`` and ``stroke_width``; an empty colour
/// or a width of 0 is none. Every object but a shape starts without one.
struct Line {
    std::string color;
    double width = 2.0;
    LineDash dash = LineDash::Solid;
    LineEnd start = LineEnd::None;
    LineEnd end = LineEnd::None;

    [[nodiscard]] bool operator==(const Line&) const noexcept = default;
};

[[nodiscard]] std::string_view to_string(LineDash dash) noexcept;
[[nodiscard]] LineDash line_dash_from_string(std::string_view s) noexcept;
[[nodiscard]] std::string_view to_string(LineEnd end) noexcept;
[[nodiscard]] LineEnd line_end_from_string(std::string_view s) noexcept;

/// Whether ``line`` paints anything: a colour and a width.
[[nodiscard]] bool line_paints(const Line& line) noexcept;

struct Shadow {
    bool on = false;
    std::string color = "#80000000";
    /// Pixels, on the design canvas.
    double blur = 8;
    double distance = 6;
    /// Degrees, 0 to the right, 90 straight down.
    double angle = 45;

    [[nodiscard]] bool operator==(const Shadow&) const noexcept = default;
};

struct Glow {
    bool on = false;
    std::string color = "#80ffffff";
    double size = 10;

    [[nodiscard]] bool operator==(const Glow&) const noexcept = default;
};

/// An outline round each glyph. Width 0 is none.
struct TextOutline {
    std::string color = "#000000";
    double width = 0;

    [[nodiscard]] bool operator==(const TextOutline&) const noexcept = default;
};

struct Effects {
    Shadow shadow;
    Glow glow;
    /// Pixels the edge fades over; 0 is a hard edge.
    double soft_edge = 0;
    TextOutline text_outline;
    Shadow text_shadow;

    [[nodiscard]] bool operator==(const Effects&) const noexcept = default;
};

/// How far past its box the effects paint, in canvas pixels: a shadow's
/// distance and blur, a glow's size. A soft edge fades inward. What a clip
/// or a raster round the box must allow for.
[[nodiscard]] double effects_reach(const Effects& effects) noexcept;

enum class Bullets {
    None,
    Disc,
    Number,
};

enum class Autofit {
    None,
    Shrink,
};

struct Margins {
    double left = 0;
    double top = 0;
    double right = 0;
    double bottom = 0;

    [[nodiscard]] bool operator==(const Margins&) const noexcept = default;
};

[[nodiscard]] std::string_view to_string(Bullets bullets) noexcept;
[[nodiscard]] Bullets bullets_from_string(std::string_view s) noexcept;
[[nodiscard]] std::string_view to_string(Autofit autofit) noexcept;
[[nodiscard]] Autofit autofit_from_string(std::string_view s) noexcept;

/// How a text box sets its paragraphs. Alignment and line height are the
/// box's type (BoxType); these are the rest. Each logical line of the box is
/// a paragraph: bullets mark each, the spaces go between them, the indent is
/// each one's first row, the margins inset every row, and autofit shrinks
/// the type until they fit the box.
struct Paragraph {
    Bullets bullets = Bullets::None;
    /// Pixels before and after each paragraph, and the first line's indent.
    double space_before = 0;
    double space_after = 0;
    double indent = 0;
    Autofit autofit = Autofit::None;
    Margins margins;
    int columns = 1;
    double column_gap = 0;

    [[nodiscard]] bool operator==(const Paragraph&) const noexcept = default;
};

/// A picture object's adjustments; its file and fit are the object's
/// ``filename`` and ``object_fit``.
struct Picture {
    Crop crop;
    Recolor recolor = Recolor::None;
    std::string tint;
    /// 0 opaque, 1 invisible.
    double transparency = 0;

    [[nodiscard]] bool operator==(const Picture&) const noexcept = default;
};

enum class VideoStart {
    OnShow,
    OnClick,
};

[[nodiscard]] std::string_view to_string(VideoStart start) noexcept;
[[nodiscard]] VideoStart video_start_from_string(std::string_view s) noexcept;

/// A video object; its file and fit are the object's ``filename`` and
/// ``object_fit``.
struct Video {
    VideoStart start = VideoStart::OnShow;
    bool loop = false;
    bool mute = false;
    /// A picture shown until it plays; a library image.
    std::string poster;

    [[nodiscard]] bool operator==(const Video&) const noexcept = default;
};

/// The most rows, or columns, a table takes.
inline constexpr int k_max_table_side = 20;

struct Table {
    int rows = 0;
    int columns = 0;
    /// Row-major, ``rows * columns`` texts.
    std::vector<std::string> cells;
    std::string border_color = "#ffffff";
    double border_width = 1;
    bool header = false;
    bool banding = false;

    [[nodiscard]] bool operator==(const Table&) const noexcept = default;
};

/// What an animation does to its object. An entrance
/// brings it on, an exit takes it off, an emphasis moves it and leaves it as
/// it was. Fly and Wipe come from (or go to) a side.
enum class AnimationEffect {
    // Entrances
    Appear,
    Fade,
    FlyIn,
    Wipe,
    Zoom,
    Float,
    // Emphasis
    Pulse,
    Grow,
    // Exits
    Disappear,
    FadeOut,
    FlyOut,
    WipeOut,
    ZoomOut,
    FloatOut,
};

enum class AnimationKind {
    Entrance,
    Emphasis,
    Exit,
};

[[nodiscard]] constexpr AnimationKind animation_kind(AnimationEffect effect) noexcept
{
    switch (effect) {
    case AnimationEffect::Pulse:
    case AnimationEffect::Grow:
        return AnimationKind::Emphasis;
    case AnimationEffect::Disappear:
    case AnimationEffect::FadeOut:
    case AnimationEffect::FlyOut:
    case AnimationEffect::WipeOut:
    case AnimationEffect::ZoomOut:
    case AnimationEffect::FloatOut:
        return AnimationKind::Exit;
    case AnimationEffect::Appear:
    case AnimationEffect::Fade:
    case AnimationEffect::FlyIn:
    case AnimationEffect::Wipe:
    case AnimationEffect::Zoom:
    case AnimationEffect::Float:
        break;
    }
    return AnimationKind::Entrance;
}

/// Whether ``e`` comes from (or goes to) a side: fly and wipe. The others
/// keep no direction.
[[nodiscard]] constexpr bool animation_has_direction(AnimationEffect e) noexcept
{
    return e == AnimationEffect::FlyIn || e == AnimationEffect::FlyOut
           || e == AnimationEffect::Wipe || e == AnimationEffect::WipeOut;
}

/// When an animation starts: on the operator's next click (a step of its
/// own), with the animation before it, or after it has finished.
enum class AnimationTrigger {
    OnClick,
    WithPrevious,
    AfterPrevious,
};

/// One animation of an object. ``order`` places it among every animation on
/// its slide (the Build pane's order); the slide's steps are its on-click
/// animations, plus the one the slide comes in on.
struct Animation {
    int order = 0;
    AnimationEffect effect = AnimationEffect::Fade;
    AnimationTrigger trigger = AnimationTrigger::OnClick;
    /// Fly and Wipe: the side it comes from, or goes to.
    model::TransitionDirection direction = model::TransitionDirection::Left;
    int duration_ms = 500;
    int delay_ms = 0;

    [[nodiscard]] bool operator==(const Animation&) const noexcept = default;
};

[[nodiscard]] std::string_view to_string(AnimationEffect effect) noexcept;
[[nodiscard]] AnimationEffect animation_effect_from_string(std::string_view s) noexcept;
[[nodiscard]] std::string_view to_string(AnimationTrigger trigger) noexcept;
[[nodiscard]] AnimationTrigger animation_trigger_from_string(std::string_view s) noexcept;

/// The type a line of a box is set in where it differs from the box's: an
/// empty family, a size of 0, a flag left unset or an empty colour is the
/// box's own. A follow-lines theme sets its active line and its other lines
/// in types of their own.
struct LineType {
    std::string family;
    int size = 0;
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<bool> underline;
    std::string color;

    /// True when every field is the box's.
    [[nodiscard]] bool empty() const noexcept
    {
        return family.empty() && size <= 0 && !bold && !italic && !underline && color.empty();
    }
    [[nodiscard]] bool operator==(const LineType&) const noexcept = default;
};

/// Where a click on the object takes the show.
/// The parts' stored forms, under the object's keys for each.
void to_json(nlohmann::json& j, const Crop& v);
void from_json(const nlohmann::json& j, Crop& v);
void to_json(nlohmann::json& j, const Gradient& v);
void from_json(const nlohmann::json& j, Gradient& v);
void to_json(nlohmann::json& j, const PictureFill& v);
void from_json(const nlohmann::json& j, PictureFill& v);
void to_json(nlohmann::json& j, const Shadow& v);
void from_json(const nlohmann::json& j, Shadow& v);
void to_json(nlohmann::json& j, const Effects& v);
void from_json(const nlohmann::json& j, Effects& v);
void to_json(nlohmann::json& j, const Paragraph& v);
void from_json(const nlohmann::json& j, Paragraph& v);
void to_json(nlohmann::json& j, const Picture& v);
void from_json(const nlohmann::json& j, Picture& v);
void to_json(nlohmann::json& j, const Video& v);
void from_json(const nlohmann::json& j, Video& v);
void to_json(nlohmann::json& j, const Table& v);
void from_json(const nlohmann::json& j, Table& v);
void to_json(nlohmann::json& j, const Animation& v);
void from_json(const nlohmann::json& j, Animation& v);
/// A line type stores the fields it sets; one it leaves to the box is absent.
void to_json(nlohmann::json& j, const LineType& v);
void from_json(const nlohmann::json& j, LineType& v);

}  // namespace seventhslide::canvas
