#pragma once

// What a background theme gives a Freeform slide when it is copied in: its
// colour, picture or video and its fade, written into the
// slide's own fields once. Nothing links the slide to the theme afterwards,
// so a theme that changes or is deleted leaves the slide as it was.
//
// A slide names only its document's own files (freeform/own_media.hpp), so
// the picture or video a theme shows, its own or a library file, is copied
// into the document's before the slide can point at it: ``copy_from_url``
// says which. The copy itself is the app's (off the GUI thread). Seam: plain
// C++20 + nlohmann/json.

#include "seventhslide/freeform/document.hpp"
#include "seventhslide/layout/background_fill.hpp"
#include "seventhslide/model/transition.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace seventhslide::freeform {

struct BackgroundCopy {
    /// The slide's background. For a picture or video still to be copied,
    /// the kind is set and the filename left empty until the copy is in.
    SlideBackground background;
    /// The theme's fade, or a cut when it has none: always set, so the slide
    /// changes as the theme did.
    model::Transition transition;
    /// The file the theme shows (``/static/theme_backgrounds/...``, or a
    /// library picture or video by its ``/static/`` URL) that the document
    /// needs a copy of first; empty when there is none.
    std::string copy_from_url;

    [[nodiscard]] bool waits_for_copy() const noexcept { return !copy_from_url.empty(); }
};

/// What a background showing ``fill`` and changing by ``transition`` gives a
/// slide.
[[nodiscard]] BackgroundCopy background_copy_of(const layout::BackgroundFill& fill,
                                                model::Transition transition);

/// What background theme ``bg_style`` gives a slide.
[[nodiscard]] BackgroundCopy background_copy_of(const nlohmann::json& bg_style);

/// ``copy`` with its picture or video now the document's own, by its URL.
void place_copied_file(BackgroundCopy& copy, std::string filename);

}  // namespace seventhslide::freeform
