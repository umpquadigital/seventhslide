#pragma once

// Web-page content painter: the page's latest frame, fitted into its area.
// The page is drawn off screen by the app; this only places the pictures it
// hands over. Seam: Qt Gui.

#include "seventhslide/render/source_frames.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <QPainter>
#include <QRectF>

namespace seventhslide::render {

/// The web area in canvas pixels (see SceneMediaArea::on_canvas).
[[nodiscard]] QRectF web_area_rect(const scene::SceneFrame& frame);

/**
 * Paint ``frame.web`` when the frame is a web page: a black plate over its
 * area with the page's picture from ``frames`` fitted into it (``fit``).
 *
 * Skips when blank or when the theme places no web area. A page with no
 * picture yet (loading, or no renderer) paints the plate alone.
 */
void paint_web_media(QPainter& painter, const scene::SceneFrame& frame,
                     const SourceFrames* frames);

} // namespace seventhslide::render
