#include "seventhslide/audio/pipewire_source.hpp"

#include "seventhslide/audio/audio_format.hpp"
#include "seventhslide/util/strings.hpp"

#include <atomic>

#if SEVENTHSLIDE_WITH_PIPEWIRE
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include <algorithm>
#include <mutex>
#endif

namespace seventhslide::audio {

#if SEVENTHSLIDE_WITH_PIPEWIRE

namespace {

void init_pipewire()
{
    static std::once_flag once;
    std::call_once(once, [] { pw_init(nullptr, nullptr); });
}

/// A node name PipeWire and its tools take without quoting: the name
/// lower-cased, runs of anything else as one underscore ("SeventhSlide
/// Main" is seventhslide_main).
std::string node_name_for(std::string_view name)
{
    std::string out;
    bool gap = false;
    for (const char c : name) {
        if (!util::is_alnum(c)) {
            gap = !out.empty();
            continue;
        }
        if (gap) {
            out += '_';
            gap = false;
        }
        out += util::to_lower(c);
    }
    return out.empty() ? std::string("seventhslide") : out;
}

} // namespace

bool pipewire_available()
{
    init_pipewire();
    pw_main_loop* loop = pw_main_loop_new(nullptr);
    if (loop == nullptr) {
        return false;
    }
    pw_context* context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
    bool ok = false;
    if (context != nullptr) {
        pw_core* core = pw_context_connect(context, nullptr, 0);
        ok = core != nullptr;
        if (core != nullptr) {
            pw_core_disconnect(core);
        }
        pw_context_destroy(context);
    }
    pw_main_loop_destroy(loop);
    return ok;
}

struct PipeWireSource::Impl {
    explicit Impl(PipeWireSource& o)
        : owner(o)
    {
        events.version = PW_VERSION_STREAM_EVENTS;
        events.process = &Impl::on_process;
    }

    ~Impl() { close(); }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    /// PipeWire's data thread: fill the buffer it asks for.
    static void on_process(void* data)
    {
        auto* self = static_cast<Impl*>(data);
        pw_buffer* b = pw_stream_dequeue_buffer(self->stream);
        if (b == nullptr) {
            return;
        }
        spa_data& d = b->buffer->datas[0];
        if (d.data == nullptr) {
            pw_stream_queue_buffer(self->stream, b);
            return;
        }
        constexpr std::uint32_t stride = sizeof(float) * k_channels;
        std::uint32_t frames = d.maxsize / stride;
        if (b->requested != 0) {
            frames = std::min<std::uint32_t>(frames, static_cast<std::uint32_t>(b->requested));
        }
        self->owner.drift_.pull(static_cast<float*>(d.data), frames);
        d.chunk->offset = 0;
        d.chunk->stride = stride;
        d.chunk->size = frames * stride;
        self->sent += frames;
        pw_stream_queue_buffer(self->stream, b);
    }

    bool open(std::string_view name)
    {
        close();
        init_pipewire();
        loop = pw_thread_loop_new("seventhslide-mic", nullptr);
        if (loop == nullptr) {
            return false;
        }
        node = node_name_for(name);
        const std::string description(name);
        pw_properties* props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE,
            "Production", PW_KEY_MEDIA_CLASS, "Audio/Source", PW_KEY_NODE_NAME, node.c_str(),
            PW_KEY_NODE_DESCRIPTION, description.c_str(), PW_KEY_APP_NAME, "SeventhSlide",
            nullptr);
        stream = pw_stream_new_simple(pw_thread_loop_get_loop(loop), node.c_str(), props, &events,
                                      this);
        if (stream == nullptr) {
            close();
            return false;
        }
        std::uint8_t pod[1024];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(pod, sizeof(pod));
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.rate = k_sample_rate;
        info.channels = k_channels;
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
        const spa_pod* params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
        // Not linked to anything by itself: it waits, as a microphone does,
        // for an app to record from it.
        const auto flags = static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS
                                                        | PW_STREAM_FLAG_RT_PROCESS);
        if (pw_stream_connect(stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, flags, params, 1) < 0
            || pw_thread_loop_start(loop) < 0) {
            close();
            return false;
        }
        sent = 0;
        return true;
    }

    void close()
    {
        if (loop != nullptr) {
            pw_thread_loop_stop(loop);
        }
        if (stream != nullptr) {
            pw_stream_destroy(stream);
            stream = nullptr;
        }
        if (loop != nullptr) {
            pw_thread_loop_destroy(loop);
            loop = nullptr;
        }
        node.clear();
    }

    PipeWireSource& owner;
    pw_stream_events events{};
    pw_thread_loop* loop = nullptr;
    pw_stream* stream = nullptr;
    std::string node;
    std::atomic<std::uint64_t> sent{0};
};

#else // no PipeWire in this build

bool pipewire_available()
{
    return false;
}

struct PipeWireSource::Impl {
    explicit Impl(PipeWireSource& /*owner*/) {}
    bool open(std::string_view /*name*/) { return false; }
    void close() {}
    void* stream = nullptr;
    std::string node;
    std::atomic<std::uint64_t> sent{0};
};

#endif

PipeWireSource::PipeWireSource()
    : impl_(std::make_unique<Impl>(*this))
{
}

PipeWireSource::~PipeWireSource()
{
    stop();
}

bool PipeWireSource::start(Mixer& mixer, std::string output_id, std::string_view name)
{
    stop();
    if (!impl_->open(name)) {
        return false;
    }
    link_.set(&mixer, std::move(output_id), true);
    link_.join();
    return true;
}

void PipeWireSource::stop()
{
    // Nothing pushes once the link has left; then PipeWire stops pulling.
    link_.leave();
    impl_->close();
    drift_.reset();
}

bool PipeWireSource::running() const noexcept
{
    return impl_->stream != nullptr;
}

std::string PipeWireSource::node_name() const
{
    return impl_->node;
}

void PipeWireSource::push(const float* frames, std::size_t count)
{
    drift_.push(frames, count);
}

std::uint64_t PipeWireSource::frames_sent() const noexcept
{
    return impl_->sent.load();
}

} // namespace seventhslide::audio
