#pragma once

// A test's own NDI receiver, through the runtime the app found
// (ndi_runtime.hpp): enough of the receive side of the NDI C ABI to find a
// source on this machine and take its audio. Like ndi_abi.hpp it declares
// the ABI by hand, so the tests need no SDK; the layouts were checked against
// the SDK's headers the same way (see ndi_abi_check.cpp).

#include <seventhslide/render/ndi_abi.hpp>
#include <seventhslide/render/ndi_runtime.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace seventhslide::test {

namespace ndi_recv {

using find_instance_t = void*;
using recv_instance_t = void*;

/// ``NDIlib_source_t``.
struct source_t {
    const char* p_ndi_name = nullptr;
    const char* p_url_address = nullptr;
};

/// ``NDIlib_find_create_t``.
struct find_create_t {
    bool show_local_sources = true;
    const char* p_groups = nullptr;
    const char* p_extra_ips = nullptr;
};

/// ``NDIlib_recv_bandwidth_e``.
inline constexpr std::int32_t k_bandwidth_audio_only = 10;
inline constexpr std::int32_t k_bandwidth_highest = 100;

/// ``NDIlib_recv_create_v3_t``.
struct recv_create_v3_t {
    source_t source_to_connect_to;
    std::int32_t color_format = 100;  // NDIlib_recv_color_format_fastest
    std::int32_t bandwidth = k_bandwidth_audio_only;
    bool allow_video_fields = true;
    const char* p_ndi_recv_name = nullptr;
};

/// ``NDIlib_frame_type_e``.
inline constexpr std::int32_t k_frame_video = 1;
inline constexpr std::int32_t k_frame_audio = 2;
inline constexpr std::int32_t k_frame_error = 4;

static_assert(sizeof(source_t) == 16);
static_assert(sizeof(find_create_t) == 24);
static_assert(sizeof(recv_create_v3_t) == 40);
static_assert(offsetof(recv_create_v3_t, color_format) == 16);
static_assert(offsetof(recv_create_v3_t, bandwidth) == 20);
static_assert(offsetof(recv_create_v3_t, allow_video_fields) == 24);
static_assert(offsetof(recv_create_v3_t, p_ndi_recv_name) == 32);

}  // namespace ndi_recv

/// Receives one NDI source on this machine: its sound, and if asked its
/// pictures.
class NdiTestReceiver {
public:
    NdiTestReceiver()
    {
        const auto path = render::ndi_runtime_path();
        if (path.isEmpty() || render::ndi_api() == nullptr) {
            return;
        }
#if defined(_WIN32)
        lib_ = ::LoadLibraryW(reinterpret_cast<const wchar_t*>(path.utf16()));
#else
        lib_ = ::dlopen(path.toUtf8().constData(), RTLD_LAZY | RTLD_LOCAL);
#endif
        if (lib_ == nullptr) {
            return;
        }
        find_create_ = resolve<decltype(find_create_)>("NDIlib_find_create_v2");
        find_destroy_ = resolve<decltype(find_destroy_)>("NDIlib_find_destroy");
        find_wait_ = resolve<decltype(find_wait_)>("NDIlib_find_wait_for_sources");
        find_sources_ = resolve<decltype(find_sources_)>("NDIlib_find_get_current_sources");
        recv_create_ = resolve<decltype(recv_create_)>("NDIlib_recv_create_v3");
        recv_destroy_ = resolve<decltype(recv_destroy_)>("NDIlib_recv_destroy");
        recv_capture_ = resolve<decltype(recv_capture_)>("NDIlib_recv_capture_v3");
        recv_free_audio_ = resolve<decltype(recv_free_audio_)>("NDIlib_recv_free_audio_v3");
        recv_free_video_ = resolve<decltype(recv_free_video_)>("NDIlib_recv_free_video_v2");
        ready_ = find_create_ && find_destroy_ && find_wait_ && find_sources_ && recv_create_
                 && recv_destroy_ && recv_capture_ && recv_free_audio_ && recv_free_video_;
    }

    ~NdiTestReceiver()
    {
        if (recv_ != nullptr) {
            recv_destroy_(recv_);
        }
        if (find_ != nullptr) {
            find_destroy_(find_);
        }
#if defined(_WIN32)
        if (lib_ != nullptr) {
            ::FreeLibrary(static_cast<HMODULE>(lib_));
        }
#else
        if (lib_ != nullptr) {
            ::dlclose(lib_);
        }
#endif
    }

    NdiTestReceiver(const NdiTestReceiver&) = delete;
    NdiTestReceiver& operator=(const NdiTestReceiver&) = delete;

    [[nodiscard]] bool ready() const noexcept { return ready_; }

    /// Find the source whose name ends "(<source_name>)" and connect to it,
    /// for its sound alone unless ``video``. False when it does not appear
    /// within ``timeout``.
    bool connect(std::string_view source_name, std::chrono::milliseconds timeout,
                 bool video = false)
    {
        if (!ready_) {
            return false;
        }
        const ndi_recv::find_create_t create;
        find_ = find_create_(&create);
        if (find_ == nullptr) {
            return false;
        }
        const std::string wanted = "(" + std::string(source_name) + ")";
        const auto until = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < until) {
            find_wait_(find_, 200);
            std::uint32_t count = 0;
            const ndi_recv::source_t* sources = find_sources_(find_, &count);
            for (std::uint32_t i = 0; i < count; ++i) {
                const std::string_view name = sources[i].p_ndi_name ? sources[i].p_ndi_name : "";
                if (name.ends_with(wanted)) {
                    ndi_recv::recv_create_v3_t settings;
                    settings.source_to_connect_to = sources[i];
                    settings.bandwidth = video ? ndi_recv::k_bandwidth_highest
                                               : ndi_recv::k_bandwidth_audio_only;
                    recv_ = recv_create_(&settings);
                    return recv_ != nullptr;
                }
            }
        }
        return false;
    }

    /// What one audio frame said about itself, and its samples interleaved.
    struct Audio {
        int sample_rate = 0;
        int channels = 0;
        std::int32_t fourcc = 0;
        std::vector<float> interleaved;
        /// Each frame's length and timecode, in the order they came.
        std::vector<int> frame_samples;
        std::vector<std::int64_t> timecodes;
    };

    /// Audio frames until ``frames`` frames have come or ``timeout`` passes.
    /// The format is the last frame's.
    Audio capture(std::size_t frames, std::chrono::milliseconds timeout)
    {
        Audio out;
        const auto until = std::chrono::steady_clock::now() + timeout;
        while (out.interleaved.size() / 2 < frames && std::chrono::steady_clock::now() < until) {
            render::ndi::audio_frame_v3_t frame;
            const auto type = recv_capture_(recv_, nullptr, &frame, nullptr, 100);
            if (type == ndi_recv::k_frame_error) {
                break;
            }
            if (type != ndi_recv::k_frame_audio) {
                continue;
            }
            out.sample_rate = frame.sample_rate;
            out.channels = frame.no_channels;
            out.fourcc = frame.FourCC;
            out.frame_samples.push_back(frame.no_samples);
            out.timecodes.push_back(frame.timecode);
            if (frame.FourCC == render::ndi::k_fourcc_fltp && frame.no_channels == 2) {
                const auto* data = reinterpret_cast<const float*>(frame.p_data);
                const std::size_t stride = static_cast<std::size_t>(frame.channel_stride_in_bytes)
                                           / sizeof(float);
                for (int i = 0; i < frame.no_samples; ++i) {
                    out.interleaved.push_back(data[i]);
                    out.interleaved.push_back(data[stride + static_cast<std::size_t>(i)]);
                }
            }
            recv_free_audio_(recv_, &frame);
        }
        return out;
    }

    /// What came in over a while: frames of each kind, and the last
    /// picture's size.
    struct Seen {
        int video_frames = 0;
        int width = 0;
        int height = 0;
        int audio_frames = 0;
    };

    /// Every frame for ``duration`` (connected with ``video`` for pictures),
    /// calling ``between`` after each short wait so the caller's event loop,
    /// which sends the pictures, runs.
    template <typename Between>
    Seen watch(std::chrono::milliseconds duration, Between between)
    {
        Seen out;
        const auto until = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < until) {
            render::ndi::video_frame_v2_t video;
            render::ndi::audio_frame_v3_t audio;
            const auto type = recv_capture_(recv_, &video, &audio, nullptr, 10);
            if (type == ndi_recv::k_frame_error) {
                break;
            }
            if (type == ndi_recv::k_frame_video) {
                ++out.video_frames;
                out.width = video.xres;
                out.height = video.yres;
                recv_free_video_(recv_, &video);
            } else if (type == ndi_recv::k_frame_audio) {
                ++out.audio_frames;
                recv_free_audio_(recv_, &audio);
            }
            between();
        }
        return out;
    }

private:
    template <typename Fn>
    Fn resolve(const char* name)
    {
#if defined(_WIN32)
        // Through void(*)() first: see resolve() in src/render/ndi_runtime.cpp.
        return reinterpret_cast<Fn>(
            reinterpret_cast<void (*)()>(::GetProcAddress(static_cast<HMODULE>(lib_), name)));
#else
        return reinterpret_cast<Fn>(::dlsym(lib_, name));
#endif
    }

    void* lib_ = nullptr;
    bool ready_ = false;
    ndi_recv::find_instance_t find_ = nullptr;
    ndi_recv::recv_instance_t recv_ = nullptr;

    ndi_recv::find_instance_t (*find_create_)(const ndi_recv::find_create_t*) = nullptr;
    void (*find_destroy_)(ndi_recv::find_instance_t) = nullptr;
    bool (*find_wait_)(ndi_recv::find_instance_t, std::uint32_t) = nullptr;
    const ndi_recv::source_t* (*find_sources_)(ndi_recv::find_instance_t, std::uint32_t*) = nullptr;
    ndi_recv::recv_instance_t (*recv_create_)(const ndi_recv::recv_create_v3_t*) = nullptr;
    void (*recv_destroy_)(ndi_recv::recv_instance_t) = nullptr;
    std::int32_t (*recv_capture_)(ndi_recv::recv_instance_t, render::ndi::video_frame_v2_t*,
                                  render::ndi::audio_frame_v3_t*, void*, std::uint32_t) = nullptr;
    void (*recv_free_audio_)(ndi_recv::recv_instance_t, const render::ndi::audio_frame_v3_t*) =
        nullptr;
    void (*recv_free_video_)(ndi_recv::recv_instance_t, const render::ndi::video_frame_v2_t*) =
        nullptr;
};

}  // namespace seventhslide::test
