#pragma once

// The one place a SceneFrame becomes pixels.
//
// Audience windows, the operator preview and every offscreen sink (NDI,
// virtual camera, Blackmagic) share this composite, so a slide looks the same
// wherever it is shown. It paints a *presentation* - the settled frame, or two
// frames and a progress while a transition runs - rather than a single frame,
// because a cross-fade is the general case and a still is the degenerate one.
//
// Each frame's content paints back to front in its ``scene::paint_order``:
// the body, title boxes, objects and media each at its own place in the stack,
// as the theme or deck was designed. Every path here (settled, a dissolve, a
// moving change, a change during a change) walks that one order.
//
// Fade *state* (what is outgoing, how far along, when to snap) lives in
// fade.hpp, which stays Qt-free and is shared the same way. This header is the
// Qt side of a transition: the composite order.
// Seam: Qt Gui (QPainter). Call on the GUI thread.

#include "seventhslide/layout/qt_font_faces.hpp"
#include "seventhslide/render/background_painter.hpp"
#include "seventhslide/render/canvas_fit.hpp"
#include "seventhslide/render/paint_context.hpp"
#include "seventhslide/render/source_frames.hpp"
#include "seventhslide/scene/paint_order.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QSize>

#include <cstddef>
#include <vector>

namespace seventhslide::render {

struct SceneFadeLayers;

/**
 * One stack's picture at one instant.
 *
 * A non-null ``outgoing`` means its content is mid-transition; a non-null
 * ``background_outgoing`` means its background is. The two run independently -
 * a slide change that keeps the same background changes only the content -
 * which is why they carry separate progresses. What the content shows (its
 * words, objects, picture, video or page) changes as one, each part at its
 * place in the stack (``scene::paint_order``): when the media differs too,
 * each side's is painted at that side's opacity.
 *
 * ``current`` must outlive the call. This is a view, not an owner.
 */
struct LayerPresentation {
    const scene::SceneFrame* current = nullptr;
    /// Frame being faded out, or null when the content is settled.
    const scene::SceneFrame* outgoing = nullptr;
    /// 0 = fully outgoing, 1 = fully current (eased): the transition's part.
    double content_progress = 1.0;
    /// How far into the change, in ms: what each object's build plays by.
    double elapsed_ms = 0.0;
    /// Incoming background. Null means ``current->background``.
    const scene::SceneBackground* background = nullptr;
    /// Background being faded out, or null when the background is settled.
    const scene::SceneBackground* background_outgoing = nullptr;
    double background_progress = 1.0;
    /// How each changes: a cross-fade, or down and up through nothing.
    model::TransitionType content_type = model::TransitionType::Fade;
    model::TransitionType background_type = model::TransitionType::Fade;
    /// This layer's cross-fade scratch; null falls back to
    /// ``SceneResources::fade_layers`` for the top layer, else paints live.
    SceneFadeLayers* fade_layers = nullptr;
    /// A change that came while another was running: the outgoing side is
    /// the layer's content as it stood then (viewport-sized, the canvas fit
    /// applied), not ``outgoing``'s, so nothing jumps.
    const QImage* outgoing_picture = nullptr;

    [[nodiscard]] bool content_fading() const noexcept { return outgoing != nullptr; }
    [[nodiscard]] bool background_fading() const noexcept
    {
        return background_outgoing != nullptr;
    }
    /// The background to paint at ``background_progress``. Requires ``current``.
    [[nodiscard]] const scene::SceneBackground& incoming_background() const noexcept;
};

/**
 * What to show, at one instant: the top stack's picture (the fields this
 * inherits) and the stacks under it, back to front, each changing by its own
 * transition. With ``below`` empty the stacks under the top one
 * are ``current->stacked_below``, settled.
 */
struct ScenePresentation : LayerPresentation {
    std::vector<LayerPresentation> below;

    /// A settled frame: no transition in flight.
    [[nodiscard]] static ScenePresentation still(const scene::SceneFrame& frame) noexcept;
};

/// The most pictures one change keeps (``SceneFadeLayers``): two per side,
/// enough for content split around its media. A change with more runs draws
/// the rest anew on every frame, so a change never holds more than this (and
/// its one spare).
inline constexpr std::size_t k_fade_run_pictures = 4;

/**
 * Scratch surfaces that let a cross-fade blend pictures instead of repainting
 * everything twice per frame: the compositor paints each run once and then
 * blends.
 *
 * A run is one side's content that holds still through the change, between
 * two things painted on every frame of it (the media, and objects changing one
 * by one). Each is drawn into a viewport-sized picture. Content with nothing
 * live in the middle of it is one run per side; content split by its media
 * (in a moving change, or with objects that have no ids) is two.
 *
 * The caller owns them and owns invalidation: clear ``valid`` when a new
 * transition begins. The compositor repaints them when they are invalid, the
 * viewport size changed or the change's runs are other than those held, and
 * otherwise reuses them as-is.
 *
 * This dissolves a run as a unit, so where two elements of one run overlap,
 * the pair cross-fades together rather than each fading on its own.
 */
struct SceneFadeLayers {
    /// One picture per run, in the order the change paints them, at most
    /// ``k_fade_run_pictures``.
    std::vector<QImage> runs;
    /// The steps each picture holds.
    std::vector<std::vector<scene::PaintStep>> held;
    /// Where content with no picture of its own that must still blend as one
    /// (a run past those kept, a slide coming in under a carried picture) is
    /// drawn anew on each frame. Never valid from one frame to the next.
    QImage spare;
    QSize size;
    bool valid = false;
};

/// Everything a composite needs besides the frames: what every element painter
/// takes (pictures, fonts, clock time, the video player, retained rasters),
/// plus this composite's video frames and fade scratch. Every field is
/// optional; a default-constructed set paints a frame that needs no images or
/// video, drawing every element live.
struct SceneResources : PaintContext {
    const QImage* background_video_frame = nullptr;
    /// Optional cross-fade scratch. Without it a transition repaints both
    /// sides every frame, which is correct but much dearer.
    SceneFadeLayers* fade_layers = nullptr;
};

/**
 * Paint ``presentation`` into ``painter`` over ``viewport``.
 *
 * ``fit`` places the design canvas inside the viewport. The caller owns it
 * because a window recomputes it on resize while a still computes one per call.
 * Sets its own render hints, and leaves the painter's transform and opacity as
 * it found them. A null ``presentation.current`` paints nothing.
 */
void paint_scene_presentation(QPainter& painter,
                              const ScenePresentation& presentation,
                              const CanvasFit& fit,
                              const QRectF& viewport,
                              const SceneResources& resources = {});

/**
 * ``layer``'s content alone - its words, objects and media, mid-transition
 * as it is - without its background, over ``viewport``. What a change that
 * comes during another fades from (``LayerPresentation::outgoing_picture``).
 * ``top`` says whether it may use ``SceneResources::fade_layers``, the top
 * layer's cross-fade scratch.
 */
void paint_layer_content(QPainter& painter,
                         const LayerPresentation& layer,
                         const CanvasFit& fit,
                         const QRectF& viewport,
                         const SceneResources& res,
                         bool top);

/**
 * Composite a presentation into an existing buffer, reusing its allocation.
 *
 * ``target`` is reallocated only when its size or format does not already
 * match, so a destination that composites every tick stops paying for a fresh
 * buffer each time - at 4K that allocate-and-clear is around 6 ms on its own.
 *
 * Reuse is safe even if a caller keeps a shallow copy: QImage is
 * copy-on-write, so painting here detaches instead of corrupting it. It just
 * stops saving the allocation, which is why the sinks deep-copy on delivery.
 */
void render_scene_presentation_into(QImage& target,
                                    const ScenePresentation& presentation,
                                    int viewport_w,
                                    int viewport_h,
                                    const QColor& letterbox_color = QColor(Qt::black),
                                    const SceneResources& resources = {});

/**
 * Composite a presentation into a new image.
 *
 * ``letterbox_color`` fills whatever the canvas does not cover. The image is
 * premultiplied, so a transparent letterbox blends correctly mid-fade (keyed
 * NDI / Blackmagic output depends on that). Prefer
 * ``render_scene_presentation_into`` on a path that repeats.
 */
[[nodiscard]] QImage render_scene_presentation_image(const ScenePresentation& presentation,
                                                     int viewport_w,
                                                     int viewport_h,
                                                     const QColor& letterbox_color = QColor(
                                                         Qt::black),
                                                     const SceneResources& resources = {});

/// Composite one settled frame into a new image (thumbnails, theme previews,
/// export). Shorthand for ``render_scene_presentation_image`` of a still.
[[nodiscard]] QImage render_scene_frame_image(const scene::SceneFrame& frame,
                                              int viewport_w,
                                              int viewport_h,
                                              const QColor& letterbox_color = QColor(Qt::black),
                                              const SceneResources& resources = {});

/**
 * One still render with everything it reads, so it can be painted on any
 * thread: the frame is shared and never changes, the pictures and the videos'
 * clocks are copies, and the image cache and font faces lock themselves.
 * Built on the GUI thread from live state; painted wherever the picture is
 * wanted.
 */
struct StillPaint {
    scene::SharedSceneFrame frame;
    QSize size;
    QColor letterbox = QColor(Qt::black);
    /// Composite the picture over a transparency checkerboard.
    bool checkerboard = false;
    BackgroundImageCache* cache = nullptr;
    /// See ``PaintContext::now``.
    QDateTime now;
    QImage background_video_frame;
    /// The sources it shows: every video's picture and clock, pages, live
    /// sources, video objects.
    SourceFrames source_frames;
    const layout::QtFontFaces* faces = nullptr;
};

/// Paint ``still``. Any thread; null without a frame.
[[nodiscard]] QImage paint_still(const StillPaint& still);

}  // namespace seventhslide::render
