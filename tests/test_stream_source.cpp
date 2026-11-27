// Sound pushed in on another clock (a web page's) as a stack's source: kept a
// little ahead, passed through at the mixer's rate, resampled at another.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/audio/audio_format.hpp>
#include <seventhslide/audio/stream_source.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using seventhslide::audio::k_block_frames;
using seventhslide::audio::k_channels;
using seventhslide::audio::k_sample_rate;
using seventhslide::audio::StreamMix;
using seventhslide::audio::StreamSource;

namespace {

/// ``frames`` stereo frames of a sine at ``hz``, both sides alike, starting at
/// frame ``from`` of the tone.
std::vector<float> tone(std::size_t from, std::size_t frames, int rate, double hz = 440.0)
{
    std::vector<float> out(frames * k_channels);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto v = static_cast<float>(
            0.5 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(from + i) / rate));
        out[i * k_channels] = v;
        out[i * k_channels + 1] = v;
    }
    return out;
}

double rms(const std::vector<float>& samples)
{
    double sum = 0.0;
    for (const float v : samples) {
        sum += static_cast<double>(v) * v;
    }
    return samples.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(samples.size()));
}

std::vector<float> read_block(StreamSource& source)
{
    std::vector<float> out(k_block_frames * k_channels, 1.0f);
    source.read(out.data(), k_block_frames);
    return out;
}

} // namespace

TEST_CASE("Pushed sound plays once enough is in hand, silence before", "[audio][web]")
{
    StreamSource source(k_sample_rate / 8);
    // Nothing yet: silence, written over whatever was in the block.
    CHECK(rms(read_block(source)) == 0.0);

    // A page's chunks of 2048 frames; under the target, still silence.
    std::size_t pushed = 0;
    const auto push = [&](int rate) {
        const auto chunk = tone(pushed, 2048, rate);
        source.push(chunk.data(), 2048, rate);
        pushed += 2048;
    };
    push(k_sample_rate);
    CHECK(rms(read_block(source)) == 0.0);
    for (int i = 0; i < 3; ++i) {
        push(k_sample_rate);
    }
    // A sine at half scale: an RMS of 0.5 / sqrt 2.
    CHECK(rms(read_block(source)) == Catch::Approx(0.5 / std::numbers::sqrt2).margin(0.02));
}

TEST_CASE("Sound at another rate is resampled to the mixer's", "[audio][web]")
{
    StreamSource source(k_sample_rate / 8);
    constexpr int rate = 44100;
    // A quarter second at 44.1 kHz, in uneven chunks.
    std::size_t pushed = 0;
    for (const std::size_t n : {1000u, 2048u, 333u, 4096u}) {
        const auto chunk = tone(pushed, n, rate);
        source.push(chunk.data(), n, rate);
        pushed += n;
    }
    constexpr std::size_t quarter = rate / 4;
    while (pushed < quarter) {
        const std::size_t n = std::min<std::size_t>(2048, quarter - pushed);
        const auto chunk = tone(pushed, n, rate);
        source.push(chunk.data(), n, rate);
        pushed += n;
    }
    // What came out is about a quarter second at 48 kHz, and the same tone.
    CHECK(source.buffer().fill() == Catch::Approx(k_sample_rate / 4).margin(k_sample_rate / 400));
    const auto out = read_block(source);
    CHECK(rms(out) == Catch::Approx(0.5 / std::numbers::sqrt2).margin(0.02));
    // No jump where chunks meet: neighbouring samples of a 440 Hz sine differ
    // by at most 2 pi 440 / 48000 of its amplitude.
    float worst = 0.0f;
    for (std::size_t i = 1; i < k_block_frames; ++i) {
        worst = std::max(worst, std::abs(out[i * k_channels] - out[(i - 1) * k_channels]));
    }
    CHECK(worst < 0.5f * 2.0f * std::numbers::pi_v<float> * 440.0f / k_sample_rate * 1.2f);
}

TEST_CASE("Sound at a rate no page makes is dropped", "[audio][web]")
{
    StreamSource source(k_block_frames);
    const auto chunk = tone(0, 4096, 1000);
    source.push(chunk.data(), 4096, 1000);
    source.push(chunk.data(), 4096, 500000);
    CHECK(source.buffer().fill() == 0);
}

TEST_CASE("A page's streams are summed, each kept apart until then", "[audio][web]")
{
    seventhslide::audio::StreamMix mix;
    // Two streams at different rates, in chunks of their own sizes, at a
    // quarter scale each, alternating as a page's two contexts would.
    std::vector<float> a(2048 * k_channels, 0.25f);
    std::vector<float> b(1024 * k_channels, 0.25f);
    for (int i = 0; i < 4; ++i) {
        mix.push(7, a.data(), 2048, k_sample_rate);
        mix.push(-3, b.data(), 1024, 44100);
        mix.push(-3, b.data(), 1024, 44100);
    }
    CHECK(mix.stream_count() == 2);
    std::vector<float> out(k_block_frames * k_channels);
    mix.read(out.data(), k_block_frames);
    // Neither garbled into the other: a steady sum.
    for (const float v : out) {
        CHECK(v == Catch::Approx(0.5f).margin(0.01));
    }

    // No more streams than a page is allowed.
    for (std::int64_t id = 100; id < 120; ++id) {
        mix.push(id, a.data(), 2048, k_sample_rate);
    }
    CHECK(mix.stream_count() == StreamMix::k_max_streams);
}


TEST_CASE("A page's stream that has ended makes room for a new one", "[audio][web]")
{
    // A page that makes an audio context for each thing it plays: ids 100 to
    // 108 in turn, 2 s of the mixer's time apart, beside a stream (7) that
    // sends all along.
    StreamMix mix;
    const std::vector<float> chunk(2048 * k_channels, 0.25f);
    const std::vector<float> steady(4 * k_block_frames * k_channels, 0.25f);
    std::vector<float> out(k_block_frames * k_channels);
    for (std::int64_t id = 100; id < 109; ++id) {
        for (int i = 0; i < 3; ++i) {  // enough to start: 125 ms
            mix.push(id, chunk.data(), 2048, k_sample_rate);
        }
        mix.read(out.data(), k_block_frames);
        INFO("stream " << id);
        REQUIRE(out[0] == Catch::Approx(id == 100 ? 0.25f : 0.5f).margin(0.01));
        if (id == 108) {
            break;
        }
        for (int block = 1; block < 200; ++block) {
            if (block % 4 == 1) {
                mix.push(7, steady.data(), 4 * k_block_frames, k_sample_rate);
            }
            mix.read(out.data(), k_block_frames);
        }
    }
    // The ninth is heard beside the one still sending, and those that ended
    // are gone.
    for (const float v : out) {
        CHECK(v == Catch::Approx(0.5f).margin(0.01));
    }
    CHECK(mix.stream_count() == 2);
}
