#pragma once

// Canvas objects as playback paints them, made from a Freeform object.
//
// Freeform slides and theme elements both place ``freeform::Object``s, so both
// convert here: a field the painter reads cannot be carried on one path and
// forgotten on the other. Empty style fields fall back to the defaults the
// editors show (painted_type).
// Seam: plain C++20. Lives with Freeform so scene/ does not have to be
// included to compile a document.

#include "seventhslide/canvas/object.hpp"
#include "seventhslide/freeform/document.hpp"
#include "seventhslide/layout/overlays.hpp"

namespace seventhslide::freeform {

/// What a kind of object paints with where its own type fields are empty: a
/// clock and an indicator sit top-left, an indicator is 30 px; the rest take
/// the box defaults.
[[nodiscard]] canvas::TypeDefaults type_defaults(ObjectType type) noexcept;

/// Make ``type`` (an object of kind ``kind``'s typography) what playback
/// paints: each empty field from the kind's defaults, and the line height
/// clamped as text layout clamps it.
void fill_painted_type(canvas::BoxType& type, ObjectType kind);

/// ``obj``'s typography as playback paints it (fill_painted_type). The
/// editors show these values, so a ribbon cannot disagree with the slide.
[[nodiscard]] canvas::BoxType painted_type(const Object& obj);

/// How an image fills its box: its ``object_fit``, or ``contain``.
[[nodiscard]] std::string painted_object_fit(const Object& obj);

/// ``obj`` as playback paints it: the canvas object with its typography
/// filled in (type_defaults, not clamped: a clock lays out at a fixed line
/// height of its own), its fit named and its shape's adjustments clamped.
/// The scene carries this copy; a theme's Countdown element sets its
/// ``clock_mode`` on it.
[[nodiscard]] canvas::Object painted_object(const Object& obj);

/// An indicator's box, geometry and typography only: which verse or slide it
/// reads changes with the cursor, so its line is added per frame.
[[nodiscard]] layout::OverlayBox indicator_box(const Object& obj);

}  // namespace seventhslide::freeform
