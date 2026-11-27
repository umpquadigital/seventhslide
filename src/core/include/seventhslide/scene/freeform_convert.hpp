#pragma once

// Snapshot a live song/Bible build into a Freeform document.
// One-way: the original song/Bible is untouched. Seam: plain C++20.
//
// It lives beside the scene builder because it bakes what the scene paints:
// each object is a frame's text box, rows and chrome, read through the same
// builder calls the audience frame uses.

#include "seventhslide/build/slide_builder.hpp"
#include "seventhslide/freeform/document.hpp"
#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/model/output_config.hpp"

#include <map>
#include <string>

namespace seventhslide::scene {

struct FreeformConvertOptions {
    std::string name;
    /// Copyright overlay tokens for chrome snapshots (theme template applied
    /// per output). Empty skips copyright objects.
    std::map<std::string, std::string> copyright_vars;
    /// The song's "Display copyright on output" toggle. Snapshotting bakes the
    /// notice into objects, so it is answered here: a Freeform document has no
    /// live toggle of its own. See ``theme::ShowWhen``.
    bool song_copyright = false;
    /// Bible reference overlay tokens (theme template applied per output).
    std::map<std::string, std::string> bible_ref_vars;
};

/// Bake ``built`` slides (already wrapped) into objects. Always line-control,
/// title overlays, and on each slide its own copy of the background it was
/// built with (freeform::background_copy_of): a song's theme chips may change
/// it from one slide to the next. A picture or video a theme owns is named by
/// its ``/static/theme_backgrounds/...`` URL, which the caller copies into the
/// library before saving.
[[nodiscard]] freeform::Document document_from_build(
    const build::SlideBuildResult& built,
    const std::vector<model::OutputConfig>& outputs,
    layout::ITextMeasurer& measurer,
    const FreeformConvertOptions& opts);

}  // namespace seventhslide::scene
