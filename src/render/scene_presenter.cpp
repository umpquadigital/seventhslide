#include <seventhslide/render/scene_presenter.hpp>

#include <seventhslide/model/transition.hpp>
#include <seventhslide/render/text_painter.hpp>

#include <QElapsedTimer>
#include <QThread>
#include <QPointer>
#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <optional>
#include <memory>
#include <map>
#include <functional>
#include <atomic>
#include <utility>

namespace seventhslide::render {

void ScenePresenter::FadeClock::start(int ms)
{
    duration_ms = ms;
    running = ms > 0;
    timer.start();
}

double ScenePresenter::FadeClock::fraction() const
{
    if (!running || duration_ms <= 0) {
        return 1.0;
    }
    // Quantise to the fade grid. Two destinations ticking a few milliseconds
    // apart land on the same step and so agree on progress, which is what lets
    // them share one composited surface.
    const qint64 elapsed = timer.elapsed();
    if (elapsed >= duration_ms) {
        return 1.0;  // land exactly on time rather than a step late
    }
    const qint64 stepped = (elapsed / k_fade_step_ms) * k_fade_step_ms;
    return static_cast<double>(stepped) / static_cast<double>(duration_ms);
}

// ---- what the worker paints from -------------------------------------------

struct ScenePresenter::LayerSnap {
    std::string key;
    std::uint64_t generation = 0;
    /// Kept alive here: the worker paints them after the window's thread has
    /// moved on.
    scene::SharedSceneFrame current;
    scene::SharedSceneFrame outgoing;
    std::optional<scene::SceneBackground> background;
    std::optional<scene::SceneBackground> background_outgoing;
    double content_progress = 1.0;
    double background_progress = 1.0;
    /// How far into the change: what each object's build plays by.
    double elapsed_ms = 0.0;
    model::TransitionType content_type = model::TransitionType::Fade;
    model::TransitionType background_type = model::TransitionType::Fade;
};

struct ScenePresenter::Snapshot {
    std::uint64_t revision = 0;
    bool fading = false;
    /// Back to front; the last is the top stack.
    std::vector<LayerSnap> layers;
    QImage background_video_frame;
    SourceFrames source_frames;
    /// Held, so a cache replaced meanwhile (``share_image_cache``) lasts
    /// until the paint that uses it is done.
    std::shared_ptr<BackgroundImageCache> cache;
    const layout::QtFontFaces* faces = nullptr;
};

namespace {

/// One destination shape's composited pixels, and what they cost.
///
/// Tracked per shape because destinations on one output need not be the same
/// size, and the transition decision answers for the dearest of them.
///
/// ``fade_ms`` is the cheapest *transition* frame seen at this shape; 0 =
/// unknown. A fade frame reuses the cached runs of each side's content
/// (SceneFadeLayers), so it costs far less than a settled one. The first
/// frame of each transition builds those caches and is discarded. Cheapest
/// rather than latest, so a passing load spike cannot turn transitions off.
struct Surface {
    QSize size;
    QRgb letterbox = 0;
    std::uint64_t revision = 0;
    bool valid = false;
    QImage image;
    /// Painted into instead of ``image`` while a destination still holds that
    /// one (a window's texture keeps its image until it is replaced). Without
    /// it, the next composite would copy the whole held buffer before painting
    /// over it; with it, the two alternate. Allocated only once a destination
    /// actually holds on.
    QImage spare;
    double fade_ms = 0.0;
    int fade_samples = 0;
    /// Cross-fade scratch, one per stack mid-transition, by the stack's key,
    /// with the change it was made for. Held only while a transition runs: at
    /// most ``k_fade_run_pictures`` pictures each, around 33 MB apiece at 4K.
    std::map<std::string, std::pair<std::uint64_t, SceneFadeLayers>> layers;
    /// A change that came during another: each such stack's content as it
    /// stood then, at this shape, with the change it is the outgoing side of.
    std::map<std::string, std::pair<std::uint64_t, QImage>> carried;
};

/// At most this many destination shapes are kept; the least recently
/// composited is evicted. Real outputs have one to three, and the operator's
/// preview one more.
constexpr std::size_t k_max_surfaces = 4;

}  // namespace

/// The worker: a thread of its own, the destination shapes' buffers, their
/// scratch, and the retained rasters. Everything here past construction is
/// touched only on its thread (or the caller's, with no application to run a
/// thread on).
class ScenePresenter::Compositor {
public:
    Compositor()
    {
        if (QCoreApplication::instance() == nullptr) {
            return;  // no event loop to run a thread on: paint inline
        }
        thread_.setObjectName(QStringLiteral("SeventhSlide compositor"));
        context_ = new QObject();
        context_->moveToThread(&thread_);
        thread_.start();
    }

    ~Compositor()
    {
        if (context_ != nullptr) {
            QMetaObject::invokeMethod(context_, [this] { delete context_; },
                                      Qt::BlockingQueuedConnection);
            thread_.quit();
            thread_.wait();
        }
    }

    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    /// The dearest transition frame seen at any shape, in ms; 0 for none yet.
    [[nodiscard]] double worst_fade_ms() const noexcept { return worst_fade_ms_.load(); }

    /// ``job`` on the worker, in order with every other.
    void post(std::function<void()> job)
    {
        if (context_ == nullptr) {
            job();
            return;
        }
        QMetaObject::invokeMethod(context_, std::move(job), Qt::QueuedConnection);
    }

    /// ``job`` on the application's thread, from the worker. Posted to the
    /// application, which outlives every presenter, so it runs even if this
    /// one has gone.
    void hand_back(std::function<void()> job)
    {
        if (context_ == nullptr) {
            job();
            return;
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(job),
                                  Qt::QueuedConnection);
    }

    /// ``job``'s result, waited for.
    QImage wait_for(std::function<QImage()> job)
    {
        if (context_ == nullptr) {
            return job();
        }
        QImage out;
        QMetaObject::invokeMethod(context_, [&] { out = job(); }, Qt::BlockingQueuedConnection);
        return out;
    }

    /// The picture ``snap`` shows at ``size`` over ``letterbox``. Worker only.
    QImage composite(const Snapshot& snap, QSize size, QRgb letterbox)
    {
        auto it = std::find_if(surfaces_.begin(), surfaces_.end(), [&](const Surface& s) {
            return s.size == size && s.letterbox == letterbox;
        });
        if (it == surfaces_.end()) {
            if (surfaces_.size() >= k_max_surfaces) {
                // Evict the shape composited longest ago - a destination that
                // was resized leaves its old shape behind otherwise.
                const auto oldest = std::min_element(
                    surfaces_.begin(), surfaces_.end(),
                    [](const Surface& a, const Surface& b) { return a.revision < b.revision; });
                surfaces_.erase(oldest);
            }
            surfaces_.push_back(Surface{});
            it = std::prev(surfaces_.end());
            it->size = size;
            it->letterbox = letterbox;
        }
        if (it->valid && it->revision == snap.revision) {
            return it->image;  // another destination already composited this one
        }
        drop_stale(*it, snap);
        if (!it->image.isNull() && !it->image.isDetached()) {
            std::swap(it->image, it->spare);
        }

        const SceneResources res = resources(snap);
        ScenePresentation pres;
        for (std::size_t i = 0; i < snap.layers.size(); ++i) {
            const LayerPresentation one = layer_of(snap.layers[i], *it);
            if (i + 1 == snap.layers.size()) {
                static_cast<LayerPresentation&>(pres) = one;
            } else {
                pres.below.push_back(one);
            }
        }
        QElapsedTimer timer;
        timer.start();
        render_scene_presentation_into(it->image, pres, size.width(), size.height(),
                                       QColor::fromRgba(letterbox), res);
        if (snap.fading) {
            note_fade_render(*it, static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0);
        }
        it->revision = snap.revision;
        it->valid = true;
        return it->image;
    }

    /// Paint ``was`` (stack ``was.key`` as it stood) into every shape's
    /// carried picture for change ``generation``, the outgoing side of a
    /// change that came during another. Worker only.
    void carry(const LayerSnap& was, std::uint64_t generation, bool top, const Snapshot& res_snap)
    {
        const SceneResources res = resources(res_snap);
        for (Surface& surface : surfaces_) {
            if (!surface.valid || !was.current) {
                continue;
            }
            const LayerPresentation now = layer_of(was, surface);
            QImage picture(surface.size, QImage::Format_ARGB32_Premultiplied);
            picture.fill(Qt::transparent);
            QPainter painter(&picture);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::TextAntialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const CanvasFit fit = fit_canvas(was.current->canvas_width, was.current->canvas_height,
                                             surface.size.width(), surface.size.height());
            paint_layer_content(painter, now, fit,
                                QRectF(0, 0, surface.size.width(), surface.size.height()), res,
                                top);
            painter.end();
            surface.carried[was.key] = {generation, std::move(picture)};
            surface.fade_samples = 0;  // the next transition's first frame rebuilds the scratch
        }
    }

private:
    [[nodiscard]] SceneResources resources(const Snapshot& snap)
    {
        SceneResources res;
        res.cache = snap.cache.get();
        res.background_video_frame =
            snap.background_video_frame.isNull() ? nullptr : &snap.background_video_frame;
        res.source_frames = snap.source_frames.empty() ? nullptr : &snap.source_frames;
        res.faces = snap.faces;
        res.rasters = &rasters_;
        return res;
    }

    /// ``snap`` as the compositor paints it at ``surface``: with that
    /// shape's scratch and carried picture for its change.
    [[nodiscard]] static LayerPresentation layer_of(const LayerSnap& snap, Surface& surface)
    {
        LayerPresentation one;
        one.current = snap.current.get();
        one.outgoing = snap.outgoing.get();
        one.content_progress = snap.content_progress;
        one.background = snap.background ? &*snap.background : nullptr;
        one.background_outgoing = snap.background_outgoing ? &*snap.background_outgoing : nullptr;
        one.background_progress = snap.background_progress;
        one.content_type = snap.content_type;
        one.background_type = snap.background_type;
        one.elapsed_ms = snap.elapsed_ms;
        if (one.outgoing != nullptr) {
            auto& scratch = surface.layers[snap.key];
            if (scratch.first != snap.generation) {
                scratch = {snap.generation, SceneFadeLayers{}};
            }
            one.fade_layers = &scratch.second;
            if (const auto carried = surface.carried.find(snap.key);
                carried != surface.carried.end() && carried->second.first == snap.generation) {
                one.outgoing_picture = &carried->second.second;
            }
        }
        return one;
    }

    /// Give back scratch and carried pictures for changes that are over.
    static void drop_stale(Surface& surface, const Snapshot& snap)
    {
        const auto live = [&snap](const std::string& key, std::uint64_t generation) {
            return std::any_of(snap.layers.begin(), snap.layers.end(), [&](const LayerSnap& l) {
                return l.key == key && l.outgoing && l.generation == generation;
            });
        };
        std::erase_if(surface.layers,
                      [&](const auto& entry) { return !live(entry.first, entry.second.first); });
        std::erase_if(surface.carried,
                      [&](const auto& entry) { return !live(entry.first, entry.second.first); });
    }

    void note_fade_render(Surface& surface, double ms) noexcept
    {
        ++surface.fade_samples;
        if (surface.fade_samples <= 1) {
            return;  // builds the cached layers: a one-off, not the cadence
        }
        if (surface.fade_ms <= 0.0 || ms < surface.fade_ms) {
            surface.fade_ms = ms;
        }
        double worst = 0.0;
        for (const Surface& s : surfaces_) {
            worst = std::max(worst, s.fade_ms);
        }
        worst_fade_ms_.store(worst);
    }

    QThread thread_;
    QObject* context_ = nullptr;
    std::vector<Surface> surfaces_;
    /// Shared by every destination of this output: a window and a sink of the
    /// same size reuse each other's rasters.
    ElementRasterCache rasters_;
    std::atomic<double> worst_fade_ms_{0.0};
};

ScenePresenter::ScenePresenter()
    : compositor_(std::make_unique<Compositor>())
{
}

ScenePresenter::~ScenePresenter() = default;

ScenePresenter::LayerSnap ScenePresenter::snap_layer(const Layer& layer) const
{
    LayerSnap snap;
    snap.key = layer.key;
    snap.generation = layer.generation;
    snap.current = layer.content.current();
    snap.content_type = snap.current ? snap.current->content_transition.type
                                     : model::TransitionType::Fade;
    snap.background_type = layer.background.last_spec().type;
    if (layer.content.is_fading() && layer.content.outgoing()) {
        snap.outgoing = layer.content.outgoing();
        snap.content_progress = layer.content.progress();
        snap.elapsed_ms = layer.elapsed_ms;
    }
    snap.background = layer.background.current();
    if (layer.background.is_fading() && layer.background.outgoing()) {
        snap.background_outgoing = layer.background.outgoing();
        snap.background_progress = layer.background.progress();
    }
    return snap;
}

std::shared_ptr<const ScenePresenter::Snapshot> ScenePresenter::snapshot() const
{
    auto snap = std::make_shared<Snapshot>();
    snap->revision = revision_;
    snap->fading = is_fading();
    snap->layers.reserve(layers_.size());
    for (const Layer& layer : layers_) {
        snap->layers.push_back(snap_layer(layer));
    }
    snap->background_video_frame = bg_video_frame_;
    snap->source_frames = source_frames_;
    snap->cache = cache_;
    snap->faces = font_faces_;
    return snap;
}

bool ScenePresenter::is_fading() const noexcept
{
    return std::any_of(layers_.begin(), layers_.end(),
                       [](const Layer& layer) { return layer.fading(); });
}

std::vector<std::pair<std::string, scene::SharedSceneFrame>> ScenePresenter::split_layers(
    const scene::SharedSceneFrame& frame)
{
    // A frame without stack ids (a preview, a test) keys its stacks by place.
    const auto key_of = [](const scene::SceneFrame& f, const std::string& place) {
        return f.stack_id.empty() ? place : f.stack_id;
    };
    std::vector<std::pair<std::string, scene::SharedSceneFrame>> out;
    out.reserve(frame->stacked_below.size() + 1);
    for (std::size_t i = 0; i < frame->stacked_below.size(); ++i) {
        const auto& below = frame->stacked_below[i];
        out.emplace_back(key_of(*below, "below:" + std::to_string(i)), below);
    }
    // Nothing is copied. The top stack's layer is the whole frame, which
    // carries the stacks below too: a layer paints and compares only what is
    // its own, and those are the layers before it here.
    out.emplace_back(key_of(*frame, "top"), frame);
    return out;
}

bool ScenePresenter::can_afford_fade() const
{
    // The dearest destination shape governs: a transition this output cannot
    // paint everywhere is one it should not start anywhere, or its destinations
    // would disagree about what the same output is doing.
    const double worst = compositor_->worst_fade_ms();
    if (worst <= 0.0) {
        return true;  // no transition measured here yet - let it try
    }
    // One transition frame has to fit in one tick, or the fade cannot keep
    // cadence and a clean cut is the better picture.
    return worst <= static_cast<double>(tick_budget_ms_);
}

void ScenePresenter::note_tick_budget_ms(int ms)
{
    if (ms > 0 && ms < tick_budget_ms_) {
        tick_budget_ms_ = ms;
    }
}

std::shared_ptr<void> ScenePresenter::watch(std::function<void()> changed)
{
    auto handle = std::make_shared<std::function<void()>>(std::move(changed));
    watchers_.push_back(handle);
    return handle;
}

void ScenePresenter::bump()
{
    ++revision_;
    std::erase_if(watchers_, [](const auto& watcher) { return watcher.expired(); });
    // By place, and each held while it is called: one may watch or let go.
    for (std::size_t i = 0; i < watchers_.size(); ++i) {
        if (const auto changed = watchers_[i].lock()) {
            (*changed)();
        }
    }
}

bool ScenePresenter::present(scene::SharedSceneFrame frame)
{
    whole_ = frame;
    // Each stack changes by its own transition; a stack that comes or goes
    // cuts.
    std::vector<Layer> previous = std::move(layers_);
    std::vector<bool> kept(previous.size(), false);
    layers_.clear();
    bool changed = false;
    const bool afford = can_afford_fade();
    for (auto& [key, layer_frame] : split_layers(frame)) {
        auto it = std::find_if(previous.begin(), previous.end(),
                               [&key](const Layer& l) { return l.key == key; });
        Layer layer;
        if (it != previous.end() && !kept[static_cast<std::size_t>(it - previous.begin())]) {
            kept[static_cast<std::size_t>(it - previous.begin())] = true;
            layer = std::move(*it);
        } else {
            layer.key = key;
            changed = true;
        }
        // What the stack shows now, for a change that comes while it is still
        // changing.
        const bool was_fading = layer.content.is_fading();
        const LayerSnap was = snap_layer(layer);
        const bool was_top = !previous.empty() && it != previous.end()
                             && it == std::prev(previous.end());

        const FadePresentAction bg_action =
            layer.background.present(layer_frame->background, layer_frame->background_transition);
        const FadePresentAction content_action = layer.content.present(layer_frame);
        if (content_action == FadePresentAction::StartFade) {
            layer.generation = next_generation_++;
        }
        if (content_action == FadePresentAction::StartFade && was_fading && afford) {
            // Fade from the picture as it stands, rather than jump to the
            // frame that was coming in: the worker paints it, in order, before
            // anything of the new change.
            auto res = snapshot();
            compositor_->post([compositor = compositor_.get(), was, generation = layer.generation,
                               was_top, res] { compositor->carry(was, generation, was_top, *res); });
        }
        changed = changed || content_action != FadePresentAction::Noop
                  || bg_action != FadePresentAction::Noop;

        if (content_action == FadePresentAction::StartFade) {
            layer.total_ms = layer.content.last_config().duration_ms;
            layer.slide_ms = layer_frame->content_transition.plays()
                                 ? layer_frame->content_transition.duration_ms
                                 : 0;
            layer.elapsed_ms = 0.0;
        }
        if (content_action == FadePresentAction::StartFade && afford) {
            layer.content_clock.start(layer.content.last_config().duration_ms);
        } else if (content_action != FadePresentAction::Noop) {
            layer.content_clock.stop();
            // Settling an unaffordable fade turns it into a cut: the
            // outgoing frame is dropped and the incoming one simply shown.
            layer.content.finish();
        }
        if (bg_action == FadePresentAction::StartFade && afford) {
            layer.background_clock.start(layer.background.last_duration_ms());
        } else if (bg_action != FadePresentAction::Noop) {
            layer.background_clock.stop();
            layer.background.finish();
        }
        layers_.push_back(std::move(layer));
    }
    // A stack that went: the picture changed (its scratch goes with the
    // worker's next composite).
    for (std::size_t i = 0; i < previous.size(); ++i) {
        if (!kept[i]) {
            changed = true;
        }
    }

    // Thawing shows the sources as they are now.
    const bool sources_moved = take_source_frames();
    if (!changed) {
        if (sources_moved) {
            bump();
            return true;
        }
        // Nothing to redraw, with one exception: a clock's text is only known
        // at paint time, and the session re-presents an otherwise identical
        // frame every second to drive it.
        if (whole_ && scene_frame_has_clock(*whole_)) {
            bump();
            return true;
        }
        return false;
    }
    bump();
    return true;
}

bool ScenePresenter::advance()
{
    bool moved = false;
    for (Layer& layer : layers_) {
        if (layer.content_clock.running && layer.content.is_fading()) {
            // The change's clock: its transition over the first part, the
            // builds to its end.
            const double fraction = layer.content_clock.fraction();
            const double elapsed = fraction * layer.total_ms;
            if (fraction >= 1.0) {
                layer.content.finish();
                moved = true;
            } else {
                const double slide = layer.slide_ms > 0
                                         ? model::eased(layer.content.current()->content_transition.easing,
                                                        std::min(1.0, elapsed / layer.slide_ms))
                                         : 1.0;
                if (slide != layer.content.progress() || elapsed != layer.elapsed_ms) {
                    layer.content.hold_progress(slide);
                    moved = true;
                }
            }
            layer.elapsed_ms = elapsed;
            if (!layer.content.is_fading()) {
                layer.content_clock.stop();
            }
        }
        if (layer.background_clock.running && layer.background.is_fading()) {
            const double next = model::eased(layer.background.last_spec().easing,
                                             layer.background_clock.fraction());
            if (next != layer.background.progress()) {
                layer.background.set_progress(next);
                moved = true;
            }
            if (!layer.background.is_fading()) {
                layer.background_clock.stop();
            }
        }
    }
    if (moved) {
        bump();
    }
    return moved;
}

void ScenePresenter::settle()
{
    const bool was_fading = is_fading();
    for (Layer& layer : layers_) {
        layer.content_clock.stop();
        layer.background_clock.stop();
        layer.content.finish();
        layer.background.finish();
    }
    if (was_fading) {
        bump();  // once settled, so a watcher finds it so
    }
}

void ScenePresenter::set_fade_progress_for_test(double t)
{
    for (Layer& layer : layers_) {
        layer.content_clock.stop();
        layer.background_clock.stop();
        // ``t`` of the whole change: the transition's part as it is (not
        // eased), and the builds by the time that is.
        layer.elapsed_ms = t * layer.total_ms;
        if (t >= 1.0) {
            layer.content.set_progress(1.0);
        } else {
            layer.content.hold_progress(
                layer.slide_ms > 0 ? std::min(1.0, layer.elapsed_ms / layer.slide_ms) : 1.0);
        }
        layer.background.set_progress(t);
    }
    bump();
}

QImage ScenePresenter::surface(QSize size, const QColor& letterbox)
{
    auto snap = snapshot();
    const QRgb key = letterbox.rgba();
    return compositor_->wait_for([compositor = compositor_.get(), snap, size, key] {
        return compositor->composite(*snap, size, key);
    });
}

void ScenePresenter::request(QSize size, const QColor& letterbox, QObject* context,
                             std::function<void(QImage)> done)
{
    auto snap = snapshot();
    const QRgb key = letterbox.rgba();
    QPointer<QObject> guard(context);
    compositor_->post([compositor = compositor_.get(), snap, size, key, guard,
                       done = std::move(done)]() mutable {
        QImage picture = compositor->composite(*snap, size, key);
        // Back on the application's thread, which ``context`` lives on, and
        // asked there whether it has gone. Posted to ``context`` from here,
        // it could go between the asking and the posting.
        compositor->hand_back(
            [guard, done = std::move(done), picture = std::move(picture)]() mutable {
                if (!guard.isNull()) {
                    done(std::move(picture));
                }
            });
    });
}

bool ScenePresenter::frozen() const noexcept
{
    return whole_ && whole_->is_frozen;
}

void ScenePresenter::set_static_root(std::filesystem::path root)
{
    cache_->set_static_root(std::move(root));
}

void ScenePresenter::share_image_cache(std::shared_ptr<BackgroundImageCache> cache)
{
    if (cache && cache != cache_) {
        cache_ = std::move(cache);
        bump();
    }
}

void ScenePresenter::set_background_video_frame(QImage frame)
{
    if (frame.cacheKey() == bg_video_frame_.cacheKey() || frozen()) {
        return;
    }
    bg_video_frame_ = std::move(frame);
    // As for the clip: every output is handed the one background.
    const auto* current = current_frame();
    if (current == nullptr || current->background.type == layout::BackgroundType::Video) {
        bump();
    }
}

void ScenePresenter::set_source_frames(const SourceFrames& frames)
{
    // Kept while frozen, and shown when the output thaws (``present``): a
    // source that changed during the freeze and then stood still would draw
    // nothing new to show it by.
    latest_source_frames_ = frames;
    if (take_source_frames()) {
        bump();
    }
}

bool ScenePresenter::take_source_frames()
{
    // A frozen layer (a stack's freeze, or the output's, which freezes every
    // layer) holds the pictures of the sources it shows; every other source is
    // painted as it is now. So a frozen lower third over a playing video holds
    // itself, not the video under it.
    std::vector<std::string> shown;
    std::vector<std::string> held;
    if (const auto* frame = current_frame()) {
        const auto note = [&](const scene::SceneFrame& layer) {
            auto& into = layer.is_frozen ? held : shown;
            for (auto& source : layer_sources_of(layer)) {
                into.push_back(std::move(source));
            }
        };
        note(*frame);
        for (const auto& below : frame->stacked_below) {
            note(*below);
        }
    }
    SourceFrames next = latest_source_frames_;
    for (const auto& source : held) {
        if (const auto had = source_frames_.find(source); had != source_frames_.end()) {
            next.insert_or_assign(source, had->second);
        } else {
            next.erase(source);
        }
    }
    // Only the sources this output shows moving decide whether its picture
    // moved: every output is handed every source, and one ticking on one
    // output must not make the others re-composite.
    bool moved = false;
    const auto key_of = [](const SourceFrames& frames, const std::string& source) {
        const auto it = frames.find(source);
        return it != frames.end() ? it->second.image.cacheKey() : 0;
    };
    for (const auto& source : shown) {
        moved = moved || key_of(source_frames_, source) != key_of(next, source);
    }
    source_frames_ = std::move(next);
    return moved;
}

}  // namespace seventhslide::render
