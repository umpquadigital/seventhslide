#include <seventhslide/render/ndi_receive.hpp>

#include <seventhslide/audio/audio_format.hpp>
#include <seventhslide/render/ndi_abi.hpp>
#include <seventhslide/render/ndi_runtime.hpp>

#include <algorithm>
#include <cstring>
#include <utility>

namespace seventhslide::render {
namespace {

const ndi::Api* receiving_api()
{
    const ndi::Api* api = ndi_api();
    return api != nullptr && api->receives() ? api : nullptr;
}

/// How long one wait for a frame lasts; also how soon the thread notices it
/// should stop.
constexpr std::uint32_t k_capture_wait_ms = 100;

} // namespace

bool ndi_receive_available()
{
    return receiving_api() != nullptr;
}

NdiFinder::NdiFinder()
{
    if (const auto* api = receiving_api()) {
        ndi::find_create_t create;
        create.show_local_sources = true;
        finder_ = api->find_create_v2(&create);
    }
}

NdiFinder::~NdiFinder()
{
    if (finder_ != nullptr) {
        receiving_api()->find_destroy(finder_);
    }
}

std::vector<std::string> NdiFinder::sources() const
{
    std::vector<std::string> out;
    const auto* api = receiving_api();
    if (finder_ == nullptr || api == nullptr) {
        return out;
    }
    std::uint32_t count = 0;
    const ndi::source_t* found = api->find_get_current_sources(finder_, &count);
    for (std::uint32_t i = 0; found != nullptr && i < count; ++i) {
        if (found[i].p_ndi_name != nullptr && found[i].p_ndi_name[0] != '\0') {
            out.emplace_back(found[i].p_ndi_name);
        }
    }
    return out;
}

NdiReceiver::NdiReceiver(std::string source_name, FrameReady on_frame, Bandwidth bandwidth)
    : name_(std::move(source_name))
    , on_frame_(std::move(on_frame))
{
    const auto* api = receiving_api();
    if (api == nullptr || name_.empty()) {
        return;
    }
    ndi::recv_create_v3_t create;
    create.source_to_connect_to.p_ndi_name = name_.c_str();
    create.color_format = ndi::k_recv_color_bgrx_bgra;
    create.bandwidth = bandwidth == Bandwidth::Preview ? ndi::k_recv_bandwidth_lowest
                                                       : ndi::k_recv_bandwidth_highest;
    create.allow_video_fields = false;
    create.p_ndi_recv_name = "SeventhSlide";
    receiver_ = api->recv_create_v3(&create);
    if (receiver_ != nullptr) {
        thread_ = std::thread([this] { run(); });
    }
}

NdiReceiver::~NdiReceiver()
{
    stop_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
    if (receiver_ != nullptr) {
        receiving_api()->recv_destroy(receiver_);
    }
}

QImage NdiReceiver::frame() const
{
    std::lock_guard lock(mu_);
    return frame_;
}

NdiReceiver::Stats NdiReceiver::stats() const
{
    Stats stats;
    {
        std::lock_guard lock(mu_);
        stats = stats_;
    }
    const auto* api = receiving_api();
    if (api != nullptr && api->recv_get_no_connections != nullptr && receiver_ != nullptr) {
        stats.connections = api->recv_get_no_connections(receiver_);
    }
    return stats;
}

void NdiReceiver::run()
{
    const auto* api = receiving_api();
    while (!stop_) {
        ndi::video_frame_v2_t video;
        ndi::audio_frame_v3_t audio;
        const std::int32_t type =
            api->recv_capture_v3(receiver_, &video, &audio, nullptr, k_capture_wait_ms);
        if (type == ndi::k_frame_video) {
            take_video(&video);
            api->recv_free_video_v2(receiver_, &video);
            if (on_frame_) {
                on_frame_();
            }
        } else if (type == ndi::k_frame_audio) {
            take_audio(&audio);
            api->recv_free_audio_v3(receiver_, &audio);
        }
        // Metadata is never asked for; a status change or nothing needs no
        // answer.
    }
}

void NdiReceiver::take_video(const void* data)
{
    const auto& video = *static_cast<const ndi::video_frame_v2_t*>(data);
    if (video.p_data == nullptr || video.xres <= 0 || video.yres <= 0) {
        return;
    }
    // BGRX/BGRA as asked for, little-endian: Qt's RGB32 and ARGB32. NDI's
    // alpha is straight, so a picture with it is premultiplied here, once.
    const bool alpha = video.FourCC == ndi::k_fourcc_bgra;
    if (!alpha && video.FourCC != ndi::k_fourcc_bgrx) {
        return;
    }
    const QImage wrapped(video.p_data, video.xres, video.yres, video.line_stride_in_bytes,
                         alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    QImage picture = alpha ? wrapped.convertToFormat(QImage::Format_ARGB32_Premultiplied)
                           : wrapped.copy();
    std::lock_guard lock(mu_);
    frame_ = std::move(picture);
    stats_.size = QSize(video.xres, video.yres);
    stats_.frame_rate = video.frame_rate_D > 0
                            ? static_cast<double>(video.frame_rate_N) / video.frame_rate_D
                            : 0.0;
    ++stats_.frames;
}

void NdiReceiver::take_audio(const void* data)
{
    const auto& sound = *static_cast<const ndi::audio_frame_v3_t*>(data);
    if (sound.p_data == nullptr || sound.no_samples <= 0 || sound.no_channels <= 0
        || sound.FourCC != ndi::k_fourcc_fltp) {
        return;
    }
    // Planar float, one channel after another: the first two, as stereo.
    const auto samples = static_cast<std::size_t>(sound.no_samples);
    const auto* left = reinterpret_cast<const float*>(sound.p_data);
    const auto* right = sound.no_channels > 1
                            ? reinterpret_cast<const float*>(sound.p_data
                                                             + sound.channel_stride_in_bytes)
                            : left;
    interleaved_.resize(samples * audio::k_channels);
    for (std::size_t i = 0; i < samples; ++i) {
        interleaved_[i * audio::k_channels] = left[i];
        interleaved_[i * audio::k_channels + 1] = right[i];
    }
    sound_.push(interleaved_.data(), samples, sound.sample_rate);
}

} // namespace seventhslide::render
