#pragma once

// Image-mode content painter - themed area + object-fit.
// Distinct from background images (6.6). Seam: Qt Gui.

#include "seventhslide/render/background_painter.hpp"
#include "seventhslide/render/paint_context.hpp"
#include "seventhslide/scene/paint_order.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <QPainter>
#include <QRectF>
#include <QSizeF>

#include <optional>

namespace seventhslide::render {

/**
 * Paint ``frame.image`` into the themed image area when mode is Image.
 *
 * Geometry from ``frame.image->area`` (width / height <= 0 -> canvas).
 * Fit from ``image_fit`` (contain/cover/fill). Skips when blank, disabled
 * (``image_enabled`` false), missing URL, or load failure.
 */
void paint_image_media(QPainter& painter,
                       const scene::SceneFrame& frame,
                       const PaintContext& ctx = {});

/// Paint the whole object stack (design px), back to front in the frame's
/// ``scene::paint_order``. Text, image, shape and clock objects all paint
/// here - a theme's elements and a Freeform deck's alike.
/// ``ctx.now`` and ``ctx.video_clock`` only reach clock objects.
/// Each object is one element of ``ctx.rasters``. As the frame is once its
/// builds have played (an object going by an exit is gone), or with
/// ``builds_at_ms``, each object with builds as they have it that far into
/// the change into the frame, in its place in the stack.
void paint_scene_objects(QPainter& painter,
                         const scene::SceneFrame& frame,
                         const PaintContext& ctx = {},
                         std::optional<double> builds_at_ms = std::nullopt);

/// Same stack, cross-fading per-object during a content transition, in the
/// change's ``scene::paint_order`` with objects shared by id.
void paint_objects_transitioning(QPainter& painter,
                                 const scene::SceneFrame& outgoing,
                                 const scene::SceneFrame& incoming,
                                 double progress,
                                 const model::Transition& fallback,
                                 const PaintContext& ctx = {},
                                 double elapsed_ms = 0.0);

/// One object of a frame on a ``canvas`` big, as ``paint_scene_objects``
/// paints it. ``ctx.video_clock`` is its frame's (``video_clock_of``).
void paint_scene_object(QPainter& painter,
                        const scene::SceneObject& obj,
                        const QSizeF& canvas,
                        const PaintContext& ctx,
                        std::optional<double> builds_at_ms = std::nullopt);

/// The object ``step`` of a change from ``outgoing`` to ``incoming``
/// (``scene::paint_order`` with objects shared by id) through it, clipped to
/// the canvas. An object both have changes by the incoming one's transition,
/// one leaving by its own or else ``fallback``; one that builds plays its
/// build.
void paint_object_step(QPainter& painter,
                       const scene::PaintStep& step,
                       const scene::SceneFrame& outgoing,
                       const PaintContext& out_ctx,
                       const scene::SceneFrame& incoming,
                       const PaintContext& in_ctx,
                       double progress,
                       const model::Transition& fallback,
                       double elapsed_ms);

[[nodiscard]] bool scene_objects_have_ids(const scene::SceneFrame& frame);

/// Resolve image area rect in canvas pixels (see SceneMediaArea::on_canvas).
[[nodiscard]] QRectF image_area_rect(const scene::SceneFrame& frame);

} // namespace seventhslide::render
