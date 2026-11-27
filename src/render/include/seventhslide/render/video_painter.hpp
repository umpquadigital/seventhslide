#pragma once

// Video-mode area + countdown painter.
// Fit is CSS contain. Distinct from image mode.
// Seam: Qt Gui.

#include "seventhslide/render/background_painter.hpp"
#include "seventhslide/render/source_frames.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <QImage>
#include <QPainter>
#include <QRectF>

namespace seventhslide::render {

/// Resolve video area rect in canvas pixels (see SceneMediaArea::on_canvas).
[[nodiscard]] QRectF video_area_rect(const scene::SceneFrame& frame);

/**
 * Paint the video layer: a black plate with the frame's own video's latest
 * picture (``SceneVideoMedia::source`` in ``frames``) fitted (contain)
 * inside the video area.
 *
 * Skips when not Video mode or blank, and when the theme places no video area
 * (``video_enabled`` false). With no picture yet (null ``frames``, or none
 * for the source), the black plate only, pending decode. The countdown is a
 * clock element and paints with the frame's objects, not here.
 */
void paint_video_media(QPainter& painter,
                       const scene::SceneFrame& frame,
                       const SourceFrames* frames = nullptr);

} // namespace seventhslide::render
