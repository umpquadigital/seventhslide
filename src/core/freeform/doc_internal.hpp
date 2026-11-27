#pragma once

// Helpers the doc_*.cpp files share. Not part of the public API.
// Seam: plain C++20 (no Qt).

#include "seventhslide/freeform/document.hpp"

#include <vector>

namespace seventhslide::freeform {

/// A slide with a fresh id and nothing on it.
[[nodiscard]] Slide empty_slide(Document& doc);

/// A copy of ``src``, everything it has, with a fresh id for the slide and
/// each object. Every slide copied (duplicated, pasted) is made here.
[[nodiscard]] Slide clone_slide(Document& doc, const Slide& src);

/// Clamp every entry of ``map`` into the deck's slides and make the map
/// non-decreasing (a later cue never sits on an earlier slide).
void make_non_decreasing(std::vector<int>& map, int slide_count);

/// Size ``deck``'s cue-to-slide map to ``cue_count`` entries (new cues land
/// on the last slide) and make it valid.
void pad_map(Deck& deck, int cue_count);

} // namespace seventhslide::freeform
