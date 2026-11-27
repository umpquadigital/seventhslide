#pragma once

// Every still picture a piece of content shows, slide by slide, on each
// output: what an export saves as images.
//
// The content is loaded onto a stack of its own, outside any session, so it
// is laid out and themed exactly as it would be live (a rundown row under its
// service's themes, library content as it plays from the library) without
// touching what is live. Seam: plain C++20; the measurer may lock itself, so
// this runs on any thread given its own copies of the outputs and themes.

#include "seventhslide/freeform/document.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/scene/scene_frame.hpp"
#include "seventhslide/session/stack_runtime.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace seventhslide::session {

/// What to make stills of: a rundown row, or library content as it plays
/// from the library.
using StillSource =
    std::variant<model::ServiceItem, model::Song, model::BiblePassage, LiveFreeformContent>;

/// One output's stills of one piece of content, in order.
struct OutputStills {
    std::string output_id;
    std::vector<scene::SceneFrame> frames;
};

/**
 * ``source`` on every output in ``ctx.outputs``, in that order: a frame per
 * slide of a song, a Bible passage or a Freeform, a frame per image of an
 * image or an image folder.
 *
 * Content that shows no still picture of its own - video, audio, a web page,
 * a live source, a divider - has no frames on any output. A theme background
 * that is a video paints as it does before the video has a frame.
 */
[[nodiscard]] std::vector<OutputStills> still_frames(const StillSource& source,
                                                     const StackBuildContext& ctx);

/**
 * The first of ``source``'s stills on output ``output_id`` (one of
 * ``ctx.outputs``): its first slide, or its first image. Nullopt where
 * ``still_frames`` would have none. A thumbnail's: it lays out ``source`` as
 * ``still_frames`` does but makes only the one frame.
 */
[[nodiscard]] std::optional<scene::SceneFrame> first_still_frame(const StillSource& source,
                                                                 const StackBuildContext& ctx,
                                                                 std::string_view output_id);

/**
 * A rundown row as a Freeform document, as a service's package carries it.
 * A song or Bible row is laid out and themed as it plays from its service
 * (``ctx.service_themes`` and the row's own picks), on every output in
 * ``ctx.outputs``, and baked as Save as Freeform bakes a song
 * (``scene::document_from_build``): a picture or video a theme owns stays
 * named by its ``/static/theme_backgrounds/...`` URL. A Freeform row is the
 * document it holds. Nullopt for any other row, or one that does not load.
 */
[[nodiscard]] std::optional<freeform::Document> freeform_from_service_item(const model::ServiceItem& item,
                                                                         const StackBuildContext& ctx);

}  // namespace seventhslide::session
