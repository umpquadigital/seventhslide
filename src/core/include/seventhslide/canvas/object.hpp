#pragma once

// The one placed thing on a canvas: a Freeform slide's object, a theme's
// element (which wraps one), and what the scene paints. Its geometry is
// BoxFrame, its typography BoxType; the rest is its parts (parts.hpp): what
// fills it, its outline, its effects, its paragraph, picture, video, table and
// link. A part is absent from the stored JSON while it is the default, and
// keys that stored objects already had keep their names (fill, stroke,
// filename, object_fit, shape_kind, ...), so a theme saved before the parts
// existed reads as it did.
//
// Seam: plain C++20 + nlohmann/json. Values are read as stored: the clamps
// text layout applies (line height, gap, tracking) are the Freeform
// document's, which reads an object through here (freeform/document.hpp).

#include "seventhslide/canvas/box.hpp"
#include "seventhslide/canvas/parts.hpp"
#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/geom/shape_path.hpp"
#include "seventhslide/model/transition.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::canvas {

enum class ObjectType {
    Text,
    Image,
    Video,
    Shape,
    Table,
    Indicator,
    Clock,
};

[[nodiscard]] std::string_view to_string(ObjectType type) noexcept;
[[nodiscard]] ObjectType object_type_from_string(std::string_view s) noexcept;

/// When the current cue has no verse label, paint slide ``n/N`` or nothing.
enum class IndicatorFallback {
    Slide,
    Blank,
};

[[nodiscard]] std::string_view to_string(IndicatorFallback fallback) noexcept;
[[nodiscard]] IndicatorFallback indicator_fallback_from_string(std::string_view s) noexcept;

/// The gap between an indicator's labels, in spaces of its font. One space is
/// how indicators have always looked.
inline constexpr double k_default_indicator_gap = 1.0;
inline constexpr double k_min_indicator_gap = 0.1;
inline constexpr double k_max_indicator_gap = 4.0;

/// ``gap`` in the range an indicator takes; not-a-number reads as the default.
[[nodiscard]] double clamp_indicator_gap(double gap) noexcept;

/// What a clock object counts. ``Countdown`` reads the playing video's
/// remaining time; a theme's Countdown element is a Clock with this mode.
enum class ClockMode {
    WallClock,
    Countdown,
};

/// A vertical alignment read from stored or edited data: ``top``, ``middle``
/// or ``bottom``, or empty for the object type's own default. Anything else
/// reads as ``middle``, which is how paint has always shown it.
[[nodiscard]] std::string read_vertical_align(std::string_view value);


// --- the object ----------------------------------------------------------

struct Object : BoxFrame, BoxType {
    std::string id;
    ObjectType type = ObjectType::Text;

    int z = 0;
    /// Empty = ungrouped. Objects that share a ``group_id`` select and transform
    /// as a unit. Nested groups flatten into the newest id.
    std::string group_id;
    bool locked = false;

    // Text
    std::string text;
    Paragraph paragraph;

    // Image and Video: the library file and how it fills the box.
    std::string filename;
    std::string object_fit = "contain";
    Picture picture;
    Video video;

    // Shape
    geom::ShapeKind shape_kind = geom::ShapeKind::Rectangle;
    /// Normalized outline adjustments; the shape reflows from these on resize.
    geom::ShapeAdjust shape_adjust{};

    // Shapes and text boxes
    Fill fill;
    Line line;
    Effects effects;

    Table table;

    /// Indicator only. Default: ``n/N`` when the current cue has no verse.
    IndicatorFallback indicator_fallback = IndicatorFallback::Slide;
    /// Indicator only. The gap between labels, in spaces of the box font
    /// (``k_min_indicator_gap``-``k_max_indicator_gap``).
    double indicator_gap = k_default_indicator_gap;

    /// Clock only. Paint uses current time; these flags are never baked as text.
    bool clock_24h = false;
    bool clock_seconds = false;
    ClockMode clock_mode = ClockMode::WallClock;

    /// How the object comes and goes: a cut unless it dissolves. Per object;
    /// there is no document or slide value to inherit.
    model::Transition transition;

    [[nodiscard]] bool operator==(const Object&) const noexcept = default;
};

/// Whether ``obj`` paints a frame behind its words: a text box, an indicator
/// or a clock with a fill or a line. (A shape's fill and line are its outline,
/// and paint whatever this says.)
[[nodiscard]] bool paints_frame(const Object& obj) noexcept;

/// A drawn shape's point as stored: [x, y], or [x, y, 1] when smooth. Nothing
/// for anything else.
[[nodiscard]] std::optional<geom::PathPoint> path_point_from_json(const nlohmann::json& j);

/// The picture or video ``obj`` places (``filename``), as a ``/static/...``
/// URL (model::static_media_url); nothing for any other object, or a name
/// that cannot be served. A picture fill's file is ``fill_picture_url``'s.
[[nodiscard]] std::optional<std::string> object_media_url(const Object& obj);

/// The picture a fill paints with, as a ``/static/...`` URL: nothing unless
/// the fill is a picture with a file that can be served.
[[nodiscard]] std::optional<std::string> fill_picture_url(const Fill& fill);

/// Carry ``obj``'s geometry and every pixel size it holds (type size, gap,
/// tracking, line width, effect sizes, margins, spacing, the table's border)
/// through ``mapping``. A part that paints nothing (a line with no colour, an
/// effect that is off) keeps its sizes, so the stored form, which leaves such
/// a part out, maps to the same object. Text layout's clamps are not applied.
void map_object(Object& obj, const geom::CanvasMapping& mapping);

/// ``map_object`` on a stored object, in place: only the mapped fields
/// change, and keys the model does not know are kept.
void map_object_json(nlohmann::json& obj, const geom::CanvasMapping& mapping);

// --- a resize ------------------------------------------------------------
//
// When an output changes canvas, what is laid out on it is carried over once
// by ``geom::CanvasMapping::resize``: each object's centre moves per axis and
// its type and lengths scale by the geometric mean of the two factors. A text
// box's frame stretches per axis with it. A picture or a shape keeps its
// proportions instead, sized by the same mean, so a circle stays a circle;
// one that covered the whole canvas still does, and one that no longer fits
// is pulled in.

/// True for the kinds whose frame keeps its proportions through a resize (a
/// picture, a video, a shape); a text box's frame stretches per axis.
[[nodiscard]] bool keeps_proportions(ObjectType type) noexcept;

/// Where a proportion-keeping frame ``before``, on ``from``, lands on ``to``:
/// per axis when it covered ``from`` (so it still covers), else at the mean
/// scale about its moved centre, shrunk to fit ``to`` if it is larger, and
/// kept inside ``to`` if it was inside ``from``.
[[nodiscard]] BoxRect resized_frame(const BoxRect& before, geom::CanvasSize from,
                                    geom::CanvasSize to);

/// The step after ``map_object(obj, CanvasMapping::resize(from, to))``: a
/// picture or shape whose frame was ``before`` takes ``resized_frame``; a
/// text box keeps the per-axis frame it was mapped to.
void keep_proportions(Object& obj, const BoxRect& before, geom::CanvasSize from,
                      geom::CanvasSize to);
/// ``keep_proportions`` on a stored object, whose ``type`` says which it is.
void keep_proportions_json(nlohmann::json& obj, const BoxRect& before, geom::CanvasSize from,
                           geom::CanvasSize to);

/// A stored object's frame (``x``, ``y``, ``w``, ``h``; a missing field reads
/// as 0).
[[nodiscard]] BoxRect frame_of_json(const nlohmann::json& obj);

/// Carry ``obj`` from ``from`` onto ``to`` as a resize does: ``map_object`` by
/// ``CanvasMapping::resize``, then ``keep_proportions``.
void resize_object(Object& obj, geom::CanvasSize from, geom::CanvasSize to);
void resize_object_json(nlohmann::json& obj, geom::CanvasSize from, geom::CanvasSize to);

/// True for ``x``, ``y``, ``width``, ``height`` and ``rotation``, which the
/// editors set through their geometry commands.
[[nodiscard]] bool is_geometry_property(std::string_view name) noexcept;

/// ``frame`` with geometry property ``name`` (see is_geometry_property) set
/// to ``value``; rotation is brought into one turn. Nothing for any other
/// name. One table answers both, so a name cannot be geometry without a field
/// to write.
[[nodiscard]] std::optional<canvas::BoxFrame> with_geometry_property(canvas::BoxFrame frame,
                                                                     std::string_view name,
                                                                     double value) noexcept;

/// ``degrees`` turned into ``[0, 360)``; 0 when it is not finite.
[[nodiscard]] double normalized_rotation(double degrees) noexcept;

/// The stored form. Keys stored objects have always had keep their names; a
/// part is written only when it is not the default.
void to_json(nlohmann::json& j, const Object& v);
void from_json(const nlohmann::json& j, Object& v);


}  // namespace seventhslide::canvas
