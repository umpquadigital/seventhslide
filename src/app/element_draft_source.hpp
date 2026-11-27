#pragma once

// Where the theme controller gets the elements of the theme being designed:
// the designer's draft, folded into a style before it is painted or saved.
// Seam: plain C++20 + nlohmann/json.

#include <nlohmann/json.hpp>

namespace seventhslide::app {

class ElementDraftSource {
public:
    virtual ~ElementDraftSource() = default;
    /// Put the elements being edited into ``style`` under ``elements``;
    /// nothing when no theme is being designed here.
    virtual void apply_elements(nlohmann::json& style) const = 0;

protected:
    ElementDraftSource() = default;
    ElementDraftSource(const ElementDraftSource&) = default;
    ElementDraftSource& operator=(const ElementDraftSource&) = default;
};

}  // namespace seventhslide::app
