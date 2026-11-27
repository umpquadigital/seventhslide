#include <seventhslide/render/scene_compositor.hpp>

#include <seventhslide/model/transition.hpp>
#include <seventhslide/render/background_painter.hpp>
#include <seventhslide/render/element_raster_cache.hpp>
#include <seventhslide/render/image_painter.hpp>
#include <seventhslide/render/live_painter.hpp>
#include <seventhslide/render/source_frames.hpp>
#include <seventhslide/render/text_painter.hpp>
#include <seventhslide/render/video_painter.hpp>
#include <seventhslide/render/web_painter.hpp>
#include <seventhslide/scene/paint_order.hpp>

#include <QColor>
#include <QSizeF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace seventhslide::render {
namespace {

/// Background twice: across the whole viewport, then over the design canvas.
/// The viewport pass is what fills letterbox bars. It is skipped when an
/// exactly fitting opaque canvas fill would cover it anyway - but only at full
/// opacity, because mid-fade both passes contribute to the blend.
void paint_background_pass(QPainter& painter,
                           const scene::SceneBackground& bg,
                           const CanvasFit& fit,
                           const QRectF& viewport,
                           const QRectF& canvas,
                           const SceneResources& res,
                           const QImage* bg_video)
{
    if (painter.opacity() < 1.0 || !canvas_background_covers_viewport(bg, fit, bg_video)) {
        paint_background(painter, bg, viewport, res, bg_video);
    }
    painter.save();
    painter.translate(fit.offset_x, fit.offset_y);
    painter.scale(fit.scale, fit.scale);
    paint_background(painter, bg, canvas, res, bg_video);
    painter.restore();
}

/// Whether ``a`` and ``b`` show the same picture, video, page or source, so
/// it plays on through a change of their words rather than changing too. A
/// video is the same item, not only the same file: one sent again starts
/// over on a player of its own, and the one going plays out beside it.
[[nodiscard]] bool same_media(const scene::SceneFrame& a, const scene::SceneFrame& b)
{
    const auto same_video = [](const auto& x, const auto& y) {
        return x.has_value() == y.has_value()
               && (!x || (x->source == y->source && x->url == y->url));
    };
    const auto same_web = [](const auto& x, const auto& y) {
        return x.has_value() == y.has_value() && (!x || x->source == y->source);
    };
    return a.image == b.image && same_video(a.video, b.video) && same_web(a.web, b.web)
           && same_web(a.live, b.live);
}

/// The area ``frame``'s media paints in, in canvas coordinates.
[[nodiscard]] QRectF media_area_rect(const scene::SceneFrame& frame)
{
    switch (frame.mode) {
    case scene::SceneMode::Image:
        return image_area_rect(frame);
    case scene::SceneMode::Video:
        return video_area_rect(frame);
    case scene::SceneMode::Web:
        return web_area_rect(frame);
    case scene::SceneMode::Live:
        return live_area_rect(frame);
    case scene::SceneMode::Empty:
    case scene::SceneMode::Song:
    case scene::SceneMode::Bible:
    case scene::SceneMode::Freeform:
    case scene::SceneMode::Audio:
        break;
    }
    return {};
}

/// ``frame``'s picture, video, page and live source, in canvas coordinates:
/// each from its own source, whatever layer it is on. A design preview's
/// stand-in covers the area as the media will: the output's black plate, its
/// colour bars over that when it has them, and its label.
void paint_media(QPainter& painter, const scene::SceneFrame& frame, const PaintContext& ctx)
{
    if (frame.media_stand_in) {
        const QRectF area = media_area_rect(frame);
        painter.save();
        painter.setClipRect(area, Qt::IntersectClip);
        painter.fillRect(area, QColor(Qt::black));
        if (frame.media_stand_in->bars) {
            paint_colour_bars(painter, area);
        }
        paint_overlay_box(painter, frame.media_stand_in->label, ctx);
        painter.restore();
        return;
    }
    paint_image_media(painter, frame, ctx);
    paint_video_media(painter, frame, ctx.source_frames);
    paint_web_media(painter, frame, ctx.source_frames);
    paint_live_media(painter, frame, ctx.source_frames);
}

/// Run ``paint`` with ``painter`` in canvas coordinates: ``fit`` applied.
template <class Paint>
void in_canvas(QPainter& painter, const CanvasFit& fit, Paint&& paint)
{
    painter.save();
    painter.translate(fit.offset_x, fit.offset_y);
    painter.scale(fit.scale, fit.scale);
    paint();
    painter.restore();
}

/// One frame of a layer as its steps paint it: the frame, and a context whose
/// countdowns count down that frame's own video.
struct FrameSide {
    const scene::SceneFrame* frame = nullptr;
    PaintContext ctx;
};

[[nodiscard]] FrameSide side_of(const scene::SceneFrame& frame, const SceneResources& res)
{
    FrameSide side{.frame = &frame, .ctx = res};
    side.ctx.video_clock = video_clock_of(frame, res.source_frames);
    return side;
}

[[nodiscard]] QSizeF canvas_size(const scene::SceneFrame& frame)
{
    return {static_cast<double>(frame.canvas_width), static_cast<double>(frame.canvas_height)};
}

/// One step of ``side``'s frame as the frame has it, at the painter's
/// opacity, in canvas coordinates. All but the media are clipped to the
/// canvas; the media keeps to its area.
void paint_step(QPainter& painter,
                const FrameSide& side,
                const scene::PaintStep& step,
                std::optional<double> builds_at_ms)
{
    const scene::SceneFrame& frame = *side.frame;
    if (step.part == scene::PaintPart::Media) {
        paint_media(painter, frame, side.ctx);
        return;
    }
    painter.save();
    painter.setClipRect(QRectF(QPointF(0, 0), canvas_size(frame)), Qt::IntersectClip);
    switch (step.part) {
    case scene::PaintPart::Body:
        paint_text_content(painter, *frame.text, side.ctx);
        break;
    case scene::PaintPart::TitleBox:
        paint_overlay_box(painter, frame.overlay.boxes[step.index], side.ctx);
        break;
    case scene::PaintPart::Object:
        paint_scene_object(painter, frame.objects[step.index], canvas_size(frame), side.ctx,
                           builds_at_ms);
        break;
    case scene::PaintPart::Media:
        break;
    }
    painter.restore();
}

/// ``side``'s frame whole, back to front, at the painter's opacity, in canvas
/// coordinates: a settled layer, or one side of a change painted live.
void paint_frame(QPainter& painter, const FrameSide& side, std::optional<double> builds_at_ms)
{
    for (const auto& step : scene::paint_order(*side.frame)) {
        paint_step(painter, side, step, builds_at_ms);
    }
}

/// What a change paints, in order: a run, one side's flat steps drawn
/// together, or a step painted live on every frame of the change.
struct ChangeOp {
    /// A run's steps, all of one side, back to front. Empty for a live step.
    std::vector<scene::PaintStep> run;
    bool outgoing_run = false;
    scene::PaintStep live;
};

/**
 * ``order`` as a change paints it. A step ``is_live`` says must change on
 * every frame paints on its own. Between two such steps, each side's other
 * steps make one run: they hold still for the whole change, so a run is drawn
 * once into a picture and the two sides' pictures dissolve, the outgoing under
 * the incoming, as a slide does. With no live step between them they stay one
 * picture per side, whatever their z.
 */
template <class IsLive>
[[nodiscard]] std::vector<ChangeOp> plan_change(const std::vector<scene::PaintStep>& order,
                                                IsLive&& is_live)
{
    std::vector<ChangeOp> ops;
    ChangeOp outgoing{.outgoing_run = true};
    ChangeOp incoming;
    const auto flush = [&] {
        for (ChangeOp* run : {&outgoing, &incoming}) {
            if (!run->run.empty()) {
                ops.push_back(std::move(*run));
                *run = ChangeOp{.outgoing_run = run == &outgoing};
            }
        }
    };
    for (const auto& step : order) {
        if (is_live(step)) {
            flush();
            ops.push_back({.live = step});
        } else {
            (step.side == scene::PaintSide::Outgoing ? outgoing : incoming).run.push_back(step);
        }
    }
    flush();
    return ops;
}

/// A run to draw, and the frame its steps are of.
struct RunSource {
    const ChangeOp* op = nullptr;
    const FrameSide* side = nullptr;
};

/// What ``paint`` paints on the canvas, into a viewport-sized transparent
/// picture with the canvas fit applied: drawn at the viewport's corner, at
/// any strength, it blends as one.
template <typename Paint>
void render_picture(QImage& target, const CanvasFit& fit, const QRectF& viewport, Paint&& paint)
{
    const int w = std::max(1, static_cast<int>(std::ceil(viewport.width())));
    const int h = std::max(1, static_cast<int>(std::ceil(viewport.height())));
    if (target.width() != w || target.height() != h
        || target.format() != QImage::Format_ARGB32_Premultiplied) {
        target = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    }
    target.fill(Qt::transparent);

    QPainter painter(&target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    in_canvas(painter, fit, [&] { paint(painter); });
    painter.end();
}

/// ``steps`` of ``side``'s frame as a picture (``render_picture``), drawn
/// once so blending it later is a blit rather than a re-render.
void render_run(QImage& target,
                const FrameSide& side,
                const std::vector<scene::PaintStep>& steps,
                const CanvasFit& fit,
                const QRectF& viewport)
{
    render_picture(target, fit, viewport, [&](QPainter& painter) {
        for (const auto& step : steps) {
            paint_step(painter, side, step, std::nullopt);
        }
    });
}

/// Make ``layers`` hold pictures of the first ``k_fade_run_pictures`` of
/// ``runs``, in order, drawing them unless it already holds those runs at this
/// size. Returns how many runs have a picture; the rest are drawn anew each
/// frame (``draw_run``), as they would be with no scratch at all, so the
/// memory a change holds is bounded.
[[nodiscard]] std::size_t prepare_runs(SceneFadeLayers& layers,
                                       const std::vector<RunSource>& runs,
                                       const CanvasFit& fit,
                                       const QRectF& viewport)
{
    const std::size_t pictured = std::min(runs.size(), k_fade_run_pictures);
    const QSize want(static_cast<int>(std::ceil(viewport.width())),
                     static_cast<int>(std::ceil(viewport.height())));
    const auto holds = [&] {
        if (!layers.valid || layers.size != want || layers.held.size() != pictured) {
            return false;
        }
        for (std::size_t i = 0; i < pictured; ++i) {
            if (layers.held[i] != runs[i].op->run) {
                return false;
            }
        }
        return true;
    };
    if (holds()) {
        return pictured;
    }
    layers.runs.resize(pictured);
    layers.held.resize(pictured);
    for (std::size_t i = 0; i < pictured; ++i) {
        render_run(layers.runs[i], *runs[i].side, runs[i].op->run, fit, viewport);
        layers.held[i] = runs[i].op->run;
    }
    layers.size = want;
    layers.valid = true;
    return pictured;
}

/// ``op``'s run: its picture when it has one, else its steps painted live.
/// Several steps below full strength are pictured first even so: the run
/// blends as one, and a step over another still hides it rather than letting
/// it show through.
/// ``spare`` is where such a run is pictured (``SceneFadeLayers::spare``).
void draw_run(QPainter& painter,
              const ChangeOp& op,
              const FrameSide& side,
              const QImage* picture,
              QImage& spare,
              const CanvasFit& fit,
              const QRectF& viewport)
{
    if (picture != nullptr) {
        painter.drawImage(viewport.topLeft(), *picture);
        return;
    }
    if (op.run.size() > 1 && painter.opacity() < 1.0) {
        render_run(spare, side, op.run, fit, viewport);
        painter.drawImage(viewport.topLeft(), spare);
        return;
    }
    in_canvas(painter, fit, [&] {
        for (const auto& step : op.run) {
            paint_step(painter, side, step, std::nullopt);
        }
    });
}

/// Where each side of a moving transition is, in viewport coordinates.
struct SidePlace {
    QTransform transform;
    std::optional<QRectF> clip;
    double opacity = 1.0;
};
struct Motion {
    SidePlace outgoing;
    SidePlace incoming;
    /// Reveal: the incoming is still and under; the outgoing slides off it.
    bool incoming_under = false;
};

/// The two sides of a ``type`` transition ``progress`` of the way from
/// ``from``'s side, over ``viewport``.
[[nodiscard]] Motion motion_at(model::TransitionType type,
                               model::TransitionDirection from,
                               double progress,
                               const QRectF& viewport)
{
    using model::TransitionDirection;
    using model::TransitionType;
    const double p = std::clamp(progress, 0.0, 1.0);
    const double w = viewport.width();
    const double h = viewport.height();
    // The way the incoming picture travels in, as a unit step from its side.
    const double dx = from == TransitionDirection::Right ? 1.0 : from == TransitionDirection::Left ? -1.0 : 0.0;
    const double dy = from == TransitionDirection::Down ? 1.0 : from == TransitionDirection::Up ? -1.0 : 0.0;
    const auto shifted = [&](double fraction) {
        return QTransform::fromTranslate(dx * w * fraction, dy * h * fraction);
    };
    Motion m;
    switch (type) {
    case TransitionType::Push:
        m.outgoing.transform = shifted(-p);
        m.incoming.transform = shifted(1.0 - p);
        break;
    case TransitionType::Cover:
        m.incoming.transform = shifted(1.0 - p);
        break;
    case TransitionType::Reveal:
        m.outgoing.transform = shifted(-p);
        m.incoming_under = true;
        break;
    case TransitionType::Wipe: {
        QRectF clip = viewport;
        if (from == TransitionDirection::Right) {
            clip.setLeft(viewport.right() - w * p);
        } else if (from == TransitionDirection::Left) {
            clip.setRight(viewport.left() + w * p);
        } else if (from == TransitionDirection::Down) {
            clip.setTop(viewport.bottom() - h * p);
        } else {
            clip.setBottom(viewport.top() + h * p);
        }
        m.incoming.clip = clip;
        break;
    }
    case TransitionType::Split: {
        QRectF clip = viewport;
        const QPointF c = viewport.center();
        if (from == TransitionDirection::Left || from == TransitionDirection::Right) {
            clip.setLeft(c.x() - w * p / 2.0);
            clip.setRight(c.x() + w * p / 2.0);
        } else {
            clip.setTop(c.y() - h * p / 2.0);
            clip.setBottom(c.y() + h * p / 2.0);
        }
        m.incoming.clip = clip;
        break;
    }
    case TransitionType::Zoom: {
        const double scale = 0.5 + 0.5 * p;
        const QPointF c = viewport.center();
        m.incoming.transform = QTransform::fromTranslate(-c.x(), -c.y())
                               * QTransform::fromScale(scale, scale)
                               * QTransform::fromTranslate(c.x(), c.y());
        m.incoming.opacity = p;
        m.outgoing.opacity = 1.0 - p;
        break;
    }
    case TransitionType::Cut:
    case TransitionType::Fade:
    case TransitionType::FadeThroughBlack:
        break;
    }
    return m;
}

/// A moving transition: each side whole - its words, objects and media, in
/// their order - put where the motion has it. The objects move with their
/// slide, not by their own fades.
void paint_moving(QPainter& painter,
                  const LayerPresentation& layer,
                  const CanvasFit& fit,
                  const QRectF& viewport,
                  const SceneResources& res,
                  SceneFadeLayers* scratch)
{
    const scene::SceneFrame& current = *layer.current;
    const scene::SceneFrame& outgoing = *layer.outgoing;
    const FrameSide out = side_of(outgoing, res);
    const FrameSide in = side_of(current, res);
    const QImage* carried = layer.outgoing_picture;
    const bool carried_out = carried != nullptr && !carried->isNull();
    // A slide with builds draws its objects live, each as its builds have
    // it, in their places in the stack. The media is live on both sides.
    const bool builds = std::any_of(current.objects.begin(), current.objects.end(),
                                    [](const scene::SceneObject& o) { return !o.builds.empty(); });
    const auto out_ops =
        carried_out ? std::vector<ChangeOp>{}
                    : plan_change(scene::paint_order(outgoing), [](const scene::PaintStep& step) {
                          return step.part == scene::PaintPart::Media;
                      });
    const auto in_ops = plan_change(scene::paint_order(current), [&](const scene::PaintStep& step) {
        return step.part == scene::PaintPart::Media
               || (builds && step.part == scene::PaintPart::Object);
    });

    std::vector<RunSource> runs;
    for (const auto& [ops, side] : {std::pair{&out_ops, &out}, std::pair{&in_ops, &in}}) {
        for (const auto& op : *ops) {
            if (!op.run.empty()) {
                runs.push_back({.op = &op, .side = side});
            }
        }
    }
    SceneFadeLayers local;
    SceneFadeLayers& pictures = scratch != nullptr ? *scratch : local;
    const std::size_t pictured = prepare_runs(pictures, runs, fit, viewport);

    const Motion motion = motion_at(layer.content_type, current.content_transition.direction,
                                    layer.content_progress, viewport);
    // The runs are stored outgoing side first.
    const std::size_t outgoing_runs = static_cast<std::size_t>(std::count_if(
        out_ops.begin(), out_ops.end(), [](const ChangeOp& op) { return !op.run.empty(); }));
    const auto side = [&](bool incoming) {
        const SidePlace& place = incoming ? motion.incoming : motion.outgoing;
        painter.save();
        if (place.clip) {
            painter.setClipRect(*place.clip, Qt::IntersectClip);
        }
        painter.setTransform(place.transform, true);
        painter.setOpacity(painter.opacity() * place.opacity);
        if (!incoming && carried_out) {
            painter.drawImage(viewport.topLeft(), *carried);
        }
        std::size_t run = incoming ? outgoing_runs : 0;
        for (const auto& op : incoming ? in_ops : out_ops) {
            if (!op.run.empty()) {
                draw_run(painter, op, incoming ? in : out,
                         run < pictured ? &pictures.runs[run] : nullptr, pictures.spare, fit,
                         viewport);
                ++run;
                continue;
            }
            in_canvas(painter, fit, [&] {
                paint_step(painter, incoming ? in : out, op.live,
                           incoming && builds ? std::optional(layer.elapsed_ms) : std::nullopt);
            });
        }
        painter.restore();
    };
    if (motion.incoming_under) {
        side(true);
        side(false);
    } else {
        side(false);
        side(true);
    }
}

/// A change that does not move: each side's runs dissolve, objects that
/// change one by one do so by their own transitions, and the media goes as
/// the words do unless both sides show the same, which plays on at full
/// strength. All of it in the change's order.
void paint_dissolving(QPainter& painter,
                      const LayerPresentation& layer,
                      const CanvasFit& fit,
                      const QRectF& viewport,
                      const SceneResources& res,
                      SceneFadeLayers* scratch)
{
    const scene::SceneFrame& current = *layer.current;
    const scene::SceneFrame& outgoing = *layer.outgoing;
    const auto blend = model::blend_at(layer.content_type, layer.content_progress);
    // Once the slide is in, builds may play on: what went is not drawn.
    const bool going = blend.outgoing > 0.0;
    // Canvas objects carry stable ids and their own transitions, so they
    // change object by object; the body, the title boxes and objects without
    // ids have neither and change with their side.
    const bool per_object = scene_objects_have_ids(current) || scene_objects_have_ids(outgoing);
    const auto order =
        scene::paint_order(outgoing, current,
                           {.match_objects = per_object, .same_media = same_media(outgoing, current)});
    const auto ops = plan_change(order, [&](const scene::PaintStep& step) {
        return step.part == scene::PaintPart::Media
               || (per_object && step.part == scene::PaintPart::Object);
    });
    const FrameSide out = side_of(outgoing, res);
    const FrameSide in = side_of(current, res);
    // Without scratch, each run is drawn anew every frame: the same, and
    // dearer.
    std::size_t pictured = 0;
    QImage local_spare;
    QImage& spare = scratch != nullptr ? scratch->spare : local_spare;
    if (scratch != nullptr) {
        std::vector<RunSource> runs;
        for (const auto& op : ops) {
            if (!op.run.empty()) {
                runs.push_back({.op = &op, .side = op.outgoing_run ? &out : &in});
            }
        }
        pictured = prepare_runs(*scratch, runs, fit, viewport);
    }

    const double base = painter.opacity();
    std::size_t run = 0;
    for (const auto& op : ops) {
        if (!op.run.empty()) {
            if (!op.outgoing_run || going) {
                painter.setOpacity(base * (op.outgoing_run ? blend.outgoing : blend.incoming));
                draw_run(painter, op, op.outgoing_run ? out : in,
                         run < pictured ? &scratch->runs[run] : nullptr, spare, fit, viewport);
                painter.setOpacity(base);
            }
            ++run;
            continue;
        }
        const scene::PaintStep& step = op.live;
        if (step.part == scene::PaintPart::Media) {
            // The same picture or video plays on through the change, at full
            // strength. Another item: what it showed goes as its words do,
            // what comes in comes with them.
            if (step.side == scene::PaintSide::Outgoing && !going) {
                continue;
            }
            painter.setOpacity(step.side == scene::PaintSide::Both       ? base
                               : step.side == scene::PaintSide::Outgoing ? base * blend.outgoing
                                                                         : base * blend.incoming);
            in_canvas(painter, fit, [&] {
                paint_step(painter, step.side == scene::PaintSide::Outgoing ? out : in, step,
                           std::nullopt);
            });
            painter.setOpacity(base);
            continue;
        }
        // An object that changes by its own transition, drawn live over
        // what is under it and under what is over it.
        in_canvas(painter, fit, [&] {
            paint_object_step(painter, step, outgoing, out.ctx, current, in.ctx,
                              layer.content_progress, current.content_transition,
                              layer.elapsed_ms);
        });
    }
}

/// One stack's picture: its background, then its content, each changing by
/// its own transition. Every stack shows its own video; only the top stack
/// plays the theme's background video (a stack below shows its poster).
void paint_layer(QPainter& painter,
                 const LayerPresentation& layer,
                 const CanvasFit& fit,
                 const QRectF& viewport,
                 const QRectF& canvas,
                 const SceneResources& res,
                 bool top)
{
    const QImage* bg_video = top ? res.background_video_frame : nullptr;
    const scene::SceneBackground& bg_in = layer.incoming_background();
    if (layer.background_fading()) {
        // A background changes where it is: a moving type fades it.
        const auto blend = model::blend_at(model::transition_moves(layer.background_type)
                                               ? model::TransitionType::Fade
                                               : layer.background_type,
                                           layer.background_progress);
        painter.save();
        painter.setOpacity(painter.opacity() * blend.outgoing);
        paint_background_pass(painter, *layer.background_outgoing, fit, viewport, canvas, res,
                              bg_video);
        painter.restore();
        painter.save();
        painter.setOpacity(painter.opacity() * blend.incoming);
        paint_background_pass(painter, bg_in, fit, viewport, canvas, res, bg_video);
        painter.restore();
    } else {
        paint_background_pass(painter, bg_in, fit, viewport, canvas, res, bg_video);
    }
    paint_layer_content(painter, layer, fit, viewport, res, top);
}

}  // namespace

void paint_layer_content(QPainter& painter,
                         const LayerPresentation& layer,
                         const CanvasFit& fit,
                         const QRectF& viewport,
                         const SceneResources& res,
                         bool top)
{
    const scene::SceneFrame& current = *layer.current;
    if (!layer.content_fading()) {
        in_canvas(painter, fit, [&] { paint_frame(painter, side_of(current, res), std::nullopt); });
        return;
    }

    SceneFadeLayers* scratch = layer.fade_layers != nullptr ? layer.fade_layers
                               : top                       ? res.fade_layers
                                                           : nullptr;
    if (model::transition_moves(layer.content_type)) {
        paint_moving(painter, layer, fit, viewport, res, scratch);
        return;
    }
    // A change during a change: the outgoing side is the picture as it stood
    // when this one began, words, objects and media alike.
    const QImage* carried = layer.outgoing_picture;
    if (carried != nullptr && !carried->isNull()) {
        const auto blend = model::blend_at(layer.content_type, layer.content_progress);
        // Once the slide is in, builds may play on: what went is not drawn.
        if (blend.outgoing > 0.0) {
            painter.save();
            painter.setOpacity(painter.opacity() * blend.outgoing);
            painter.drawImage(viewport.topLeft(), *carried);
            painter.restore();
        }
        if (blend.incoming <= 0.0) {
            return;
        }
        const FrameSide in = side_of(current, res);
        painter.save();
        painter.setOpacity(painter.opacity() * blend.incoming);
        if (painter.opacity() < 1.0 && scene::paint_order(current).size() > 1) {
            // Coming in, the slide blends as one, as a dissolve's sides do:
            // what is over something else still hides it.
            QImage local;
            QImage& picture = scratch != nullptr ? scratch->spare : local;
            render_picture(picture, fit, viewport, [&](QPainter& p) {
                paint_frame(p, in, layer.elapsed_ms);
            });
            painter.drawImage(viewport.topLeft(), picture);
        } else {
            in_canvas(painter, fit, [&] { paint_frame(painter, in, layer.elapsed_ms); });
        }
        painter.restore();
        return;
    }
    paint_dissolving(painter, layer, fit, viewport, res, scratch);
}


ScenePresentation ScenePresentation::still(const scene::SceneFrame& frame) noexcept
{
    ScenePresentation pres;
    pres.current = &frame;
    return pres;
}

const scene::SceneBackground& LayerPresentation::incoming_background() const noexcept
{
    return background != nullptr ? *background : current->background;
}

void paint_scene_presentation(QPainter& painter,
                              const ScenePresentation& pres,
                              const CanvasFit& fit,
                              const QRectF& viewport,
                              const SceneResources& res)
{
    if (pres.current == nullptr) {
        return;
    }
    const scene::SceneFrame& current = *pres.current;
    if (res.rasters != nullptr) {
        res.rasters->begin_pass();
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF canvas(0, 0, current.canvas_width, current.canvas_height);

    // Hide the stacks below only when the top background paints opaque
    // pixels across the viewport. A no-op fill (transparent, or a video with
    // no frame and no poster) still has to let them show, and mid-fade the
    // background is partly transparent for the whole transition.
    const bool stacks_hidden = !pres.background_fading()
        && background_hides_stacks(pres.incoming_background(), fit, res.background_video_frame);
    if (!stacks_hidden) {
        if (!pres.below.empty()) {
            for (const auto& layer : pres.below) {
                if (layer.current != nullptr) {
                    paint_layer(painter, layer, fit, viewport, canvas, res, false);
                }
            }
        } else {
            for (const auto& frame : current.stacked_below) {
                LayerPresentation settled;
                settled.current = frame.get();
                paint_layer(painter, settled, fit, viewport, canvas, res, false);
            }
        }
    }
    paint_layer(painter, pres, fit, viewport, canvas, res, true);
}

void render_scene_presentation_into(QImage& target,
                                    const ScenePresentation& pres,
                                    int viewport_w,
                                    int viewport_h,
                                    const QColor& letterbox_color,
                                    const SceneResources& res)
{
    const int w = std::max(1, viewport_w);
    const int h = std::max(1, viewport_h);
    // Premultiplied so that a transparent letterbox blends correctly while a
    // fade is in flight - keyed NDI / Blackmagic output depends on it.
    if (target.width() != w || target.height() != h
        || target.format() != QImage::Format_ARGB32_Premultiplied) {
        target = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    }
    target.fill(letterbox_color);
    if (pres.current == nullptr) {
        return;
    }

    QPainter painter(&target);
    const CanvasFit fit =
        fit_canvas(pres.current->canvas_width, pres.current->canvas_height, w, h);
    paint_scene_presentation(painter, pres, fit, QRectF(0, 0, w, h), res);
    painter.end();
}

QImage render_scene_presentation_image(const ScenePresentation& pres,
                                       int viewport_w,
                                       int viewport_h,
                                       const QColor& letterbox_color,
                                       const SceneResources& res)
{
    QImage img;
    render_scene_presentation_into(img, pres, viewport_w, viewport_h, letterbox_color, res);
    return img;
}

QImage render_scene_frame_image(const scene::SceneFrame& frame,
                                int viewport_w,
                                int viewport_h,
                                const QColor& letterbox_color,
                                const SceneResources& resources)
{
    return render_scene_presentation_image(ScenePresentation::still(frame), viewport_w,
                                           viewport_h, letterbox_color, resources);
}

QImage paint_still(const StillPaint& still)
{
    if (!still.frame) {
        return {};
    }
    SceneResources res;
    res.cache = still.cache;
    res.now = still.now;
    res.faces = still.faces;
    res.background_video_frame =
        still.background_video_frame.isNull() ? nullptr : &still.background_video_frame;
    res.source_frames = still.source_frames.empty() ? nullptr : &still.source_frames;
    QImage img = render_scene_frame_image(*still.frame, still.size.width(), still.size.height(),
                                          still.letterbox, res);
    return still.checkerboard ? composite_on_transparency_checkerboard(img) : img;
}

}  // namespace seventhslide::render
