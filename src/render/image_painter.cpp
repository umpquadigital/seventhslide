#include <seventhslide/render/image_painter.hpp>


#include <seventhslide/render/effects_painter.hpp>
#include <seventhslide/render/element_raster_cache.hpp>
#include <seventhslide/render/object_fit.hpp>
#include <seventhslide/render/picture_painter.hpp>
#include <seventhslide/render/shape_painter.hpp>
#include <seventhslide/render/table_painter.hpp>
// The Freeform stack paints text and clock objects in the same z-ordered pass
// as shapes and images, so this TU reaches into the text painter for those two.
#include <seventhslide/render/text_painter.hpp>

#include <seventhslide/model/static_media.hpp>
#include <seventhslide/scene/paint_order.hpp>

#include <QBrush>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QString>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::render {

QRectF image_area_rect(const scene::SceneFrame& frame)
{
    const auto area = (frame.image ? frame.image->area : scene::SceneMediaArea{})
                          .on_canvas(frame.canvas_width, frame.canvas_height);
    return QRectF(area.x, area.y, area.width, area.height);
}

void paint_image_media(QPainter& painter,
                       const scene::SceneFrame& frame,
                       const PaintContext& ctx)
{
    if (frame.mode != scene::SceneMode::Image || frame.is_blank) {
        return;
    }
    if (!frame.image || frame.image->url.empty() || !frame.image->area.enabled) {
        return;
    }

    const QRectF area = image_area_rect(frame);
    if (!area.isValid()) {
        return;
    }

    QImage img;
    if (ctx.cache) {
        img = ctx.cache->get(frame.image->url);
    }
    if (img.isNull()) {
        return;
    }

    const ObjectFitPlacement place =
        object_fit(frame.image->fit, img.width(), img.height(), area.x(), area.y(), area.width(),
                   area.height());
    if (place.empty()) {
        return;
    }

    ElementPlacement placement;
    placement.bounds = area;
    draw_element(painter, ctx.rasters, *frame.image, ElementInputs{nullptr, img.cacheKey(), area},
                 placement, [&](QPainter& p) {
                     p.save();
                     p.setClipRect(area, Qt::IntersectClip);
                     // Black plate under contain/cover letterboxing.
                     p.fillRect(area, QColor(Qt::black));
                     p.drawImage(QRectF(place.dst_x, place.dst_y, place.dst_w, place.dst_h), img,
                                 QRectF(place.src_x, place.src_y, place.src_w, place.src_h));
                     p.restore();
                 });
}

namespace {

/// Move ``target`` (a QPainter or a QTransform) into an object's own
/// coordinates: its box at the origin, rotated and flipped about its centre.
/// The painter and the raster bounds both go through here, so they cannot
/// disagree about where an object is.
template <class Target>
void to_object_space(Target& target, const canvas::BoxFrame& box)
{
    target.translate(box.x + box.w * 0.5, box.y + box.h * 0.5);
    if (box.rotation != 0.0) {
        target.rotate(box.rotation);
    }
    if (box.flip_h || box.flip_v) {
        target.scale(box.flip_h ? -1.0 : 1.0, box.flip_v ? -1.0 : 1.0);
    }
    target.translate(-box.w * 0.5, -box.h * 0.5);
}

void apply_object_transform(QPainter& painter, const canvas::BoxFrame& box)
{
    painter.setOpacity(painter.opacity() * std::clamp(box.opacity, 0.0, 1.0));
    to_object_space(painter, box);
}

/// ``local`` (in the object's own coordinates) as the painter places it.
[[nodiscard]] QRectF object_bounds(const canvas::BoxFrame& box, const QRectF& local)
{
    QTransform t;
    to_object_space(t, box);
    // A pixel of slack: the bounds only have to cover the element.
    return t.mapRect(local).adjusted(-1, -1, 1, 1);
}

/// The picture a fill paints with, decoded, or null: nothing until the file
/// is in the cache.
[[nodiscard]] QImage fill_picture(const canvas::Fill& fill, const PaintContext& ctx)
{
    const auto url = canvas::fill_picture_url(fill);
    if (!url || ctx.cache == nullptr) {
        return {};
    }
    return ctx.cache->get(*url);
}

void paint_shape_object(QPainter& painter,
                        const canvas::Object& sh,
                        const PaintContext& ctx)
{
    if (sh.w <= 0 || sh.h <= 0 || sh.opacity <= 0) {
        return;
    }
    const QImage picture = fill_picture(sh.fill, ctx);
    // A mitred stroke or a line's end reaches past the outline; the effects
    // reach further.
    const double stroke_reach = shape_reach(sh.shape_kind, sh.shape_adjust, sh.line);
    const double reach = stroke_reach + canvas::effects_reach(sh.effects);
    ElementPlacement placement;
    placement.bounds =
        object_bounds(sh, QRectF(-reach, -reach, sh.w + 2 * reach, sh.h + 2 * reach));
    draw_element(painter, ctx.rasters, sh, ElementInputs{nullptr, picture.cacheKey(), {}},
                 placement, [&](QPainter& p) {
        p.save();
        apply_object_transform(p, sh);
        const QRectF body(-stroke_reach, -stroke_reach, sh.w + 2 * stroke_reach,
                          sh.h + 2 * stroke_reach);
        paint_with_effects(p, sh.effects, body, [&](QPainter& q) {
            paint_shape_outline(q, sh.shape_kind, sh.w, sh.h, sh.shape_adjust, sh.fill, sh.line,
                                1.0, &picture);
        });
        p.restore();
    });
}

/// The fill and line of a text-like box (a text box, an indicator, a clock),
/// behind its words, turned as its words are; words are never mirrored, so
/// the frame is not either. One element of the cache of its own: the words
/// are another (an OverlayBox), so the two never share a raster.
void paint_box_frame(QPainter& painter, const canvas::Object& box, const PaintContext& ctx)
{
    if (box.w <= 0 || box.h <= 0 || box.opacity <= 0 || !canvas::paints_frame(box)) {
        return;
    }
    const double reach = line_reach(box.line) + canvas::effects_reach(box.effects);
    const QImage picture = fill_picture(box.fill, ctx);
    canvas::BoxFrame frame = box;
    frame.flip_h = false;
    frame.flip_v = false;
    ElementPlacement placement;
    placement.bounds = object_bounds(frame, QRectF(-reach, -reach, box.w + 2 * reach, box.h + 2 * reach));
    draw_element(painter, ctx.rasters, box, ElementInputs{nullptr, picture.cacheKey(), {}},
                 placement, [&](QPainter& p) {
        p.save();
        apply_object_transform(p, frame);
        paint_rect_frame(p, QRectF(0, 0, box.w, box.h), box.fill, box.line, box.effects, &picture);
        p.restore();
    });
}

void paint_image_object(QPainter& painter,
                        const canvas::Object& img,
                        const PaintContext& ctx)
{
    const auto url = canvas::object_media_url(img);
    if (img.w <= 0 || img.h <= 0 || img.opacity <= 0 || !url || !ctx.cache) {
        return;
    }
    const QImage pix = ctx.cache->get(*url);
    if (pix.isNull()) {
        return;
    }
    const QRectF local(0, 0, img.w, img.h);
    const double reach = canvas::effects_reach(img.effects);
    ElementPlacement placement;
    placement.bounds = object_bounds(img, local.adjusted(-reach, -reach, reach, reach));
    draw_element(painter, ctx.rasters, img, ElementInputs{nullptr, pix.cacheKey(), {}}, placement,
                 [&](QPainter& p) {
                     p.save();
                     apply_object_transform(p, img);
                     // The effects follow the picture's own pixels (a
                     // contained picture's shadow is the picture's, not the
                     // box's), so the clip is inside the body.
                     paint_with_effects(p, img.effects, local, [&](QPainter& q) {
                         paint_picture(q, pix, local, img.object_fit, img.picture.crop,
                                       img.picture);
                     });
                     p.restore();
                 });
}

/// A table in its box, kept as one raster (its cells' words go into it).
/// Turned but never mirrored: its words read as words, as a text box's do.
void paint_table(QPainter& painter, const canvas::Object& table, const PaintContext& ctx)
{
    if (table.w <= 0 || table.h <= 0 || table.opacity <= 0) {
        return;
    }
    canvas::BoxFrame frame = table;
    frame.flip_h = false;
    frame.flip_v = false;
    const double reach = std::max(0.0, table.table.border_width);
    ElementPlacement placement;
    placement.bounds = object_bounds(frame, QRectF(-reach, -reach, table.w + 2 * reach, table.h + 2 * reach));
    PaintContext cells = ctx;
    cells.rasters = nullptr;
    draw_element(painter, ctx.rasters, table, ElementInputs{ctx.faces}, placement, [&](QPainter& p) {
        p.save();
        apply_object_transform(p, frame);
        paint_table_object(p, table, cells);
        p.restore();
    });
}

/// A video object (4.7's 6): its player's latest picture, or until it plays
/// its poster, in its box by its fit; a black plate before either has come.
/// Drawn live: its picture moves every frame.
void paint_video_object(QPainter& painter, const canvas::Object& video, bool plays,
                        const PaintContext& ctx)
{
    const auto url = canvas::object_media_url(video);
    if (video.w <= 0 || video.h <= 0 || video.opacity <= 0 || !url) {
        return;
    }
    QImage pix;
    if (const QImage* playing = source_image(ctx.source_frames, scene::video_object_key(*url))) {
        pix = *playing;
    }
    if ((!plays || pix.isNull()) && ctx.cache != nullptr) {
        if (const auto poster_url = model::media_ref_url(model::k_images_subdir, video.video.poster)) {
            if (const QImage poster = ctx.cache->get(*poster_url); !poster.isNull()) {
                pix = poster;
            }
        }
    }
    const QRectF local(0, 0, video.w, video.h);
    painter.save();
    apply_object_transform(painter, video);
    paint_with_effects(painter, video.effects, local, [&](QPainter& q) {
        if (pix.isNull()) {
            q.fillRect(local, Qt::black);
        } else {
            paint_picture(q, pix, local, video.object_fit, {});
        }
    });
    painter.restore();
}

[[nodiscard]] bool object_visual_equal(const scene::SceneObject& a, const scene::SceneObject& b)
{
    auto x = a;
    auto y = b;
    x.id.clear();
    y.id.clear();
    x.transition = {};
    y.transition = {};
    return x == y;
}

void paint_one_scene_object(QPainter& painter,
                            const scene::SceneObject& obj,
                            const PaintContext& ctx)
{
    if (obj.object) {
        switch (obj.object->type) {
        case canvas::ObjectType::Shape:
            paint_shape_object(painter, *obj.object, ctx);
            break;
        case canvas::ObjectType::Image:
            paint_image_object(painter, *obj.object, ctx);
            break;
        case canvas::ObjectType::Clock:
            paint_box_frame(painter, *obj.object, ctx);
            paint_clock_object(painter, *obj.object, ctx);
            break;
        case canvas::ObjectType::Text:
        case canvas::ObjectType::Indicator:
            // The words are ``text``; the object is here for the frame behind
            // them (the scene carries it when there is one).
            paint_box_frame(painter, *obj.object, ctx);
            break;
        case canvas::ObjectType::Video:
            paint_video_object(painter, *obj.object, obj.plays, ctx);
            break;
        case canvas::ObjectType::Table:
            paint_table(painter, *obj.object, ctx);
            break;
        }
    }
    if (obj.text) {
        paint_overlay_box(painter, *obj.text, ctx);
    }
}

} // namespace

bool scene_objects_have_ids(const scene::SceneFrame& frame)
{
    for (const auto& obj : frame.objects) {
        if (!obj.id.empty()) {
            return true;
        }
    }
    return false;
}

namespace {

/// ``obj``'s rectangle on the canvas.
[[nodiscard]] QRectF object_rect(const scene::SceneObject& obj)
{
    if (obj.object) {
        return QRectF(obj.object->x, obj.object->y, obj.object->w, obj.object->h);
    }
    if (obj.text) {
        return QRectF(obj.text->x, obj.text->y, obj.text->w, obj.text->h);
    }
    return {};
}

/// Which of ``obj``'s builds has it ``elapsed_ms`` into the change: the
/// last to have started, or before any has, the first.
const scene::SceneBuild& build_at(const scene::SceneObject& obj, double elapsed_ms)
{
    const auto* at = &obj.builds.front();
    for (const auto& build : obj.builds) {
        if (elapsed_ms >= build.start_ms) {
            at = &build;
        }
    }
    return *at;
}

/// ``obj`` as its builds have it ``elapsed_ms`` into the change, on a canvas
/// ``canvas`` big.
void paint_one_build(QPainter& painter,
                     const scene::SceneObject& obj,
                     const QSizeF& canvas,
                     double elapsed_ms,
                     const PaintContext& ctx)
{
    using canvas::AnimationEffect;
    using model::TransitionDirection;
    const auto& build = build_at(obj, elapsed_ms);
    // Where in its own run it is: 0 before it starts, 1 once it is done.
    const double raw = build.duration_ms > 0
                           ? (elapsed_ms - build.start_ms) / build.duration_ms
                           : (elapsed_ms >= build.start_ms ? 1.0 : 0.0);
    const double p = model::eased(model::TransitionEasing::EaseInOut, std::clamp(raw, 0.0, 1.0));
    const QRectF r = object_rect(obj);
    const QPointF centre = r.center();
    const bool exit = build.kind() == canvas::AnimationKind::Exit;
    // How far in it is (1: there), and the motion that goes with it.
    const double in = exit ? 1.0 - p : p;
    if (in <= 0.0 && build.kind() != canvas::AnimationKind::Emphasis) {
        return;  // not on yet, or gone
    }
    double opacity = 1.0;
    QTransform move;
    std::optional<QRectF> clip;
    const auto off = [&](TransitionDirection from, double away) {
        // ``away`` of the way to just off the canvas on ``from``'s side.
        switch (from) {
        case TransitionDirection::Left:
            return QTransform::fromTranslate(-(r.right()) * away, 0);
        case TransitionDirection::Right:
            return QTransform::fromTranslate((canvas.width() - r.left()) * away, 0);
        case TransitionDirection::Up:
            return QTransform::fromTranslate(0, -(r.bottom()) * away);
        case TransitionDirection::Down:
            break;
        }
        return QTransform::fromTranslate(0, (canvas.height() - r.top()) * away);
    };
    const auto scaled = [&](double s) {
        return QTransform::fromTranslate(-centre.x(), -centre.y()) * QTransform::fromScale(s, s)
               * QTransform::fromTranslate(centre.x(), centre.y());
    };
    switch (build.effect) {
    case AnimationEffect::Appear:
    case AnimationEffect::Disappear:
        if (exit ? elapsed_ms >= build.start_ms : elapsed_ms < build.start_ms) {
            return;
        }
        break;
    case AnimationEffect::Fade:
    case AnimationEffect::FadeOut:
        opacity = in;
        break;
    case AnimationEffect::FlyIn:
    case AnimationEffect::FlyOut:
        move = off(build.direction, 1.0 - in);
        break;
    case AnimationEffect::Wipe:
    case AnimationEffect::WipeOut: {
        QRectF shown = r;
        switch (build.direction) {
        case TransitionDirection::Left:
            shown.setRight(r.left() + r.width() * in);
            break;
        case TransitionDirection::Right:
            shown.setLeft(r.right() - r.width() * in);
            break;
        case TransitionDirection::Up:
            shown.setBottom(r.top() + r.height() * in);
            break;
        case TransitionDirection::Down:
            shown.setTop(r.bottom() - r.height() * in);
            break;
        }
        clip = shown;
        break;
    }
    case AnimationEffect::Zoom:
    case AnimationEffect::ZoomOut:
        move = scaled(std::max(0.01, in));
        opacity = in;
        break;
    case AnimationEffect::Float:
    case AnimationEffect::FloatOut:
        // Up into place, and on up out of it.
        move = QTransform::fromTranslate(0, (exit ? -p : 1.0 - p) * canvas.height() * 0.08);
        opacity = in;
        break;
    case AnimationEffect::Pulse:
        move = scaled(1.0 + 0.08 * std::sin(std::numbers::pi * p));
        break;
    case AnimationEffect::Grow:
        move = scaled(1.0 + 0.2 * std::sin(std::numbers::pi * p));
        break;
    }
    if (opacity <= 0.0) {
        return;
    }
    painter.save();
    if (clip) {
        painter.setClipRect(*clip, Qt::IntersectClip);
    }
    painter.setTransform(move, true);
    painter.setOpacity(painter.opacity() * opacity);
    if (move.isIdentity()) {
        paint_one_scene_object(painter, obj, ctx);
    } else {
        // Moving, it is somewhere new each frame: a raster of it would be
        // used once and crowd out the ones that are kept.
        PaintContext live = ctx;
        live.rasters = nullptr;
        paint_one_scene_object(painter, obj, live);
    }
    painter.restore();
}

/// One object through a change ``progress`` of the way along: ``out`` as
/// the outgoing frame has it, ``in`` as the incoming does, either null where
/// that frame has none. Each side's context carries its own video clock. An
/// object both have changes by the incoming one's transition, one leaving by
/// its own or else ``fallback``; one that builds plays its build.
void paint_object_change(QPainter& painter,
                         const scene::SceneObject* out,
                         const PaintContext& out_ctx,
                         const scene::SceneObject* in,
                         const PaintContext& in_ctx,
                         const QSizeF& canvas,
                         double progress,
                         const model::Transition& fallback,
                         double elapsed_ms)
{
    // One that went by an exit is already gone.
    if (out != nullptr && out->exits()) {
        out = nullptr;
    }
    // An object that builds in this change plays its build, whatever the
    // slide's transition does.
    if (in != nullptr && !in->builds.empty()) {
        paint_one_build(painter, *in, canvas, elapsed_ms, in_ctx);
        return;
    }
    // The incoming slide decides: an object it has changes by its own; one
    // leaving by its own fade, else by the slide's.
    const model::Transition spec =
        in ? in->transition : (out && out->transition.plays() ? out->transition : fallback);
    if (!spec.plays()) {
        if (in) {
            paint_one_scene_object(painter, *in, in_ctx);
        }
        return;
    }
    if (out && in && object_visual_equal(*out, *in)) {
        paint_one_scene_object(painter, *in, in_ctx);
        return;
    }
    // An object fades where it is; a slide that moves carries its objects
    // with it (the compositor's).
    const auto blend = model::blend_at(
        model::transition_moves(spec.type) ? model::TransitionType::Fade : spec.type, progress);
    if (out && blend.outgoing > 0.0) {
        painter.save();
        painter.setOpacity(painter.opacity() * blend.outgoing);
        paint_one_scene_object(painter, *out, out_ctx);
        painter.restore();
    }
    if (in) {
        painter.save();
        painter.setOpacity(painter.opacity() * blend.incoming);
        paint_one_scene_object(painter, *in, in_ctx);
        painter.restore();
    }
}

}  // namespace

void paint_scene_object(QPainter& painter,
                        const scene::SceneObject& obj,
                        const QSizeF& canvas,
                        const PaintContext& ctx,
                        std::optional<double> builds_at_ms)
{
    if (builds_at_ms && !obj.builds.empty()) {
        paint_one_build(painter, obj, canvas, *builds_at_ms, ctx);
        return;
    }
    // Once built: an exit's object is gone.
    if (obj.exits()) {
        return;
    }
    paint_one_scene_object(painter, obj, ctx);
}

void paint_object_step(QPainter& painter,
                       const scene::PaintStep& step,
                       const scene::SceneFrame& outgoing,
                       const PaintContext& out_ctx,
                       const scene::SceneFrame& incoming,
                       const PaintContext& in_ctx,
                       double progress,
                       const model::Transition& fallback,
                       double elapsed_ms)
{
    const auto* out = step.side == scene::PaintSide::Incoming
                          ? nullptr
                          : &outgoing.objects[step.side == scene::PaintSide::Both
                                                  ? step.outgoing_index
                                                  : step.index];
    const auto* in =
        step.side == scene::PaintSide::Outgoing ? nullptr : &incoming.objects[step.index];
    const QRectF canvas(0, 0, incoming.canvas_width, incoming.canvas_height);
    painter.save();
    painter.setClipRect(canvas, Qt::IntersectClip);
    paint_object_change(painter, out, out_ctx, in, in_ctx, canvas.size(), progress, fallback,
                        elapsed_ms);
    painter.restore();
}

void paint_objects_transitioning(QPainter& painter,
                                 const scene::SceneFrame& outgoing,
                                 const scene::SceneFrame& incoming,
                                 double progress,
                                 const model::Transition& fallback,
                                 const PaintContext& ctx,
                                 double elapsed_ms)
{
    // A countdown counts down the video of its own side.
    PaintContext out_ctx = ctx;
    out_ctx.video_clock = video_clock_of(outgoing, ctx.source_frames);
    PaintContext in_ctx = ctx;
    in_ctx.video_clock = video_clock_of(incoming, ctx.source_frames);
    for (const auto& step :
         scene::paint_order(outgoing, incoming, {.match_objects = true, .same_media = false})) {
        if (step.part == scene::PaintPart::Object) {
            paint_object_step(painter, step, outgoing, out_ctx, incoming, in_ctx, progress,
                              fallback, elapsed_ms);
        }
    }
}

void paint_scene_objects(QPainter& painter,
                         const scene::SceneFrame& frame,
                         const PaintContext& ctx,
                         std::optional<double> builds_at_ms)
{
    if (frame.is_blank || frame.objects.empty()) {
        return;
    }
    // A countdown counts down the video it shows with: this frame's own.
    PaintContext own = ctx;
    own.video_clock = video_clock_of(frame, ctx.source_frames);
    const QRectF canvas(0, 0, frame.canvas_width, frame.canvas_height);
    painter.save();
    painter.setClipRect(canvas, Qt::IntersectClip);
    for (const auto& step : scene::paint_order(frame)) {
        if (step.part == scene::PaintPart::Object) {
            paint_scene_object(painter, frame.objects[step.index], canvas.size(), own,
                               builds_at_ms);
        }
    }
    painter.restore();
}

} // namespace seventhslide::render
