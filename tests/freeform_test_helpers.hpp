#pragma once

// Freeform document checks and builders that only the tests need.

#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::test {

/// What breaks playback in ``doc``, one message per problem; empty when it
/// plays. Simple mode needs the same slide count on every output; line control
/// needs every deck's map to cover every cue, in order, onto slides it has.
[[nodiscard]] inline std::vector<std::string> validate(const freeform::Document& doc)
{
    std::vector<std::string> errs;
    if (!doc.line_control) {
        if (!freeform::slide_counts_equal(doc)) {
            errs.emplace_back("simple mode requires the same slide count on every output");
        }
        return errs;
    }
    const auto cues = doc.cues.size();
    for (const auto& deck : doc.decks) {
        const auto& map = deck.line_to_slide;
        if (map.size() != cues) {
            errs.push_back(deck.output_id + ": line_to_slide length must match cues");
            continue;
        }
        if (!std::is_sorted(map.begin(), map.end())) {
            errs.push_back(deck.output_id + ": line_to_slide must be non-decreasing");
        }
        const int last = std::max(0, deck.slide_count() - 1);
        if (std::any_of(map.begin(), map.end(), [last](int v) { return v < 0 || v > last; })) {
            errs.push_back(deck.output_id + ": line_to_slide index out of range");
        }
    }
    return errs;
}

/// Put ``obj`` on top of slide ``slide_index`` of ``output_id``'s deck, with a
/// fresh id when it has none. The editors add objects through their own
/// commands; tests build documents with this.
[[nodiscard]] inline freeform::EditResult add_object(freeform::Document& doc,
                                                     std::string_view output_id,
                                                     int slide_index,
                                                     freeform::Object obj)
{
    auto* slide = freeform::find_slide(doc, output_id, slide_index);
    if (slide == nullptr) {
        return freeform::EditResult::fail("unknown slide");
    }
    if (obj.id.empty()) {
        obj.id = freeform::alloc_id(doc, 'o');
    }
    int z = 0;
    for (const auto& other : slide->objects) {
        z = std::max(z, other.z + 1);
    }
    obj.z = z;
    slide->objects.push_back(std::move(obj));
    return freeform::EditResult::success();
}

}  // namespace seventhslide::test
