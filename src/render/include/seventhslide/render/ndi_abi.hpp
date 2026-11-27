#pragma once

// Minimal hand-written declaration of the NDI C ABI.
//
// WHY THIS FILE EXISTS: SeventhSlide deliberately does not vendor, link, or
// redistribute any part of the NDI SDK. The user installs the NDI runtime
// themselves; we dlopen it at run time. Declaring the handful of entry points
// we need here keeps the repository (and CI) free of SDK material entirely.
//
// The NDI C ABI has been stable across v4/v5/v6. It is still an ABI we do not
// own, so the layouts below are cross-checked against the genuine headers by
// the optional `seventhslide_ndi_abi_check` target:
//
//     cmake -B build -DSEVENTHSLIDE_NDI_VERIFY_ABI=ON -DNDI_SDK_DIR=/path/to/sdk
//
// Run that once on a machine with the SDK installed after touching this file.
// Nothing in the normal build or in CI depends on the SDK being present.
//
// 64-bit only: the struct layouts assume LP64 / LLP64 pointers.

#include <cstddef>
#include <cstdint>

namespace seventhslide::render::ndi {

/// Opaque sender handle (``NDIlib_send_instance_t``).
using send_instance_t = void*;
/// Opaque finder and receiver handles (``NDIlib_find_instance_t``,
/// ``NDIlib_recv_instance_t``).
using find_instance_t = void*;
using recv_instance_t = void*;

/// ``NDI_LIB_FOURCC(a,b,c,d)`` - little-endian packed four character code.
constexpr std::int32_t fourcc(char a, char b, char c, char d) noexcept
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<unsigned char>(a))
                                     | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8)
                                     | (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16)
                                     | (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24));
}

/// Subset of ``NDIlib_FourCC_video_type_e`` we emit.
inline constexpr std::int32_t k_fourcc_bgra = fourcc('B', 'G', 'R', 'A');
inline constexpr std::int32_t k_fourcc_bgrx = fourcc('B', 'G', 'R', 'X');
inline constexpr std::int32_t k_fourcc_rgba = fourcc('R', 'G', 'B', 'A');
inline constexpr std::int32_t k_fourcc_rgbx = fourcc('R', 'G', 'B', 'X');
inline constexpr std::int32_t k_fourcc_uyvy = fourcc('U', 'Y', 'V', 'Y');

/// ``NDIlib_FourCC_audio_type_FLTP``: planar 32-bit float, one channel after
/// another (note the lower-case p).
inline constexpr std::int32_t k_fourcc_fltp = fourcc('F', 'L', 'T', 'p');

/// ``NDIlib_frame_format_type_e``.
inline constexpr std::int32_t k_format_interleaved = 0;
inline constexpr std::int32_t k_format_progressive = 1;
inline constexpr std::int32_t k_format_field_0 = 2;
inline constexpr std::int32_t k_format_field_1 = 3;

/// ``NDIlib_send_timecode_synthesize``.
inline constexpr std::int64_t k_timecode_synthesize = INT64_MAX;

/// ``NDIlib_send_create_t``.
struct send_create_t {
    const char* p_ndi_name = nullptr;
    const char* p_groups = nullptr;
    bool clock_video = true;
    bool clock_audio = true;
};

/// ``NDIlib_video_frame_v2_t``.
struct video_frame_v2_t {
    std::int32_t xres = 0;
    std::int32_t yres = 0;
    std::int32_t FourCC = k_fourcc_bgra;
    std::int32_t frame_rate_N = 30000;
    std::int32_t frame_rate_D = 1001;
    float picture_aspect_ratio = 0.0F;  // 0 => square pixels, derived from xres/yres
    std::int32_t frame_format_type = k_format_progressive;
    std::int64_t timecode = k_timecode_synthesize;
    std::uint8_t* p_data = nullptr;
    // Union of line_stride_in_bytes / data_size_in_bytes in the SDK header.
    // Uncompressed formats (all we send) use the stride member.
    std::int32_t line_stride_in_bytes = 0;
    const char* p_metadata = nullptr;
    std::int64_t timestamp = 0;
};

/// ``NDIlib_audio_frame_v3_t``.
struct audio_frame_v3_t {
    std::int32_t sample_rate = 48000;
    std::int32_t no_channels = 2;
    std::int32_t no_samples = 0;
    std::int64_t timecode = k_timecode_synthesize;
    std::int32_t FourCC = k_fourcc_fltp;
    std::uint8_t* p_data = nullptr;
    // Union of channel_stride_in_bytes / data_size_in_bytes in the SDK
    // header. Planar float (all we send) uses the stride member.
    std::int32_t channel_stride_in_bytes = 0;
    const char* p_metadata = nullptr;
    std::int64_t timestamp = 0;
};

/// ``NDIlib_source_t``: a source as the finder lists it. The second member
/// is a union of ``p_url_address`` and the deprecated ``p_ip_address``.
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

/// ``NDIlib_recv_color_format_e``: what the receiver hands back.
inline constexpr std::int32_t k_recv_color_bgrx_bgra = 0;
/// ``NDIlib_recv_bandwidth_e``.
inline constexpr std::int32_t k_recv_bandwidth_lowest = 0;
inline constexpr std::int32_t k_recv_bandwidth_highest = 100;

/// ``NDIlib_recv_create_v3_t``.
struct recv_create_v3_t {
    source_t source_to_connect_to;
    std::int32_t color_format = k_recv_color_bgrx_bgra;
    std::int32_t bandwidth = k_recv_bandwidth_highest;
    bool allow_video_fields = false;
    const char* p_ndi_recv_name = nullptr;
};

/// ``NDIlib_metadata_frame_t``.
struct metadata_frame_t {
    std::int32_t length = 0;
    std::int64_t timecode = k_timecode_synthesize;
    char* p_data = nullptr;
};

/// ``NDIlib_frame_type_e``: what ``recv_capture_v3`` returned.
inline constexpr std::int32_t k_frame_none = 0;
inline constexpr std::int32_t k_frame_video = 1;
inline constexpr std::int32_t k_frame_audio = 2;
inline constexpr std::int32_t k_frame_metadata = 3;
inline constexpr std::int32_t k_frame_error = 4;
inline constexpr std::int32_t k_frame_status_change = 100;

/// ``NDIlib_tally_t``.
struct tally_t {
    bool on_program = false;
    bool on_preview = false;
};

// Pin our own layout so an accidental field reorder is caught at compile time
// even without the SDK present. The ABI-check target compares these same
// offsets against the genuine header.
static_assert(sizeof(void*) == 8, "The NDI ABI shim assumes 64-bit pointers.");
static_assert(sizeof(send_create_t) == 24);
static_assert(offsetof(send_create_t, p_ndi_name) == 0);
static_assert(offsetof(send_create_t, p_groups) == 8);
static_assert(offsetof(send_create_t, clock_video) == 16);
static_assert(offsetof(send_create_t, clock_audio) == 17);

static_assert(sizeof(video_frame_v2_t) == 72);
static_assert(offsetof(video_frame_v2_t, xres) == 0);
static_assert(offsetof(video_frame_v2_t, yres) == 4);
static_assert(offsetof(video_frame_v2_t, FourCC) == 8);
static_assert(offsetof(video_frame_v2_t, frame_rate_N) == 12);
static_assert(offsetof(video_frame_v2_t, frame_rate_D) == 16);
static_assert(offsetof(video_frame_v2_t, picture_aspect_ratio) == 20);
static_assert(offsetof(video_frame_v2_t, frame_format_type) == 24);
static_assert(offsetof(video_frame_v2_t, timecode) == 32);
static_assert(offsetof(video_frame_v2_t, p_data) == 40);
static_assert(offsetof(video_frame_v2_t, line_stride_in_bytes) == 48);
static_assert(offsetof(video_frame_v2_t, p_metadata) == 56);
static_assert(offsetof(video_frame_v2_t, timestamp) == 64);

static_assert(sizeof(audio_frame_v3_t) == 64);
static_assert(offsetof(audio_frame_v3_t, sample_rate) == 0);
static_assert(offsetof(audio_frame_v3_t, no_channels) == 4);
static_assert(offsetof(audio_frame_v3_t, no_samples) == 8);
static_assert(offsetof(audio_frame_v3_t, timecode) == 16);
static_assert(offsetof(audio_frame_v3_t, FourCC) == 24);
static_assert(offsetof(audio_frame_v3_t, p_data) == 32);
static_assert(offsetof(audio_frame_v3_t, channel_stride_in_bytes) == 40);
static_assert(offsetof(audio_frame_v3_t, p_metadata) == 48);
static_assert(offsetof(audio_frame_v3_t, timestamp) == 56);

static_assert(sizeof(tally_t) == 2);

static_assert(sizeof(source_t) == 16);
static_assert(offsetof(source_t, p_ndi_name) == 0);
static_assert(offsetof(source_t, p_url_address) == 8);

static_assert(sizeof(find_create_t) == 24);
static_assert(offsetof(find_create_t, show_local_sources) == 0);
static_assert(offsetof(find_create_t, p_groups) == 8);
static_assert(offsetof(find_create_t, p_extra_ips) == 16);

static_assert(sizeof(recv_create_v3_t) == 40);
static_assert(offsetof(recv_create_v3_t, source_to_connect_to) == 0);
static_assert(offsetof(recv_create_v3_t, color_format) == 16);
static_assert(offsetof(recv_create_v3_t, bandwidth) == 20);
static_assert(offsetof(recv_create_v3_t, allow_video_fields) == 24);
static_assert(offsetof(recv_create_v3_t, p_ndi_recv_name) == 32);

static_assert(sizeof(metadata_frame_t) == 24);
static_assert(offsetof(metadata_frame_t, length) == 0);
static_assert(offsetof(metadata_frame_t, timecode) == 8);
static_assert(offsetof(metadata_frame_t, p_data) == 16);

/// Entry points resolved out of the installed runtime. Every member is
/// non-null once ``NdiRuntime`` reports the runtime as available.
struct Api {
    bool (*initialize)() = nullptr;
    void (*destroy)() = nullptr;
    const char* (*version)() = nullptr;
    bool (*is_supported_CPU)() = nullptr;

    send_instance_t (*send_create)(const send_create_t*) = nullptr;
    void (*send_destroy)(send_instance_t) = nullptr;
    void (*send_send_video_v2)(send_instance_t, const video_frame_v2_t*) = nullptr;
    void (*send_send_video_async_v2)(send_instance_t, const video_frame_v2_t*) = nullptr;
    int (*send_get_no_connections)(send_instance_t, std::uint32_t timeout_ms) = nullptr;
    bool (*send_get_tally)(send_instance_t, tally_t*, std::uint32_t timeout_ms) = nullptr;
    /// Optional: a runtime without it (before NDI 4.5) sends no audio.
    void (*send_send_audio_v3)(send_instance_t, const audio_frame_v3_t*) = nullptr;

    // Finding and receiving. Optional, all together: a runtime without them
    // still sends (``receives()``).
    find_instance_t (*find_create_v2)(const find_create_t*) = nullptr;
    void (*find_destroy)(find_instance_t) = nullptr;
    const source_t* (*find_get_current_sources)(find_instance_t, std::uint32_t*) = nullptr;
    recv_instance_t (*recv_create_v3)(const recv_create_v3_t*) = nullptr;
    void (*recv_destroy)(recv_instance_t) = nullptr;
    std::int32_t (*recv_capture_v3)(recv_instance_t, video_frame_v2_t*, audio_frame_v3_t*,
                                    metadata_frame_t*, std::uint32_t timeout_ms) = nullptr;
    void (*recv_free_video_v2)(recv_instance_t, const video_frame_v2_t*) = nullptr;
    void (*recv_free_audio_v3)(recv_instance_t, const audio_frame_v3_t*) = nullptr;
    int (*recv_get_no_connections)(recv_instance_t) = nullptr;

    [[nodiscard]] bool receives() const noexcept
    {
        return find_create_v2 != nullptr && find_destroy != nullptr
               && find_get_current_sources != nullptr && recv_create_v3 != nullptr
               && recv_destroy != nullptr && recv_capture_v3 != nullptr
               && recv_free_video_v2 != nullptr && recv_free_audio_v3 != nullptr;
    }
};

}  // namespace seventhslide::render::ndi
