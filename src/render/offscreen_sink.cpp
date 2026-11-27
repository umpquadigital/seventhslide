#include <seventhslide/render/offscreen_sink.hpp>

#include <seventhslide/render/destination.hpp>

#include <QTimer>

#include <algorithm>
#include <utility>

namespace seventhslide::render {

int even_dim(int n)
{
    n = std::max(2, n);
    return n & ~1;
}

OffscreenSink::OffscreenSink(QObject* parent)
    : QObject(parent)
    , own_presenter_(std::make_unique<ScenePresenter>())
    , presenter_(own_presenter_.get())
{
    timer_ = new QTimer(this);
    timer_->setInterval(tick_interval_ms());
    connect(timer_, &QTimer::timeout, this, [this]() { tick(); });
}

OffscreenSink::~OffscreenSink()
{
    // Subclasses call stop() from their own destructor; on_stop() cannot be
    // dispatched from here. Just make sure the timer is quiet.
    if (timer_ != nullptr) {
        timer_->stop();
    }
}

void OffscreenSink::set_presenter(ScenePresenter* presenter)
{
    if (presenter == nullptr || presenter == presenter_) {
        return;
    }
    presenter_ = presenter;
    own_presenter_.reset();
    // This sink's cadence may be tighter than the shared default (a DeckLink
    // output is locked to its mode rate), and the tightest one on an output
    // decides whether a transition can be painted there at all.
    presenter_->note_tick_budget_ms(tick_interval_ms());
    seen_revision_ = 0;
    mark_dirty();
}

QSize OffscreenSink::negotiate_size(QSize want) const
{
    return {even_dim(want.width() > 0 ? want.width() : geom::k_reference_canvas.width),
            even_dim(want.height() > 0 ? want.height() : geom::k_reference_canvas.height)};
}

bool OffscreenSink::on_resize(QSize size)
{
    set_output_size(size);
    mark_dirty();
    return true;
}

QSize OffscreenSink::wanted_size() const
{
    return negotiate_size(destination_send_size(full_size_, max_height_));
}

void OffscreenSink::follow_size()
{
    const QSize want = wanted_size();
    if (live_ && video_ && want != size_) {
        // A failed resize keeps the previous format rather than dropping
        // the stream out from under a live audience.
        on_resize(want);
    }
}

void OffscreenSink::sync_timer()
{
    if (live_ && video_) {
        timer_->start();
    } else {
        timer_->stop();
    }
}

void OffscreenSink::set_max_height(int max_height)
{
    max_height = std::max(0, max_height);
    if (max_height == max_height_) {
        return;
    }
    max_height_ = max_height;
    follow_size();
    if (live_) {
        tick();
    }
}

void OffscreenSink::set_video_enabled(bool on)
{
    if (on == video_) {
        return;
    }
    video_ = on;
    sync_timer();
    if (on) {
        // Back to the current picture, at the size it has now.
        mark_dirty();
        follow_size();
        tick();
    }
}

bool OffscreenSink::start(const QString& target, QSize size)
{
    stop();
    last_error_.clear();
    target_ = target;
    full_size_ = size;
    size_ = wanted_size();
    if (!on_start(size_)) {
        target_.clear();
        return false;
    }
    live_ = true;
    dirty_ = true;
    seen_revision_ = 0;
    ++run_;
    in_flight_ = false;
    again_ = false;
    timer_->setInterval(tick_interval_ms());
    presenter_->note_tick_budget_ms(tick_interval_ms());
    tick();
    sync_timer();
    return true;
}

void OffscreenSink::stop()
{
    if (timer_ != nullptr) {
        timer_->stop();
    }
    on_stop();
    live_ = false;
    target_.clear();
    ++run_;
    in_flight_ = false;
    again_ = false;
    // Settle whatever was in flight but keep the frame itself: a restart (an
    // alpha change, a format renegotiation) should come back on the current
    // slide, not replay half a fade against a stale clock. Only for a presenter
    // we own - a shared one still has live destinations drawing from it.
    if (own_presenter_) {
        own_presenter_->settle();
    }
}

void OffscreenSink::present(scene::SharedSceneFrame frame)
{
    presenter_->present(std::move(frame));
    presentation_changed();
}

void OffscreenSink::presentation_changed()
{
    const scene::SceneFrame* frame = presenter_->current_frame();
    if (frame != nullptr && frame->canvas_width > 0 && frame->canvas_height > 0) {
        full_size_ = QSize(frame->canvas_width, frame->canvas_height);
        follow_size();
    }
    if (live_) {
        tick();
    }
}

void OffscreenSink::set_fade_progress_for_test(double t)
{
    presenter_->set_fade_progress_for_test(t);
    if (live_) {
        tick();
    }
}

void OffscreenSink::set_source_frames(const SourceFrames& frames)
{
    presenter_->set_source_frames(frames);
}

void OffscreenSink::set_background_video_frame(QImage frame)
{
    presenter_->set_background_video_frame(std::move(frame));
}

void OffscreenSink::set_static_root(std::filesystem::path root)
{
    presenter_->set_static_root(std::move(root));
}

void OffscreenSink::tick()
{
    if (!live_ || !video_) {
        return;
    }
    // While a transition runs the presentation moves on every step, so
    // ``changed`` stays true for its duration and a sink with an expensive
    // encode re-encodes for exactly that long.
    presenter_->advance();
    if (in_flight_) {
        again_ = true;  // asked again once the picture in hand arrives
        return;
    }
    const std::uint64_t revision = presenter_->revision();
    const bool changed = dirty_ || revision != seen_revision_;
    dirty_ = false;
    in_flight_ = true;
    const QSize size = size_;
    // Shared with every other destination on this output that asks for the same
    // size and letterbox; the first one here composites, the rest are handed
    // the same buffer. Deliberately not held in a member afterwards: a lasting
    // shallow copy would make the presenter's next paint detach, which is
    // exactly the buffer reuse we are here for.
    presenter_->request(size, letterbox_color(), this,
                        [this, asked = Asked{run_, revision, changed, size}](QImage image) {
                            take_picture(asked, std::move(image));
                        });
}

void OffscreenSink::take_picture(const Asked& asked, QImage image)
{
    if (asked.run != run_) {
        return;  // asked for before a stop or a start
    }
    in_flight_ = false;
    // A tick came meanwhile, or the size changed: ask again now. Anything
    // else waits for the timer.
    bool again = std::exchange(again_, false) || asked.size != size_;
    if (!live_ || !video_ || asked.size != size_ || image.isNull()) {
        // Not delivered: the next one counts as changed.
        dirty_ = true;
        again = again && live_ && video_;
    } else {
        seen_revision_ = asked.revision;
        deliver(image, asked.changed);
    }
    if (again) {
        tick();
    }
}

}  // namespace seventhslide::render
