// Cross-check src/render/include/seventhslide/render/ndi_abi.hpp against the
// genuine NDI SDK headers.
//
// This translation unit is compiled ONLY when the developer opts in:
//
//     cmake -B build -DSEVENTHSLIDE_NDI_VERIFY_ABI=ON -DNDI_SDK_DIR=/path/to/sdk
//
// It is never part of the normal build, never part of CI, and produces no
// object code that ships. It exists so that our hand-written ABI shim - which
// lets SeventhSlide avoid vendoring or redistributing any NDI SDK material -
// can be proven correct on a machine that does have the SDK installed.
//
// Run it once after touching ndi_abi.hpp. Everything here is compile-time; a
// successful compile is a passing check.

#include <seventhslide/render/ndi_abi.hpp>

#include <Processing.NDI.Lib.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ss = seventhslide::render::ndi;

// ---------------------------------------------------------------------------
// NDIlib_send_create_t
// ---------------------------------------------------------------------------
static_assert(sizeof(ss::send_create_t) == sizeof(NDIlib_send_create_t),
              "send_create_t size drifted from the SDK");
static_assert(offsetof(ss::send_create_t, p_ndi_name) == offsetof(NDIlib_send_create_t, p_ndi_name));
static_assert(offsetof(ss::send_create_t, p_groups) == offsetof(NDIlib_send_create_t, p_groups));
static_assert(offsetof(ss::send_create_t, clock_video) == offsetof(NDIlib_send_create_t, clock_video));
static_assert(offsetof(ss::send_create_t, clock_audio) == offsetof(NDIlib_send_create_t, clock_audio));

// ---------------------------------------------------------------------------
// NDIlib_video_frame_v2_t
// ---------------------------------------------------------------------------
static_assert(sizeof(ss::video_frame_v2_t) == sizeof(NDIlib_video_frame_v2_t),
              "video_frame_v2_t size drifted from the SDK");
static_assert(offsetof(ss::video_frame_v2_t, xres) == offsetof(NDIlib_video_frame_v2_t, xres));
static_assert(offsetof(ss::video_frame_v2_t, yres) == offsetof(NDIlib_video_frame_v2_t, yres));
static_assert(offsetof(ss::video_frame_v2_t, FourCC) == offsetof(NDIlib_video_frame_v2_t, FourCC));
static_assert(offsetof(ss::video_frame_v2_t, frame_rate_N)
              == offsetof(NDIlib_video_frame_v2_t, frame_rate_N));
static_assert(offsetof(ss::video_frame_v2_t, frame_rate_D)
              == offsetof(NDIlib_video_frame_v2_t, frame_rate_D));
static_assert(offsetof(ss::video_frame_v2_t, picture_aspect_ratio)
              == offsetof(NDIlib_video_frame_v2_t, picture_aspect_ratio));
static_assert(offsetof(ss::video_frame_v2_t, frame_format_type)
              == offsetof(NDIlib_video_frame_v2_t, frame_format_type));
static_assert(offsetof(ss::video_frame_v2_t, timecode)
              == offsetof(NDIlib_video_frame_v2_t, timecode));
static_assert(offsetof(ss::video_frame_v2_t, p_data) == offsetof(NDIlib_video_frame_v2_t, p_data));
static_assert(offsetof(ss::video_frame_v2_t, line_stride_in_bytes)
              == offsetof(NDIlib_video_frame_v2_t, line_stride_in_bytes));
static_assert(offsetof(ss::video_frame_v2_t, p_metadata)
              == offsetof(NDIlib_video_frame_v2_t, p_metadata));
static_assert(offsetof(ss::video_frame_v2_t, timestamp)
              == offsetof(NDIlib_video_frame_v2_t, timestamp));

// ---------------------------------------------------------------------------
// NDIlib_audio_frame_v3_t
// ---------------------------------------------------------------------------
static_assert(sizeof(ss::audio_frame_v3_t) == sizeof(NDIlib_audio_frame_v3_t),
              "audio_frame_v3_t size drifted from the SDK");
static_assert(offsetof(ss::audio_frame_v3_t, sample_rate)
              == offsetof(NDIlib_audio_frame_v3_t, sample_rate));
static_assert(offsetof(ss::audio_frame_v3_t, no_channels)
              == offsetof(NDIlib_audio_frame_v3_t, no_channels));
static_assert(offsetof(ss::audio_frame_v3_t, no_samples)
              == offsetof(NDIlib_audio_frame_v3_t, no_samples));
static_assert(offsetof(ss::audio_frame_v3_t, timecode)
              == offsetof(NDIlib_audio_frame_v3_t, timecode));
static_assert(offsetof(ss::audio_frame_v3_t, FourCC) == offsetof(NDIlib_audio_frame_v3_t, FourCC));
static_assert(offsetof(ss::audio_frame_v3_t, p_data) == offsetof(NDIlib_audio_frame_v3_t, p_data));
static_assert(offsetof(ss::audio_frame_v3_t, channel_stride_in_bytes)
              == offsetof(NDIlib_audio_frame_v3_t, channel_stride_in_bytes));
static_assert(offsetof(ss::audio_frame_v3_t, p_metadata)
              == offsetof(NDIlib_audio_frame_v3_t, p_metadata));
static_assert(offsetof(ss::audio_frame_v3_t, timestamp)
              == offsetof(NDIlib_audio_frame_v3_t, timestamp));

// ---------------------------------------------------------------------------
// NDIlib_tally_t
// ---------------------------------------------------------------------------
static_assert(sizeof(ss::tally_t) == sizeof(NDIlib_tally_t));
static_assert(offsetof(ss::tally_t, on_program) == offsetof(NDIlib_tally_t, on_program));
static_assert(offsetof(ss::tally_t, on_preview) == offsetof(NDIlib_tally_t, on_preview));

// ---------------------------------------------------------------------------
// Enum / constant values
// ---------------------------------------------------------------------------
static_assert(ss::k_fourcc_bgra == static_cast<std::int32_t>(NDIlib_FourCC_video_type_BGRA));
static_assert(ss::k_fourcc_bgrx == static_cast<std::int32_t>(NDIlib_FourCC_video_type_BGRX));
static_assert(ss::k_fourcc_rgba == static_cast<std::int32_t>(NDIlib_FourCC_video_type_RGBA));
static_assert(ss::k_fourcc_rgbx == static_cast<std::int32_t>(NDIlib_FourCC_video_type_RGBX));
static_assert(ss::k_fourcc_uyvy == static_cast<std::int32_t>(NDIlib_FourCC_video_type_UYVY));
static_assert(ss::k_fourcc_fltp == static_cast<std::int32_t>(NDIlib_FourCC_audio_type_FLTP));

static_assert(ss::k_format_progressive
              == static_cast<std::int32_t>(NDIlib_frame_format_type_progressive));
static_assert(ss::k_format_interleaved
              == static_cast<std::int32_t>(NDIlib_frame_format_type_interleaved));
static_assert(ss::k_format_field_0 == static_cast<std::int32_t>(NDIlib_frame_format_type_field_0));
static_assert(ss::k_format_field_1 == static_cast<std::int32_t>(NDIlib_frame_format_type_field_1));

static_assert(ss::k_timecode_synthesize == NDIlib_send_timecode_synthesize);

// The struct fields are typed as int32_t on our side; make sure the SDK's
// enums really are 4 bytes and not promoted to something wider.
static_assert(sizeof(NDIlib_FourCC_video_type_e) == 4);
static_assert(sizeof(NDIlib_frame_format_type_e) == 4);
static_assert(sizeof(NDIlib_FourCC_audio_type_e) == 4);

// ---------------------------------------------------------------------------
// Function signatures
//
// The SDK spells the sender handle as a pointer to an opaque struct and uses
// its own struct tags; our shim uses void* and our own tags. Those are
// ABI-identical (and the layouts are proven above), so we normalize the
// spelling and then compare the whole signature. That still catches the
// mistakes that actually corrupt a call: wrong return type, wrong arity, or a
// wrong scalar parameter type.
// ---------------------------------------------------------------------------
namespace {

template <typename T>
struct normalize {
    using type = T;
};
template <>
struct normalize<NDIlib_send_instance_t> {
    using type = ss::send_instance_t;
};
template <>
struct normalize<const NDIlib_send_create_t*> {
    using type = const ss::send_create_t*;
};
template <>
struct normalize<const NDIlib_video_frame_v2_t*> {
    using type = const ss::video_frame_v2_t*;
};
template <>
struct normalize<const NDIlib_audio_frame_v3_t*> {
    using type = const ss::audio_frame_v3_t*;
};
template <>
struct normalize<NDIlib_tally_t*> {
    using type = ss::tally_t*;
};

template <typename T>
struct normalized_fn;
template <typename R, typename... A>
struct normalized_fn<R (*)(A...)> {
    using type = typename normalize<R>::type (*)(typename normalize<A>::type...);
};
template <typename T>
using normalized_fn_t = typename normalized_fn<T>::type;

}  // namespace

// The handle substitution above is only sound if the handle really is a
// plain pointer.
static_assert(std::is_pointer_v<NDIlib_send_instance_t>);
static_assert(sizeof(NDIlib_send_instance_t) == sizeof(ss::send_instance_t));

static_assert(std::is_same_v<decltype(ss::Api::initialize),
                             normalized_fn_t<decltype(&NDIlib_initialize)>>);
static_assert(std::is_same_v<decltype(ss::Api::destroy),
                             normalized_fn_t<decltype(&NDIlib_destroy)>>);
static_assert(std::is_same_v<decltype(ss::Api::version),
                             normalized_fn_t<decltype(&NDIlib_version)>>);
static_assert(std::is_same_v<decltype(ss::Api::is_supported_CPU),
                             normalized_fn_t<decltype(&NDIlib_is_supported_CPU)>>);
static_assert(std::is_same_v<decltype(ss::Api::send_create),
                             normalized_fn_t<decltype(&NDIlib_send_create)>>);
static_assert(std::is_same_v<decltype(ss::Api::send_destroy),
                             normalized_fn_t<decltype(&NDIlib_send_destroy)>>);
static_assert(std::is_same_v<decltype(ss::Api::send_send_video_v2),
                             normalized_fn_t<decltype(&NDIlib_send_send_video_v2)>>);
static_assert(std::is_same_v<decltype(ss::Api::send_send_video_async_v2),
                             normalized_fn_t<decltype(&NDIlib_send_send_video_async_v2)>>);
static_assert(std::is_same_v<decltype(ss::Api::send_get_no_connections),
                             normalized_fn_t<decltype(&NDIlib_send_get_no_connections)>>);
static_assert(std::is_same_v<decltype(ss::Api::send_get_tally),
                             normalized_fn_t<decltype(&NDIlib_send_get_tally)>>);
static_assert(std::is_same_v<decltype(ss::Api::send_send_audio_v3),
                             normalized_fn_t<decltype(&NDIlib_send_send_audio_v3)>>);

// ---------------------------------------------------------------------------
// Finding and receiving (step 5's live sources)
// ---------------------------------------------------------------------------
static_assert(sizeof(ss::source_t) == sizeof(NDIlib_source_t), "source_t size drifted from the SDK");
static_assert(offsetof(ss::source_t, p_ndi_name) == offsetof(NDIlib_source_t, p_ndi_name));
static_assert(offsetof(ss::source_t, p_url_address) == offsetof(NDIlib_source_t, p_url_address));

static_assert(sizeof(ss::find_create_t) == sizeof(NDIlib_find_create_t),
              "find_create_t size drifted from the SDK");
static_assert(offsetof(ss::find_create_t, show_local_sources)
              == offsetof(NDIlib_find_create_t, show_local_sources));
static_assert(offsetof(ss::find_create_t, p_groups) == offsetof(NDIlib_find_create_t, p_groups));
static_assert(offsetof(ss::find_create_t, p_extra_ips)
              == offsetof(NDIlib_find_create_t, p_extra_ips));

static_assert(sizeof(ss::recv_create_v3_t) == sizeof(NDIlib_recv_create_v3_t),
              "recv_create_v3_t size drifted from the SDK");
static_assert(offsetof(ss::recv_create_v3_t, source_to_connect_to)
              == offsetof(NDIlib_recv_create_v3_t, source_to_connect_to));
static_assert(offsetof(ss::recv_create_v3_t, color_format)
              == offsetof(NDIlib_recv_create_v3_t, color_format));
static_assert(offsetof(ss::recv_create_v3_t, bandwidth)
              == offsetof(NDIlib_recv_create_v3_t, bandwidth));
static_assert(offsetof(ss::recv_create_v3_t, allow_video_fields)
              == offsetof(NDIlib_recv_create_v3_t, allow_video_fields));
static_assert(offsetof(ss::recv_create_v3_t, p_ndi_recv_name)
              == offsetof(NDIlib_recv_create_v3_t, p_ndi_recv_name));
static_assert(ss::k_recv_color_bgrx_bgra == NDIlib_recv_color_format_BGRX_BGRA);
static_assert(ss::k_recv_bandwidth_lowest == NDIlib_recv_bandwidth_lowest);
static_assert(ss::k_recv_bandwidth_highest == NDIlib_recv_bandwidth_highest);

static_assert(sizeof(ss::metadata_frame_t) == sizeof(NDIlib_metadata_frame_t),
              "metadata_frame_t size drifted from the SDK");
static_assert(offsetof(ss::metadata_frame_t, length) == offsetof(NDIlib_metadata_frame_t, length));
static_assert(offsetof(ss::metadata_frame_t, timecode)
              == offsetof(NDIlib_metadata_frame_t, timecode));
static_assert(offsetof(ss::metadata_frame_t, p_data) == offsetof(NDIlib_metadata_frame_t, p_data));

static_assert(ss::k_frame_none == NDIlib_frame_type_none);
static_assert(ss::k_frame_video == NDIlib_frame_type_video);
static_assert(ss::k_frame_audio == NDIlib_frame_type_audio);
static_assert(ss::k_frame_metadata == NDIlib_frame_type_metadata);
static_assert(ss::k_frame_error == NDIlib_frame_type_error);
static_assert(ss::k_frame_status_change == NDIlib_frame_type_status_change);

static_assert(std::is_same_v<decltype(&NDIlib_find_create_v2),
                             NDIlib_find_instance_t (*)(const NDIlib_find_create_t*)>);
static_assert(std::is_same_v<decltype(&NDIlib_recv_capture_v3),
                             NDIlib_frame_type_e (*)(NDIlib_recv_instance_t, NDIlib_video_frame_v2_t*,
                                                     NDIlib_audio_frame_v3_t*,
                                                     NDIlib_metadata_frame_t*, uint32_t)>);

