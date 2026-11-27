#include <seventhslide/render/mpv_media_player.hpp>

#include <mpv/client.h>
#include <mpv/render.h>

#include <QCoreApplication>
#include <QMetaObject>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// MPV_EVENT_START_FILE / END_FILE carry the playlist entry they are about.
static_assert(MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(1, 108),
              "libmpv client API 1.108 or later is needed");

namespace seventhslide::render {
namespace {

// Nothing here waits for mpv. Any synchronous call waits until mpv's core is
// free, which it is not while a file opens, after a seek, or as a looping
// clip goes round (0.2 s each, measured), and the window's thread, which
// calls on every tick, paints every output. So the player keeps what mpv
// last said (its property changes, as events) and answers from that, and it
// sends every change as an asynchronous request, one at a time and in order
// (client.h: requests in flight together "may be reordered arbitrarily").

/// The properties the player keeps, each by the number its change events
/// carry.
enum Observed : std::uint64_t {
    k_dwidth = 1,
    k_dheight,
    k_params_w,
    k_params_h,
    k_time_pos,
    k_duration,
    k_fps,
    k_pause,
    k_eof,
    k_core_idle,
};

struct ObservedProperty {
    Observed id;
    const char* name;
    mpv_format format;
};

constexpr ObservedProperty k_observed[] = {
    {k_dwidth, "dwidth", MPV_FORMAT_INT64},
    {k_dheight, "dheight", MPV_FORMAT_INT64},
    {k_params_w, "video-params/w", MPV_FORMAT_INT64},
    {k_params_h, "video-params/h", MPV_FORMAT_INT64},
    {k_time_pos, "time-pos", MPV_FORMAT_DOUBLE},
    {k_duration, "duration", MPV_FORMAT_DOUBLE},
    {k_fps, "container-fps", MPV_FORMAT_DOUBLE},
    {k_pause, "pause", MPV_FORMAT_FLAG},
    {k_eof, "eof-reached", MPV_FORMAT_FLAG},
    {k_core_idle, "core-idle", MPV_FORMAT_FLAG},
};

/// Requests are numbered from here, apart from the observed properties.
constexpr std::uint64_t k_first_request = 1u << 16;

/// A seek whose end mpv never reports stops holding the clock after this.
/// VideoWithSound gives up waiting sooner.
constexpr auto k_restart_limit = std::chrono::seconds(2);

/// What mpv last said about the file. A value it cannot give (no file yet,
/// no video) reads 0 or false, as a failed read did.
struct State {
    int64_t dwidth = 0;
    int64_t dheight = 0;
    int64_t params_w = 0;
    int64_t params_h = 0;
    double time_pos = 0.0;
    double duration = 0.0;
    double fps = 0.0;
    bool paused = true;
    bool eof = false;
    bool idle = true;
};

/// A change for mpv: a property to set, or a command.
struct Request {
    std::uint64_t id = 0;
    std::string property;  // empty for a command
    mpv_format format = MPV_FORMAT_NONE;
    int flag = 0;
    double real = 0.0;
    std::string text;
    std::vector<std::string> command;
};

[[nodiscard]] std::optional<int64_t> entry_of(const mpv_node& result)
{
    if (result.format != MPV_FORMAT_NODE_MAP || result.u.list == nullptr) {
        return std::nullopt;
    }
    const mpv_node_list& map = *result.u.list;
    for (int i = 0; i < map.num; ++i) {
        if (std::strcmp(map.keys[i], "playlist_entry_id") == 0
            && map.values[i].format == MPV_FORMAT_INT64) {
            return map.values[i].u.int64;
        }
    }
    return std::nullopt;
}

/// Renders mpv's frames on a thread of its own. Every call on the render
/// context is made there, so none overlaps another; mpv's update callback
/// only wakes it, and the window's thread takes the newest frame finished.
class FrameRenderer {
public:
    explicit FrameRenderer(mpv_render_context* render)
        : render_(render)
        , thread_([this] { run(); })
    {
    }

    ~FrameRenderer()
    {
        {
            const std::lock_guard lock(mu_);
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }

    FrameRenderer(const FrameRenderer&) = delete;
    FrameRenderer& operator=(const FrameRenderer&) = delete;

    /// The newest frame, and the size it should be drawn at from now on: a
    /// size other than the last one asked for is rendered again, so the
    /// frame comes at that size a moment later.
    [[nodiscard]] QImage take(QSize size)
    {
        const std::lock_guard lock(mu_);
        if (size != size_) {
            size_ = size;
            resized_ = true;
            wake_.notify_one();
        }
        return frame_;
    }

    [[nodiscard]] QImage latest() const
    {
        const std::lock_guard lock(mu_);
        return frame_;
    }

    /// No frame until the next one (another file): one being rendered now
    /// is dropped.
    void clear()
    {
        const std::lock_guard lock(mu_);
        frame_ = QImage();
        ++generation_;
    }

private:
    /// From mpv's threads: nothing but a wake (render.h forbids more).
    static void on_update(void* data)
    {
        auto* self = static_cast<FrameRenderer*>(data);
        {
            const std::lock_guard lock(self->mu_);
            self->updated_ = true;
        }
        self->wake_.notify_one();
    }

    void run()
    {
        mpv_render_context_set_update_callback(render_, &FrameRenderer::on_update, this);
        std::unique_lock lock(mu_);
        for (;;) {
            wake_.wait(lock, [this] { return stop_ || updated_ || resized_; });
            if (stop_) {
                break;
            }
            updated_ = false;
            const bool resized = std::exchange(resized_, false);
            const QSize size = size_;
            const std::uint64_t generation = generation_;
            lock.unlock();

            // Called after every update callback, as render.h asks.
            const bool fresh =
                (mpv_render_context_update(render_) & MPV_RENDER_UPDATE_FRAME) != 0;
            QImage img;
            if ((fresh || resized) && size.isValid() && !size.isEmpty()) {
                img = render(size);
            }

            lock.lock();
            if (!img.isNull() && generation == generation_) {
                frame_ = std::move(img);
            }
        }
        lock.unlock();
        // No callback runs once this returns.
        mpv_render_context_set_update_callback(render_, nullptr, nullptr);
    }

    /// The current frame at ``size``; null when mpv would not render it.
    [[nodiscard]] QImage render(QSize size)
    {
        // bgr0 matches Qt RGB32 little-endian memory layout (B,G,R,X). mpv
        // overwrites every pixel of the target, so it needs no clearing first.
        QImage img(size, QImage::Format_RGB32);
        if (img.isNull()) {
            return {};
        }
        int dims[2] = {size.width(), size.height()};
        size_t stride = static_cast<size_t>(img.bytesPerLine());
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_SW_SIZE, dims},
            {MPV_RENDER_PARAM_SW_FORMAT, const_cast<char*>("bgr0")},
            {MPV_RENDER_PARAM_SW_STRIDE, &stride},
            {MPV_RENDER_PARAM_SW_POINTER, img.bits()},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        if (mpv_render_context_render(render_, params) < 0) {
            return {};
        }
        // The X byte is undefined ("uninitialized garbage" per mpv's render.h),
        // and RGB32 promises it is 0xff. Converting sets it, in place.
        img.convertTo(QImage::Format_ARGB32_Premultiplied);
        return img;
    }

    mpv_render_context* const render_;
    mutable std::mutex mu_;
    std::condition_variable wake_;
    bool stop_ = false;
    bool updated_ = false;
    bool resized_ = false;
    QSize size_;
    std::uint64_t generation_ = 0;
    QImage frame_;
    std::thread thread_;
};

/// ``sec`` as mpv reads a time option ("12.500"), whatever the locale.
[[nodiscard]] std::string seconds_text(double sec)
{
    std::array<char, 32> buf{};
    const auto end = std::to_chars(buf.data(), buf.data() + buf.size(), sec,
                                   std::chars_format::fixed, 3)
                         .ptr;
    return std::string(buf.data(), end);
}

} // namespace

struct MpvMediaPlayer::Impl {
    /// What mpv's wakeup callback reaches: it may only post a call to the
    /// window's thread (client.h forbids anything more, and it can run on
    /// any thread, even inside one of our own calls). Cleared before the
    /// player goes.
    struct Wakeup : std::enable_shared_from_this<Wakeup> {
        std::mutex mu;
        Impl* impl = nullptr;
        std::atomic<bool> posted{false};
    };

    mpv_handle* mpv = nullptr;
    mpv_render_context* render = nullptr;
    bool loop = false;
    /// Written under ``mu``; atomic because has_media() reads it without the lock.
    std::atomic<bool> has{false};
    std::string path;
    mutable std::mutex mu;
    /// Made with ``render``, and gone before it is freed.
    std::unique_ptr<FrameRenderer> frames;
    State state;
    std::shared_ptr<Wakeup> wakeup;

    /// Changes not sent yet, in order, and the one mpv has not answered.
    std::deque<Request> requests;
    std::optional<Request> sent;
    std::uint64_t next_request = k_first_request;

    /// A load or seek was asked for and mpv has not started playing from
    /// there yet: the clock does not run, and the asked-for place stands.
    bool restarting = false;
    /// The request that began it (the seek, or the load); once mpv has
    /// answered it, the next PLAYBACK_RESTART is its own, not one left over
    /// from before.
    std::uint64_t restart_request = 0;
    bool restart_begun = false;
    std::chrono::steady_clock::time_point restart_sent;
    /// What mpv said of these once it had begun, applied when it restarts:
    /// where it really landed, which a paused seek says only once.
    std::optional<double> landed_time_pos;
    std::optional<bool> landed_eof;
    /// Started (a play, or a restart) and the picture has not moved yet. mpv
    /// says it is no longer idle up to 0.2 s before its first frame moves
    /// (measured), and the sound must wait for the picture itself.
    bool awaiting_motion = false;
    double motion_from = 0.0;
    /// The file the last load started; -1 until known. Only its failure is
    /// this player's.
    std::uint64_t load_request = 0;
    int64_t entry = -1;
    bool awaiting_start = false;
    /// The last load set mpv's ``start`` to a place in the file, and has not
    /// started there yet. ``start`` holds for every file mpv opens, and a
    /// looping file comes round to it, so it goes back to none once this one
    /// has: at its first playback restart after mpv took the load, and not
    /// on a timeout or a failed seek, which a file slow to open may see first.
    bool start_set = false;
    /// mpv has answered the last load: a playback restart now is the file's.
    bool load_answered = false;

    void expect_restart(std::uint64_t request)
    {
        restarting = true;
        restart_request = request;
        restart_begun = false;
        restart_sent = std::chrono::steady_clock::now();
        landed_time_pos.reset();
        landed_eof.reset();
    }

    void restarted()
    {
        restarting = false;
        restart_request = 0;
        if (landed_time_pos) {
            state.time_pos = *landed_time_pos;
        }
        if (landed_eof) {
            state.eof = *landed_eof;
        }
        landed_time_pos.reset();
        landed_eof.reset();
        await_motion();
    }

    void await_motion()
    {
        awaiting_motion = true;
        motion_from = state.time_pos;
    }

    /// Set ``property`` in mpv, after every change asked for before it. One
    /// not sent yet takes the new value instead (lip sync sets the speed
    /// every tick). Returns the request's number.
    std::uint64_t set(const char* property, mpv_format format, int flag, double real,
                      std::string text = {})
    {
        for (auto& waiting : requests) {
            if (waiting.property == property) {
                waiting.flag = flag;
                waiting.real = real;
                waiting.text = std::move(text);
                return waiting.id;
            }
        }
        Request r;
        r.id = next_request++;
        r.property = property;
        r.format = format;
        r.flag = flag;
        r.real = real;
        r.text = std::move(text);
        const auto id = r.id;
        requests.push_back(std::move(r));
        send_next();
        return id;
    }
    std::uint64_t set_flag(const char* property, bool on)
    {
        return set(property, MPV_FORMAT_FLAG, on ? 1 : 0, 0.0);
    }
    std::uint64_t set_double(const char* property, double value)
    {
        return set(property, MPV_FORMAT_DOUBLE, 0, value);
    }
    std::uint64_t set_string(const char* property, std::string value)
    {
        return set(property, MPV_FORMAT_STRING, 0, 0.0, std::move(value));
    }

    /// Run a command in mpv, after every change asked for before it.
    std::uint64_t run(std::vector<std::string> words)
    {
        Request r;
        r.id = next_request++;
        r.command = std::move(words);
        const auto id = r.id;
        requests.push_back(std::move(r));
        send_next();
        return id;
    }

    /// Send the next change once mpv has answered the last.
    void send_next()
    {
        while (mpv && !sent && !requests.empty()) {
            Request r = std::move(requests.front());
            requests.pop_front();
            int error = 0;
            if (r.property.empty()) {
                std::vector<const char*> argv;
                for (const auto& word : r.command) {
                    argv.push_back(word.c_str());
                }
                argv.push_back(nullptr);
                error = mpv_command_async(mpv, r.id, argv.data());
            } else if (r.format == MPV_FORMAT_FLAG) {
                error = mpv_set_property_async(mpv, r.id, r.property.c_str(), r.format, &r.flag);
            } else if (r.format == MPV_FORMAT_DOUBLE) {
                error = mpv_set_property_async(mpv, r.id, r.property.c_str(), r.format, &r.real);
            } else {
                const char* text = r.text.c_str();
                error = mpv_set_property_async(mpv, r.id, r.property.c_str(), r.format, &text);
            }
            if (error < 0) {
                answered(r.id, error, nullptr);
                continue;
            }
            sent = std::move(r);
        }
    }

    /// mpv answered request ``id``: ``error`` below 0 if it failed.
    void answered(std::uint64_t id, int error, const mpv_node* result)
    {
        if (id == restart_request && restarting) {
            if (error < 0) {
                restarted();  // no seek or load happens: nothing to wait for
            } else {
                restart_begun = true;
            }
        }
        if (id == load_request) {
            load_answered = true;
            // loadfile names the playlist entry it made; failing that, the
            // next file mpv starts is this one.
            const auto made = result != nullptr ? entry_of(*result) : std::nullopt;
            entry = made.value_or(-1);
            awaiting_start = !made && error >= 0;
            if (error < 0) {
                has = false;
            }
        }
    }

    void apply(Observed id, const mpv_event_property& prop)
    {
        const bool known = prop.format != MPV_FORMAT_NONE && prop.data != nullptr;
        const auto int64 = [&] { return known ? *static_cast<const int64_t*>(prop.data) : 0; };
        const auto real = [&] { return known ? *static_cast<const double*>(prop.data) : 0.0; };
        const auto flag = [&] { return known && *static_cast<const int*>(prop.data) != 0; };
        switch (id) {
        case k_dwidth: state.dwidth = int64(); break;
        case k_dheight: state.dheight = int64(); break;
        case k_params_w: state.params_w = int64(); break;
        case k_params_h: state.params_h = int64(); break;
        case k_time_pos:
            if (!restarting) {
                state.time_pos = real();
                if (awaiting_motion && state.time_pos != motion_from) {
                    awaiting_motion = false;
                }
            } else if (restart_begun) {
                landed_time_pos = real();
            }
            break;
        case k_duration: state.duration = real(); break;
        case k_fps: state.fps = real(); break;
        case k_pause:
            // What was asked and not yet done stands: mpv's word on the
            // pause in between is from before it.
            if (!pause_waiting()) {
                state.paused = flag();
            }
            break;
        case k_eof:
            if (!restarting) {
                state.eof = flag();
            } else if (restart_begun) {
                landed_eof = flag();
            }
            break;
        case k_core_idle: state.idle = flag(); break;
        }
    }

    /// A pause or play is asked for and mpv has not answered it yet.
    [[nodiscard]] bool pause_waiting() const
    {
        if (sent && sent->property == "pause") {
            return true;
        }
        return std::ranges::any_of(requests, [](const Request& r) { return r.property == "pause"; });
    }

    /// Take every event mpv has queued, and send what waited on its answers;
    /// returns at once (it reads this client's queue only). A file mpv
    /// failed to open drops ``has``, so callers see ``has_media() == false``
    /// instead of waiting on frames forever.
    void pump_events()
    {
        if (!mpv) {
            return;
        }
        if (wakeup) {
            wakeup->posted = false;
        }
        for (;;) {
            mpv_event* ev = mpv_wait_event(mpv, 0.0);
            if (!ev || ev->event_id == MPV_EVENT_NONE) {
                break;
            }
            switch (ev->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE:
                apply(static_cast<Observed>(ev->reply_userdata),
                      *static_cast<const mpv_event_property*>(ev->data));
                break;
            case MPV_EVENT_SET_PROPERTY_REPLY:
            case MPV_EVENT_COMMAND_REPLY:
                if (sent && ev->reply_userdata == sent->id) {
                    const auto id = sent->id;
                    sent.reset();
                    const mpv_node* result = nullptr;
                    if (ev->event_id == MPV_EVENT_COMMAND_REPLY && ev->data != nullptr) {
                        result = &static_cast<const mpv_event_command*>(ev->data)->result;
                    }
                    answered(id, ev->error, result);
                    send_next();
                }
                break;
            case MPV_EVENT_START_FILE:
                if (awaiting_start) {
                    entry = static_cast<const mpv_event_start_file*>(ev->data)->playlist_entry_id;
                    awaiting_start = false;
                }
                break;
            case MPV_EVENT_PLAYBACK_RESTART:
                if (start_set && load_answered) {
                    // The file has started where it was asked to.
                    start_set = false;
                    (void)set_string("start", "none");
                }
                if (restarting && restart_begun) {
                    restarted();
                }
                break;
            case MPV_EVENT_END_FILE: {
                const auto* end = static_cast<const mpv_event_end_file*>(ev->data);
                if (end != nullptr && end->reason == MPV_END_FILE_REASON_ERROR && entry >= 0
                    && end->playlist_entry_id == entry) {
                    has = false;
                    restarted();
                }
                break;
            }
            default:
                break;
            }
        }
        if (restarting && std::chrono::steady_clock::now() - restart_sent > k_restart_limit) {
            restarted();
        }
    }

    /// From mpv's wakeup callback, on any thread: drain the events on the
    /// window's thread soon, so what waits on an answer is sent even when
    /// nothing asks the player anything.
    static void on_wakeup(void* data)
    {
        auto* w = static_cast<Wakeup*>(data);
        auto* app = QCoreApplication::instance();
        if (app == nullptr || w->posted.exchange(true)) {
            return;
        }
        QMetaObject::invokeMethod(
            app,
            [alive = w->shared_from_this()] {
                const std::lock_guard hold(alive->mu);
                if (alive->impl != nullptr) {
                    const std::lock_guard lock(alive->impl->mu);
                    alive->impl->pump_events();
                }
            },
            Qt::QueuedConnection);
    }

    [[nodiscard]] bool ensure()
    {
        if (mpv) {
            return render != nullptr;
        }
        // Defense in depth: tests / alternate entry points may skip main()'s setlocale.
        setlocale(LC_NUMERIC, "C");

        mpv = mpv_create();
        if (!mpv) {
            return false;
        }
        mpv_set_option_string(mpv, "vo", "libmpv");
        // Hardware decode with copy-back: the software render API needs frames
        // in system memory. "-safe" limits it to methods mpv trusts and falls
        // back to software decoding when none is available.
        mpv_set_option_string(mpv, "hwdec", "auto-copy-safe");
        mpv_set_option_string(mpv, "terminal", "no");
        mpv_set_option_string(mpv, "msg-level", "all=error");
        mpv_set_option_string(mpv, "idle", "yes");
        mpv_set_option_string(mpv, "keep-open", "yes");
        mpv_set_option_string(mpv, "video-timing-offset", "0");
        // The picture only: the sound is decoded and mixed by the app, and
        // without an audio track mpv times the frames by the system clock.
        mpv_set_option_string(mpv, "aid", "no");
        if (mpv_initialize(mpv) < 0) {
            mpv_destroy(mpv);
            mpv = nullptr;
            return false;
        }
        // Observed for the player's life: they carry over from file to file.
        for (const auto& p : k_observed) {
            mpv_observe_property(mpv, p.id, p.name, p.format);
        }
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_SW)},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        if (mpv_render_context_create(&render, mpv, params) < 0) {
            render = nullptr;
            mpv_destroy(mpv);
            mpv = nullptr;
            return false;
        }
        frames = std::make_unique<FrameRenderer>(render);
        wakeup = std::make_shared<Wakeup>();
        wakeup->impl = this;
        mpv_set_wakeup_callback(mpv, &Impl::on_wakeup, wakeup.get());
        return true;
    }

    ~Impl()
    {
        if (mpv) {
            // No callback runs once this returns; a call it posted finds no
            // player.
            mpv_set_wakeup_callback(mpv, nullptr, nullptr);
        }
        if (wakeup) {
            const std::lock_guard hold(wakeup->mu);
            wakeup->impl = nullptr;
        }
        // Its thread makes the last call on the render context.
        frames.reset();
        if (render) {
            mpv_render_context_free(render);
            render = nullptr;
        }
        if (mpv) {
            mpv_destroy(mpv);
            mpv = nullptr;
        }
    }

    /// No file: what a new player says.
    void forget_file()
    {
        state = State{};
        restarting = false;
        restart_request = 0;
        restart_begun = false;
        landed_time_pos.reset();
        landed_eof.reset();
        awaiting_motion = false;
        load_request = 0;
        entry = -1;
        awaiting_start = false;
    }

    void clear_media()
    {
        if (mpv && has) {
            (void)run({"stop"});
        }
        has = false;
        path.clear();
        if (frames) {
            frames->clear();
        }
        forget_file();
    }
};

MpvMediaPlayer::MpvMediaPlayer()
    : impl_(std::make_unique<Impl>())
{
}

MpvMediaPlayer::~MpvMediaPlayer() = default;

bool MpvMediaPlayer::do_load(std::string_view path, bool loop, double start_sec)
{
    std::lock_guard lock(impl_->mu);
    if (path.empty()) {
        impl_->clear_media();
        return false;
    }
    const std::string path_str(path);
    // A file that cannot be read fails now rather than as a later mpv event.
    if (!std::ifstream(path_str, std::ios::binary).is_open()) {
        impl_->clear_media();
        return false;
    }
    if (!impl_->ensure()) {
        impl_->clear_media();
        return false;
    }
    impl_->pump_events();
    impl_->loop = loop;
    impl_->path = path_str;
    impl_->frames->clear();
    impl_->forget_file();
    if (!std::isfinite(start_sec) || start_sec < 0.0) {
        start_sec = 0.0;
    }
    // Until it has opened, the place asked for is where it stands.
    impl_->state.time_pos = start_sec;

    // Paused, looped and placed before it opens, so it never plays a moment
    // on its own nor shows a frame from elsewhere; then the file. All
    // answered later: this returns at once.
    (void)impl_->set_flag("pause", true);
    (void)impl_->set_string("loop-file", loop ? "inf" : "no");
    impl_->start_set = start_sec > 0.0;
    impl_->load_answered = false;
    (void)impl_->set_string("start", impl_->start_set ? seconds_text(start_sec) : "none");
    impl_->load_request = impl_->run({"loadfile", impl_->path, "replace"});
    impl_->expect_restart(impl_->load_request);
    impl_->has = true;
    return true;
}

void MpvMediaPlayer::play()
{
    std::lock_guard lock(impl_->mu);
    if (!impl_->mpv || !impl_->has) {
        return;
    }
    impl_->pump_events();
    if (impl_->state.paused) {
        impl_->await_motion();
    }
    (void)impl_->set_flag("pause", false);
    impl_->state.paused = false;
}

void MpvMediaPlayer::pause()
{
    std::lock_guard lock(impl_->mu);
    if (!impl_->mpv || !impl_->has) {
        return;
    }
    impl_->pump_events();
    (void)impl_->set_flag("pause", true);
    impl_->state.paused = true;
}

void MpvMediaPlayer::seek(double position_sec)
{
    std::lock_guard lock(impl_->mu);
    if (!impl_->mpv || !impl_->has) {
        return;
    }
    if (position_sec < 0.0) {
        position_sec = 0.0;
    }
    impl_->pump_events();
    impl_->expect_restart(impl_->set_double("time-pos", position_sec));
    impl_->state.time_pos = position_sec;
    impl_->state.eof = false;
}

void MpvMediaPlayer::restart()
{
    std::lock_guard lock(impl_->mu);
    if (!impl_->mpv || !impl_->has) {
        return;
    }
    impl_->pump_events();
    impl_->expect_restart(impl_->set_double("time-pos", 0.0));
    (void)impl_->set_flag("pause", false);
    impl_->state.time_pos = 0.0;
    impl_->state.paused = false;
    impl_->state.eof = false;
}

void MpvMediaPlayer::unload()
{
    std::lock_guard lock(impl_->mu);
    impl_->clear_media();
}

bool MpvMediaPlayer::has_media() const
{
    return impl_->has.load();
}

bool MpvMediaPlayer::is_playing() const
{
    std::lock_guard lock(impl_->mu);
    // The clock tick polls this. Without reading events here a player whose
    // frames nobody pulls would never learn that mpv could not open its file,
    // and would report playing forever.
    impl_->pump_events();
    if (!impl_->mpv || !impl_->has) {
        return false;
    }
    return !impl_->state.paused && !impl_->state.eof;
}

bool MpvMediaPlayer::ended() const
{
    std::lock_guard lock(impl_->mu);
    impl_->pump_events();
    return impl_->mpv && impl_->has && impl_->state.eof;
}

double MpvMediaPlayer::position_sec() const
{
    std::lock_guard lock(impl_->mu);
    impl_->pump_events();
    return impl_->state.time_pos;
}

double MpvMediaPlayer::duration_sec() const
{
    std::lock_guard lock(impl_->mu);
    impl_->pump_events();
    const double d = impl_->state.duration;
    return std::isfinite(d) && d > 0.0 ? d : 0.0;
}

bool MpvMediaPlayer::loop() const
{
    std::lock_guard lock(impl_->mu);
    return impl_->loop;
}

double MpvMediaPlayer::frame_rate() const
{
    std::lock_guard lock(impl_->mu);
    if (!impl_->has) {
        return 0.0;
    }
    impl_->pump_events();
    const double fps = impl_->state.fps;
    return std::isfinite(fps) && fps > 0.0 ? fps : 0.0;
}

bool MpvMediaPlayer::clock_running() const
{
    std::lock_guard lock(impl_->mu);
    if (!impl_->mpv || !impl_->has) {
        return false;
    }
    impl_->pump_events();
    const auto& s = impl_->state;
    return !s.paused && !s.idle && !impl_->restarting && !impl_->awaiting_motion;
}

void MpvMediaPlayer::set_speed(double speed)
{
    std::lock_guard lock(impl_->mu);
    if (!impl_->mpv) {
        return;
    }
    impl_->pump_events();
    (void)impl_->set_double("speed", speed);
}

QImage MpvMediaPlayer::current_frame(int max_w, int max_h) const
{
    std::lock_guard lock(impl_->mu);
    if (!impl_->mpv || !impl_->frames || !impl_->has) {
        return {};
    }
    impl_->pump_events();
    const auto& s = impl_->state;
    int64_t w = s.dwidth;
    int64_t h = s.dheight;
    if (w <= 0 || h <= 0) {
        w = s.params_w;
        h = s.params_h;
    }
    if (w <= 0 || h <= 0) {
        return impl_->frames->latest();
    }
    if (w > max_w) {
        h = h * max_w / w;
        w = max_w;
    }
    if (h > max_h) {
        w = w * max_h / h;
        h = max_h;
    }
    // Rendered on the renderer's thread as mpv makes each frame; this takes
    // the newest, and nothing else is done here.
    return impl_->frames->take(QSize(static_cast<int>(w), static_cast<int>(h)));
}

} // namespace seventhslide::render
