#pragma once

// Scene text measured once, when a frame is built: the lyrics / passage box
// lines (width, baseline, where each run or chord sits) and every overlay box
// (wrapped and placed, see layout::lay_out_overlay). Paint draws from these
// and measures nothing for a frame that has them.
//
// Plain C++20 (no Qt). Measure via ITextMeasurer.

#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/scene/scene_frame.hpp"

namespace seventhslide::scene {

/// ``line``'s metrics in ``box``: the font is the box's, at the line's own
/// size when it has one. A chord line is its lyric's line box with the chord
/// row (``layout::chord_row_px``, in ``chords``' style) on top.
[[nodiscard]] SceneLineMetrics measure_text_line(const SceneTextLine& line,
                                                 const SceneTextBoxStyle& box,
                                                 const layout::ChordStyle& chords,
                                                 layout::ITextMeasurer& measurer);

/// Fill every line's ``metrics``.
void measure_text_content(SceneTextContent& content, layout::ITextMeasurer& measurer);

/// Measure ``frame``'s text box and lay out its overlay, its media stand-in
/// and its text objects. The frames of the stacks below are laid out as each
/// is built, before they are shared.
void lay_out_frame_text(SceneFrame& frame, layout::ITextMeasurer& measurer);

}  // namespace seventhslide::scene
