#include <seventhslide/render/output_surfaces.hpp>

#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/render/device_catalog.hpp>
#include <seventhslide/render/output_window.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/render/virtual_camera.hpp>

#if defined(Q_OS_MACOS)
#include "output_window_mac.hpp"
#endif

#include <QScreen>
#include <QThreadPool>

#include <utility>

namespace seventhslide::render {

OutputSurfaces::OutputSurfaces(QObject* parent)
    : QObject(parent)
{
}

OutputSurfaces::~OutputSurfaces()
{
    close_all();
    // Held ones too: a preview still holding one hears of it from its host's
    // destroyed().
    qDeleteAll(presenters_);
    presenters_.clear();
    // Outside any event handling now, so windows still waiting on deleteLater
    // (the event loop may already have ended) go here.
    for (const QPointer<OutputWindow>& win : std::as_const(retiring_)) {
        delete win.data();
    }
}

void OutputSurfaces::show_window(const Destination& dest, const QString& output_name,
                                 QScreen* screen)
{
    if (OutputWindow* existing = windows_.value(dest.id, nullptr)) {
        existing->set_presenter(presenter_for(dest.output_id));
        existing->set_max_height(dest.max_height);
        existing->move_to_screen(screen);
        return;
    }
    auto* win = new OutputWindow(dest.output_id, output_name, screen, dest.id);
    win->set_presenter(presenter_for(dest.output_id));
    win->set_max_height(dest.max_height);
    connect(win, &OutputWindow::closeRequested, this, &OutputSurfaces::closeRequested);
    windows_.insert(dest.id, win);
    win->create();
    win->show_full_screen();
    win->requestUpdate();
    update_presenting();
}

void OutputSurfaces::show_desktop_window(const Destination& dest, const QString& output_name,
                                         QRect geometry)
{
    if (OutputWindow* existing = windows_.value(dest.id, nullptr)) {
        existing->set_presenter(presenter_for(dest.output_id));
        existing->set_max_height(dest.max_height);
        return;
    }
    auto* win = new OutputWindow(dest.output_id, output_name, geometry, dest.id);
    win->set_presenter(presenter_for(dest.output_id));
    win->set_max_height(dest.max_height);
    connect(win, &OutputWindow::closeRequested, this, &OutputSurfaces::closeRequested);
    connect(win, &OutputWindow::placeChanged, this, &OutputSurfaces::windowPlaceChanged);
    windows_.insert(dest.id, win);
    win->create();
    win->show_on_desktop();
    win->requestUpdate();
    update_presenting();
}

bool OutputSurfaces::start_sink(const Destination& dest, OffscreenSink* sink, QSize size,
                                QString& error)
{
    // Share the output's presentation before starting, so the very first
    // composite is already the shared one.
    // The presenter carries the output's resources (media root, video, fonts).
    sink->set_presenter(presenter_for(dest.output_id));
    // Its format before it opens, so the transport opens at its own size.
    sink->set_max_height(dest.max_height);
    sink->set_video_enabled(dest.video);
    if (!sink->start(dest.target_key, size)) {
        error = sink->last_error();
        delete sink;
        return false;
    }
    sink->presentation_changed();
    connect(sink, &OffscreenSink::statusChanged, this,
            [this, id = dest.id] { emit sinkStatusChanged(id); });
    sinks_.insert(dest.id, {sink, dest.output_id});
    update_presenting();
    return true;
}

void OutputSurfaces::apply_format(const Destination& dest)
{
    if (OutputWindow* win = windows_.value(dest.id, nullptr)) {
        win->set_max_height(dest.max_height);
    }
    if (OffscreenSink* s = sink(dest.id)) {
        s->set_max_height(dest.max_height);
        s->set_video_enabled(dest.video);
    }
}

bool OutputSurfaces::close_window(const QString& dest_id)
{
    OutputWindow* win = windows_.take(dest_id);
    if (!win) {
        return false;
    }
    const QString output_id = win->output_id();
    retire_window(win);
    release_presenter_if_unused(output_id);
    update_presenting();
    return true;
}

bool OutputSurfaces::close_sink(const QString& dest_id)
{
    const Sink entry = sinks_.take(dest_id);
    if (!entry.sink) {
        return false;
    }
    // stop() ends the transport: for a DeckLink card it joins the writer thread
    // and releases the device, so another destination (or application) can use it.
    entry.sink->stop();
    delete entry.sink;
    release_presenter_if_unused(entry.output_id);
    update_presenting();
    return true;
}

void OutputSurfaces::close_all()
{
    const auto window_ids = windows_.keys();
    for (const QString& id : window_ids) {
        close_window(id);
    }
    const auto sink_ids = sinks_.keys();
    for (const QString& id : sink_ids) {
        close_sink(id);
    }
    // A preview holding a presenter still draws it; it goes with the release.
    for (auto it = presenters_.begin(); it != presenters_.end();) {
        if (holds_.value(it.key()) > 0) {
            ++it;
            continue;
        }
        delete it.value();
        it = presenters_.erase(it);
    }
}

void OutputSurfaces::update_presenting() const
{
#if defined(Q_OS_MACOS)
    // An audience is watching while any window or sink is open: a projector
    // that sleeps ten minutes into a sermon, or NDI/SRT sends App Nap slows
    // when the app is in the background, would be wrong.
    set_presenting(!windows_.isEmpty() || !sinks_.isEmpty());
#endif
}

void OutputSurfaces::retire_window(OutputWindow* win)
{
    // Never deleted here: this runs inside window event handling - Escape on
    // the window itself, or Qt closing every top-level window on quit while the
    // operator window's close handler shuts the host down - and deleting a
    // QWindow Qt is dispatching to, or iterating over, crashes the app. It
    // stops drawing now and is deleted on the next pass of the event loop.
    win->disconnect(this);
    win->set_presenter(nullptr);  // its presenter may be deleted before it is
    win->allow_close();
    win->hide();
    win->close();
    win->deleteLater();
    retiring_.removeAll(QPointer<OutputWindow>());
    retiring_.push_back(win);
}

bool OutputSurfaces::has_window(const QString& dest_id) const
{
    return windows_.value(dest_id, nullptr) != nullptr;
}

bool OutputSurfaces::has_sink(const QString& dest_id) const
{
    return sinks_.contains(dest_id);
}

OffscreenSink* OutputSurfaces::sink(const QString& dest_id) const
{
    return sinks_.value(dest_id).sink;
}

bool OutputSurfaces::is_live(const Destination& dest) const
{
    if (destination_kind_offscreen(dest.kind)) {
        const OffscreenSink* s = sink(dest.id);
        return s != nullptr && s->is_live();
    }
    return has_window(dest.id);
}

bool OutputSurfaces::is_open(const QString& output_id) const
{
    return window_for_output(output_id) != nullptr || !live_sinks(output_id).isEmpty();
}

OutputWindow* OutputSurfaces::window_for_output(const QString& output_id) const
{
    for (auto it = windows_.cbegin(); it != windows_.cend(); ++it) {
        if (it.value() && it.value()->output_id() == output_id) {
            return it.value();
        }
    }
    return nullptr;
}

bool OutputSurfaces::has_output_window(const QString& output_id) const
{
    return window_for_output(output_id) != nullptr;
}

QHash<QString, QString> OutputSurfaces::open_screens() const
{
    QHash<QString, QString> out;
    for (auto it = windows_.cbegin(); it != windows_.cend(); ++it) {
        if (OutputWindow* win = it.value(); win != nullptr && !win->desktop()) {
            out.insert(win->screen_key(), win->output_id());
        }
    }
    return out;
}

QVector<OffscreenSink*> OutputSurfaces::live_sinks(const QString& output_id) const
{
    QVector<OffscreenSink*> out;
    for (auto it = sinks_.cbegin(); it != sinks_.cend(); ++it) {
        if (it->sink != nullptr && it->sink->is_live() && it->output_id == output_id) {
            out.push_back(it->sink);
        }
    }
    return out;
}

void OutputSurfaces::retitle(const std::function<QString(const QString&)>& label)
{
    for (auto it = windows_.cbegin(); it != windows_.cend(); ++it) {
        if (OutputWindow* win = it.value()) {
            win->set_output_name(label(win->output_id()));
        }
    }
}

void OutputSurfaces::close_windows_on(const QString& screen_key)
{
    const auto ids = windows_.keys();
    for (const QString& id : ids) {
        OutputWindow* win = windows_.value(id);
        if (win && !win->desktop() && win->screen_key() == screen_key) {
            close_window(id);
        }
    }
}

void OutputSurfaces::refit()
{
    for (auto it = windows_.begin(); it != windows_.end(); ++it) {
        OutputWindow* win = it.value();
        if (!win || win->desktop()) {
            continue;
        }
        if (QScreen* s = DeviceCatalog::screen_by_key(win->screen_key())) {
            win->move_to_screen(s);
        }
    }
}

QStringList OutputSurfaces::refit_on(QScreen* screen)
{
    QStringList ids;
    for (auto it = windows_.begin(); it != windows_.end(); ++it) {
        OutputWindow* win = it.value();
        if (win != nullptr && !win->desktop() && win->screen() == screen) {
            win->move_to_screen(screen);
            ids.push_back(it.key());
        }
    }
    return ids;
}

void OutputSurfaces::restack()
{
    for (OutputWindow* win : std::as_const(windows_)) {
        if (win != nullptr) {
            win->apply_stacking();
        }
    }
}

void OutputSurfaces::forget_output(const QString& output_id)
{
    last_frames_.remove(output_id);
    if (holds_.value(output_id) == 0) {
        delete presenters_.take(output_id);
    }
}

ScenePresenter* OutputSurfaces::hold_presenter(const QString& output_id)
{
    ++holds_[output_id];
    return presenter_for(output_id);
}

void OutputSurfaces::release_presenter(const QString& output_id)
{
    const auto it = holds_.find(output_id);
    if (it == holds_.end()) {
        return;
    }
    if (--it.value() > 0) {
        return;
    }
    holds_.erase(it);
    release_presenter_if_unused(output_id);
}

QSize OutputSurfaces::canvas_size_for(const QString& output_id) const
{
    const auto it = last_frames_.constFind(output_id);
    if (it != last_frames_.cend() && (*it)->canvas_width > 0 && (*it)->canvas_height > 0) {
        // NDI carries the native canvas; no webcam-style clamp. The Blackmagic
        // sink overrides this again with its fixed mode raster.
        return {(*it)->canvas_width, (*it)->canvas_height};
    }
    return {geom::k_reference_canvas.width, geom::k_reference_canvas.height};
}

QSize OutputSurfaces::camera_size_for(const QString& output_id) const
{
    const auto it = last_frames_.constFind(output_id);
    if (it != last_frames_.cend()) {
        return clamp_camera_size(QSize((*it)->canvas_width, (*it)->canvas_height));
    }
    return QSize(geom::k_reference_canvas.width, geom::k_reference_canvas.height);
}

ScenePresenter* OutputSurfaces::presenter_for(const QString& output_id)
{
    auto it = presenters_.find(output_id);
    if (it != presenters_.end() && it.value() != nullptr) {
        return it.value();
    }
    auto* presenter = new ScenePresenter();
    presenter->share_image_cache(pictures_);
    presenter->set_font_faces(font_faces_);
    if (!last_bg_video_frame_.isNull()) {
        presenter->set_background_video_frame(last_bg_video_frame_);
    }
    presenter->set_source_frames(last_source_frames_);
    // A destination opening mid-show starts on the output's current picture.
    if (const auto last = last_frames_.constFind(output_id); last != last_frames_.cend()) {
        presenter->present(*last);
    }
    presenters_.insert(output_id, presenter);
    return presenter;
}

void OutputSurfaces::release_presenter_if_unused(const QString& output_id)
{
    if (output_id.isEmpty() || has_output_window(output_id) || holds_.value(output_id) > 0) {
        return;
    }
    for (auto it = sinks_.cbegin(); it != sinks_.cend(); ++it) {
        if (it->output_id == output_id) {
            return;  // still drawn from
        }
    }
    // Surfaces can hold a 4K image apiece; do not keep them for an output that
    // has no destination left.
    delete presenters_.take(output_id);
}

void OutputSurfaces::present(const QString& output_id, scene::SharedSceneFrame frame)
{
    // One presentation per output: its presenter holds the frame and every
    // destination drawing from it is only told that it changed. This holds
    // it too, for a destination (and so a presenter) that opens later.
    last_frames_.insert(output_id, frame);
    const auto sinks = live_sinks(output_id);
    if (!has_output_window(output_id) && sinks.isEmpty() && !presenters_.contains(output_id)) {
        return;
    }
    presenter_for(output_id)->present(std::move(frame));
    for (OffscreenSink* s : sinks) {
        s->presentation_changed();
    }
    for (auto it = windows_.begin(); it != windows_.end(); ++it) {
        OutputWindow* win = it.value();
        if (win != nullptr && win->output_id() == output_id) {
            win->presentation_changed();
        }
    }
}

void OutputSurfaces::present_all(const std::vector<scene::SharedSceneFrame>& frames)
{
    for (const auto& frame : frames) {
        present(QString::fromStdString(frame->output_id), frame);
    }
}

void OutputSurfaces::prefetch_static_urls(const std::vector<std::string>& urls, int edge)
{
    if (urls.empty()) {
        return;
    }
    // Every output draws from the one cache, so warming it once warms them
    // all; on a thread of the pool, so the window's thread decodes nothing.
    // A paint that wants a picture still being decoded waits for that decode.
    QThreadPool::globalInstance()->start([pictures = pictures_, urls, edge] {
        for (const auto& url : urls) {
            try {
                (void)pictures->ensure(url, edge);
            } catch (const std::exception&) {
                // Drawn, or found missing, when it is shown.
            }
        }
    });
}

void OutputSurfaces::set_static_root(std::filesystem::path root)
{
    static_root_ = std::move(root);
    // Every presenter draws from this cache.
    pictures_->set_static_root(static_root_);
}

void OutputSurfaces::forget_missing_pictures(const std::vector<std::string>& urls)
{
    if (!pictures_->forget_missing(urls)) {
        return;
    }
    for (ScenePresenter* presenter : presenters_) {
        if (presenter) {
            presenter->picture_arrived();
        }
    }
}

void OutputSurfaces::set_background_video_frame(QImage frame)
{
    last_bg_video_frame_ = frame;
    for (ScenePresenter* presenter : presenters_) {
        if (presenter) {
            presenter->set_background_video_frame(frame);
        }
    }
}

void OutputSurfaces::set_source_frames(std::string_view provider, const SourceFrames& frames)
{
    if (const auto it = provided_frames_.find(provider); it != provided_frames_.end()) {
        it->second = frames;
    } else {
        provided_frames_.emplace(std::string(provider), frames);
    }
    last_source_frames_.clear();
    for (const auto& [name, own] : provided_frames_) {
        last_source_frames_.insert(own.begin(), own.end());
    }
    for (ScenePresenter* presenter : presenters_) {
        if (presenter) {
            presenter->set_source_frames(last_source_frames_);
        }
    }
}

void OutputSurfaces::set_font_faces(const layout::QtFontFaces* faces)
{
    font_faces_ = faces;
    for (ScenePresenter* presenter : presenters_) {
        if (presenter) {
            presenter->set_font_faces(font_faces_);
        }
    }
}

}  // namespace seventhslide::render
