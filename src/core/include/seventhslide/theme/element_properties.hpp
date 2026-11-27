#pragma once

// The editable properties of a theme element: its own (visibility, label,
// body options) by the names the theme designer uses, and every object
// property through freeform::set_object_property, so the designer and the
// Freeform editor accept the same values for the same field.
//
// Seam: plain C++20 + nlohmann/json (no Qt).

#include "seventhslide/freeform/doc_edit.hpp"
#include "seventhslide/theme/theme_elements.hpp"

#include <nlohmann/json.hpp>

#include <string_view>

namespace seventhslide::theme {

/// Set property ``name`` of ``el`` from ``value``. Fails, leaving ``el``
/// unchanged, for an unknown name, a wrong type or a refused value. Geometry is
/// not here (see freeform/object_properties.hpp).
[[nodiscard]] freeform::EditResult set_element_property(Element& el,
                                                        std::string_view name,
                                                        const nlohmann::json& value);

/// Every property set_element_property takes, by the same names: the
/// element's own and its object's (freeform::object_property_values), as they
/// paint.
[[nodiscard]] nlohmann::json element_property_values(const Element& el);

/// The span set_element_property holds each number property of ``el`` to,
/// by the same names: the element's own and its object's
/// (freeform::object_property_ranges).
[[nodiscard]] nlohmann::json element_property_ranges(const Element& el);

}  // namespace seventhslide::theme
