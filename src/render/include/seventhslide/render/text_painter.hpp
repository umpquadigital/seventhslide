#pragma once

// Text / overlay painter + frame image helper (6.5/6.6).
// Runs with align / valign / clip / opacity; overlay boxes; basic chord cells.
// Seam: Qt Gui (QPainter). Call in design-canvas coordinates (after canvas fit).
//
// Every text block (a text box, an overlay box, a clock) is one element: with
// ``ctx.rasters`` it is drawn once and copied while it is unchanged (see
// element_raster_cache.hpp).

#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/render/paint_context.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <QPainter>

namespace seventhslide::render {

/**
 * Paint primary lyrics / bible ``text`` into ``painter`` (design px).
 *
 * Each line is drawn where its ``metrics`` put it (a built frame measured it
 * once, see scene::lay_out_frame_text); a line without them is measured here
 * by the same function. ``ctx.faces`` selects the resolved family's real
 * weight/slant (or Qt synthesis when no styled file exists). Do not
 * double-draw to fake bold.
 */
void paint_text_content(QPainter& painter,
                        const scene::SceneTextContent& text,
                        const PaintContext& ctx = {});

/// One overlay box: a theme text element, a title slide's box, a Freeform
/// text object, a clock. Painted from its ``layout``; a box without one (a
/// clock, a hand-built box) is laid out here by layout::lay_out_overlay, the
/// function scene build uses.
void paint_overlay_box(QPainter& painter,
                       const layout::OverlayBox& box,
                       const PaintContext& ctx = {});

/**
 * One clock object (design px). ``ctx.now`` invalid = current time.
 *
 * The time lays out inside the object's box like any other text, so align,
 * vertical align and a resize all behave. ``ctx.video_clock`` supplies the
 * remaining time for a Countdown clock; without one it paints a sample so the
 * theme designer has something to place.
 */
void paint_clock_object(QPainter& painter,
                        const canvas::Object& clock,
                        const PaintContext& ctx = {});

/// True when ``frame`` (or a stack under it) shows a clock object, whose text is
/// only known at paint time and so needs a repaint even when the frame is unchanged.
[[nodiscard]] bool scene_frame_has_clock(const scene::SceneFrame& frame);

}  // namespace seventhslide::render
