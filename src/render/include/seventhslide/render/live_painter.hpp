#pragma once

// Live-source content painter: the source's latest picture, contained in the
// theme's Video area over a black plate, as a video is. Seam: Qt Gui.

#include "seventhslide/render/source_frames.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <QPainter>
#include <QRectF>

namespace seventhslide::render {

/// The live source's area in canvas pixels (see SceneMediaArea::on_canvas).
[[nodiscard]] QRectF live_area_rect(const scene::SceneFrame& frame);

/// Paint ``frame.live`` when the frame is a live source. Skips when blank or
/// when the theme places no Video area; a source with no picture yet (not
/// found, or no receiver) paints the plate alone.
void paint_live_media(QPainter& painter, const scene::SceneFrame& frame,
                      const SourceFrames* frames);

} // namespace seventhslide::render
