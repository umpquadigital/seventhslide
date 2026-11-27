#pragma once

// What one output is showing, and the pixels for it.
//
// Holds the transition state for an output plus the resources a composite
// needs (image cache, video frames, font faces), and hands out composited
// surfaces. Several destinations on the same output share one of these, so the
// scene is composited once per size rather than once per destination - and the
// background images behind it are decoded once rather than once per cache.
//
// The compositing runs on a thread of the presenter's own (the audit's
// RENDER-1): the window's thread keeps what is shown and how far each
// transition has got, and hands the worker an immutable snapshot of it with
// each request; the worker owns the destination-sized buffers, the cross-fade
// scratch and the retained element rasters. A live destination asks with
// ``request`` and is told when its picture is ready; ``surface`` waits for it,
// for a snapshot and for tests. Without an application object (no event loop
// to run a thread on) it composites on the caller's thread.
//
// Fade progress is quantised to a fixed grid (see ``k_fade_step_ms``) so that
// destinations ticking on their own timers, a few milliseconds apart, still
// compute the same progress and can share the same surface. Without that they
// would each land on a slightly different progress and share nothing.
//
// Seam: Qt Gui. Call on the GUI thread.

#include "seventhslide/layout/qt_font_faces.hpp"
#include "seventhslide/render/background_painter.hpp"
#include "seventhslide/render/element_raster_cache.hpp"
#include "seventhslide/render/fade.hpp"
#include "seventhslide/render/scene_compositor.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QSize>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

class QObject;

namespace seventhslide::render {

/**
 * Fade progress advances on this grid (~30 fps).
 *
 * The same as a destination's default tick interval, not finer, so two
 * destinations whose timers are out of phase still visit the same steps and
 * can share one composited surface. A tick landing early can repeat a step,
 * which is invisible over a 500 ms transition.
 */
inline constexpr int k_fade_step_ms = 33;

class ScenePresenter {
public:
    ScenePresenter();
    ~ScenePresenter();
    ScenePresenter(const ScenePresenter&) = delete;
    ScenePresenter& operator=(const ScenePresenter&) = delete;

    /// Show ``frame``. Starts a transition when the theme asks for one, the
    /// outgoing frame is compatible, and this output can afford to paint it.
    /// Returns true when the picture changed.
    bool present(scene::SharedSceneFrame frame);

    /// Step any running transition onto the current grid position. True when
    /// progress moved, so callers know to re-deliver even with no new frame.
    bool advance();

    /// Composited pixels for one destination's size and letterbox colour,
    /// waiting for the worker to paint them.
    ///
    /// Cached per (size, letterbox) and reused until the presentation changes,
    /// so the second destination asking for the same pair this revision pays
    /// nothing. The buffer behind each entry is reused across revisions too:
    /// a caller that keeps the image past its use makes the next paint use
    /// the shape's spare buffer instead.
    [[nodiscard]] QImage surface(QSize size, const QColor& letterbox);

    /// ``surface`` without waiting: the worker paints the presentation as it
    /// is now, and ``done`` is called on this thread with the picture, unless
    /// ``context`` is gone by then, even if the presenter has gone. ``context``
    /// lives on this thread. What the window's thread spends is taking the
    /// snapshot.
    void request(QSize size, const QColor& letterbox, QObject* context,
                 std::function<void(QImage)> done);

    /// Bumped whenever the pixels would differ. Destinations compare it against
    /// what they last delivered instead of tracking their own dirty state.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    /// Call ``changed`` each time ``revision`` moves, on this thread, for as
    /// long as the handle returned is kept: dropping it stops the calls, and
    /// asks nothing of the presenter, so a destination may drop it after the
    /// presenter has gone. ``changed`` must not wait on the presenter or
    /// change it.
    [[nodiscard]] std::shared_ptr<void> watch(std::function<void()> changed);

    /// Whether any stack on this output is mid-transition.
    [[nodiscard]] bool is_fading() const noexcept;

    [[nodiscard]] bool has_content() const noexcept { return whole_ != nullptr; }

    /// The frame being shown (the incoming one during a transition), every
    /// stack in it; null before the first present. Destinations read what
    /// they need from here rather than keeping their own copy.
    [[nodiscard]] const scene::SceneFrame* current_frame() const noexcept
    {
        return whole_.get();
    }

    /// Settle any transition in flight, keeping the current frame.
    void settle();

    void set_static_root(std::filesystem::path root);
    /// Draw pictures from ``cache``, shared with other presenters, rather
    /// than from a cache of this one's own: one decode serves every output.
    void share_image_cache(std::shared_ptr<BackgroundImageCache> cache);
    /// A picture its cache had missed has arrived
    /// (``BackgroundImageCache::forget_missing``, asked of the cache once for
    /// every presenter sharing it): draw again.
    void picture_arrived() { bump(); }
    void set_font_faces(const layout::QtFontFaces* faces) noexcept { font_faces_ = faces; }
    /// Frames of the theme's background video. Ignored while the output is
    /// frozen, so a freeze holds the picture on every destination.
    void set_background_video_frame(QImage frame);
    /// The sources' latest pictures (and videos' clocks): every video item's,
    /// web pages', live sources', video objects'. A source a frozen layer
    /// shows keeps the picture it had, until the layer thaws; a source this
    /// output is not showing does not count as a change. A video
    /// going plays on through the change that replaces it under
    /// its own source, so the stack fading from it shows it moving.
    void set_source_frames(const SourceFrames& frames);

    /// Narrow the cadence a transition has to fit inside. Destinations report
    /// their own tick interval; the tightest one governs, so every destination
    /// on an output transitions the same way rather than some fading and some
    /// cutting.
    void note_tick_budget_ms(int ms);

    /// Test hook: freeze the fade clocks and drive progress directly.
    void set_fade_progress_for_test(double t);

private:
    /// Wall-clock driver for one fade, quantised to ``k_fade_step_ms``.
    struct FadeClock {
        QElapsedTimer timer;
        int duration_ms = 0;
        bool running = false;

        void start(int ms);
        void stop() noexcept { running = false; }
        /// Linear 0..1 at the current grid step; 1.0 when stopped.
        [[nodiscard]] double fraction() const;
    };

    /// One stack's picture on this output and how it is changing: each
    /// stack changes by its own transition.
    struct Layer {
        /// The stack's id, or its place when a frame names none.
        std::string key;
        FadePresenter content;
        BackgroundFadePresenter background;
        FadeClock content_clock;
        FadeClock background_clock;
        /// Which content change this is: the worker keeps its cross-fade
        /// scratch and any carried picture for the change they were made for.
        std::uint64_t generation = 0;
        /// The change's length in all, the part its transition takes (the
        /// rest is builds), and how far into it the clock is.
        int total_ms = 0;
        int slide_ms = 0;
        double elapsed_ms = 0.0;

        [[nodiscard]] bool fading() const noexcept
        {
            return content.is_fading() || background.is_fading();
        }
    };

    /// What the worker paints from: an immutable copy of the presentation.
    struct LayerSnap;
    struct Snapshot;
    class Compositor;

    [[nodiscard]] std::shared_ptr<const Snapshot> snapshot() const;
    [[nodiscard]] LayerSnap snap_layer(const Layer& layer) const;
    /// ``frame``'s stacks back to front, each its own frame, keyed: those
    /// below as they are shared in it, and the top one's ``frame`` itself.
    [[nodiscard]] static std::vector<std::pair<std::string, scene::SharedSceneFrame>> split_layers(
        const scene::SharedSceneFrame& frame);
    [[nodiscard]] bool can_afford_fade() const;
    /// The pixels would differ: a new revision, and the watchers told.
    void bump();
    [[nodiscard]] bool frozen() const noexcept;
    /// Paint the latest sources' pictures from now on, but for those a frozen
    /// layer shows, which keep theirs; true when a source an unfrozen layer
    /// shows has a different picture.
    bool take_source_frames();

    /// What was last presented, every stack in it.
    scene::SharedSceneFrame whole_;
    /// Its stacks, back to front; the last is the top one.
    std::vector<Layer> layers_;

    int tick_budget_ms_ = k_fade_step_ms;

    std::uint64_t revision_ = 1;
    std::uint64_t next_generation_ = 1;
    /// Those ``watch`` hands out, while their holders keep them.
    std::vector<std::weak_ptr<std::function<void()>>> watchers_;

    /// Thread-safe: the worker reads it, and it is warmed off the window's
    /// thread (``OutputSurfaces::prefetch_static_urls``).
    std::shared_ptr<BackgroundImageCache> cache_ = std::make_shared<BackgroundImageCache>();
    QImage bg_video_frame_;
    /// The sources' pictures this output paints, and the latest handed in
    /// (which differ only while it is frozen).
    SourceFrames source_frames_;
    SourceFrames latest_source_frames_;
    const layout::QtFontFaces* font_faces_ = nullptr;
    /// Last: it stops its thread before anything above goes.
    std::unique_ptr<Compositor> compositor_;
};

}  // namespace seventhslide::render
