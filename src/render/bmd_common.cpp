// What DeckLink's input and output do that needs no driver: a mode's
// rate, 2vuy to RGB, and the sound kept on a card. Built on every system, so
// the tests hold it to the same numbers wherever the SDK is not wired up.

#include <seventhslide/render/bmd_input.hpp>
#include <seventhslide/render/bmd_output.hpp>
#include <seventhslide/render/bmd_runtime.hpp>
#include <seventhslide/render/pixel_pack.hpp>

#include <seventhslide/audio/audio_format.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace seventhslide::render {

double BmdDisplayModeInfo::fps() const noexcept
{
    if (frame_duration <= 0 || time_scale <= 0) {
        return 0.0;
    }
    return double(time_scale) / double(frame_duration);
}

int BmdDisplayModeInfo::tick_interval_ms() const noexcept
{
    const double f = fps();
    if (f <= 0.0) {
        return 33;  // The base class default; better than dividing by zero.
    }
    return std::clamp(int(1000.0 / f), 4, 1000);
}

QImage bmd_uyvy_to_image(const std::uint8_t* data, int width, int height, int row_bytes)
{
    if (data == nullptr || width <= 0 || height <= 0 || row_bytes < width * 2) {
        return {};
    }
    // Video range (Y 16-235, C 16-240) to full-range RGB, in 16.16 fixed
    // point, in the matrix the output packs with (video_yuv_matrix).
    const bool hd = video_yuv_matrix(height) == YuvMatrix::Bt709;
    const int ky = 76309;                     // 1.164
    const int kr = hd ? 117489 : 104597;      // 1.793 / 1.596
    const int kgu = hd ? 13975 : 25675;       // 0.213 / 0.392
    const int kgv = hd ? 34925 : 53279;       // 0.533 / 0.813
    const int kb = hd ? 138438 : 132201;      // 2.112 / 2.017
    const auto clamp8 = [](int v) { return static_cast<std::uint32_t>(std::clamp(v >> 16, 0, 255)); };
    QImage out(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* src = data + static_cast<std::ptrdiff_t>(y) * row_bytes;
        auto* dst = reinterpret_cast<std::uint32_t*>(out.scanLine(y));
        for (int x = 0; x < width; x += 2) {
            const int u = src[0] - 128;
            const int y0 = (src[1] - 16) * ky;
            const int v = src[2] - 128;
            const int y1 = (src[3] - 16) * ky;
            src += 4;
            const int r = kr * v;
            const int g = -kgu * u - kgv * v;
            const int b = kb * u;
            dst[x] = 0xff000000u | (clamp8(y0 + r) << 16) | (clamp8(y0 + g) << 8) | clamp8(y0 + b);
            if (x + 1 < width) {
                dst[x + 1] = 0xff000000u | (clamp8(y1 + r) << 16) | (clamp8(y1 + g) << 8)
                             | clamp8(y1 + b);
            }
        }
    }
    return out;
}

std::uint32_t bmd_audio_target_frames(const BmdDisplayModeInfo& mode) noexcept
{
    constexpr std::uint32_t floor = audio::k_sample_rate / 25;  // 40 ms
    if (mode.time_scale <= 0 || mode.frame_duration <= 0) {
        return floor;
    }
    const auto per_frame = static_cast<std::uint32_t>(
        std::llround(double(audio::k_sample_rate) * double(mode.frame_duration)
                     / double(mode.time_scale)));
    return std::max(floor, 2 * per_frame);
}

std::uint32_t bmd_audio_top_up(std::uint32_t buffered, std::uint32_t target) noexcept
{
    return buffered >= target ? 0 : target - buffered;
}

void bmd_audio_pack(const float* in, std::size_t samples, std::int32_t* out) noexcept
{
    constexpr double full = 2147483647.0;
    for (std::size_t i = 0; i < samples; ++i) {
        const double v = std::clamp(double(in[i]), -1.0, 1.0);
        out[i] = static_cast<std::int32_t>(std::lround(v * full));
    }
}

} // namespace seventhslide::render
