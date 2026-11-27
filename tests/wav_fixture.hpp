#pragma once

// A real audio file for tests that play one: a WAV of a quiet tone, written
// on the spot so the suite needs no binary fixture.

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>

namespace seventhslide::test {

/// Write ``seconds`` of a 440 Hz tone, 8 kHz mono 16-bit PCM, to ``path``.
inline void write_tone_wav(const std::filesystem::path& path, double seconds = 1.0)
{
    constexpr std::uint32_t rate = 8000;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bits = 16;
    const auto samples = static_cast<std::uint32_t>(seconds * rate);
    const std::uint32_t data_bytes = samples * channels * (bits / 8);

    std::ofstream out(path, std::ios::binary);
    const auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.put(static_cast<char>((v >> (8 * i)) & 0xff));
        }
    };
    const auto u16 = [&](std::uint16_t v) {
        out.put(static_cast<char>(v & 0xff));
        out.put(static_cast<char>((v >> 8) & 0xff));
    };
    out.write("RIFF", 4);
    u32(36 + data_bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);  // PCM
    u16(channels);
    u32(rate);
    u32(rate * channels * (bits / 8));
    u16(static_cast<std::uint16_t>(channels * (bits / 8)));
    u16(bits);
    out.write("data", 4);
    u32(data_bytes);
    for (std::uint32_t i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / rate;
        const auto sample = static_cast<std::int16_t>(2000.0 * std::sin(2.0 * std::numbers::pi * 440.0 * t));
        u16(static_cast<std::uint16_t>(sample));
    }
}

}  // namespace seventhslide::test
