#pragma once

// Laying out and painting one Freeform slide outside the live session: the
// Freeform editor's slide previews and the Freeform library's tiles.
//
// Seam: Qt Gui. Call on the GUI thread (see preview_resources.hpp).

#include "preview_resources.hpp"

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/render/scene_compositor.hpp>

#include <QImage>
#include <QSize>

#include <string>
#include <vector>

namespace seventhslide::app {

/// Lay ``doc`` out for ``outputs`` with the live session's ``theme_priority``.
/// Only those outputs' decks are laid out.
[[nodiscard]] build::SlideBuildResult build_freeform_slides(
    const freeform::Document& doc,
    const std::vector<model::OutputConfig>& outputs,
    const std::vector<std::string>& theme_priority,
    PreviewResources& previews);

/// Slide ``slide_index`` of ``rt``, whose output's canvas is ``canvas``, at
/// ``size`` over black, ready to paint (on any thread).
[[nodiscard]] render::StillPaint freeform_slide_still(const build::OutputRuntime& rt,
                                                      geom::CanvasSize canvas,
                                                      int slide_index,
                                                      QSize size,
                                                      PreviewResources& previews);

}  // namespace seventhslide::app
