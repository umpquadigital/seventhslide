#pragma once

// The editable properties of a canvas object, by the name the design ribbon
// uses for each (the slide editor's ribbon and sidebar). The theme designer and the
// Freeform editor both write object fields through here, so a field accepts
// the same values, and refuses the same ones, in either.
//
// Geometry (x, y, width, height, rotation) is not in the property table: the
// editors place objects through their geometry commands, which know about
// locks and units. The geometry names, and the field each writes, are here
// (is_geometry_property, with_geometry_property).
//
// Seam: plain C++20 + nlohmann/json (no Qt).

#include "seventhslide/canvas/property_value.hpp"
#include "seventhslide/freeform/document.hpp"
#include "seventhslide/freeform/doc_edit.hpp"
#include "seventhslide/geom/canvas.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::freeform {

/// The strict readers of a property value are canvas/property_value.hpp's.
namespace property_value = canvas::property_value;

// Field writers both property tables use (this one and the theme element's
// on top of it), so a flag or a colour is refused in the same words in either
// editor.

/// ``field`` set from a bool, or "expected on or off".
[[nodiscard]] EditResult set_flag_field(bool& field, const nlohmann::json& v);

/// ``field`` set from a colour string, or "color required" (an empty one too).
[[nodiscard]] EditResult set_color_field(std::string& field, const nlohmann::json& v);

/// Widest outline a shape takes, in canvas pixels.
inline constexpr double k_max_stroke_width = canvas::k_max_line_width;

/// Largest font size a box takes, in canvas pixels: the height of the
/// largest canvas an output may declare, past which no line shows whole.
inline constexpr int k_max_font_size = geom::k_max_canvas.height;

/**
 * Set property ``name`` of ``obj`` from ``value``, a ribbon value as JSON: a
 * number, a bool or a string, whichever the field takes.
 *
 * Fails, leaving ``obj`` unchanged, for an unknown name, a value of the wrong
 * type, or a value the field refuses (an alignment it does not have, an empty
 * font). Numbers are brought into the field's range rather than refused.
 */
[[nodiscard]] EditResult set_object_property(Object& obj,
                                             std::string_view name,
                                             const nlohmann::json& value);

/// Every property set_object_property takes, and the geometry names, with
/// ``obj``'s value as it paints (painted_type: a kind's defaults filled in), so
/// a ribbon reads by the same names it writes. Geometry is in whole pixels, as
/// the ribbon shows it.
[[nodiscard]] nlohmann::json object_property_values(const Object& obj);

/// The span set_object_property holds each number property of ``obj`` to,
/// by name: ``{"font_size": {"min": 8, "max": 4320}, …}``, in the units
/// object_property_values reads (a share as 0 to 1). A number with no entry
/// takes any value (an angle, brought into one turn). For ``obj`` because a
/// shape's kind narrows one (a double arrow's head).
[[nodiscard]] nlohmann::json object_property_ranges(const Object& obj);

// The geometry names, and the field each writes, are the object's
// (canvas/object.hpp); these are their names here.
using canvas::is_geometry_property;
using canvas::normalized_rotation;
using canvas::with_geometry_property;

}  // namespace seventhslide::freeform
