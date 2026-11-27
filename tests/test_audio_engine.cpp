#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "test_wait.hpp"

#include <seventhslide/audio/audio_engine.hpp>
#include <seventhslide/audio/audio_file_player.hpp>
#include <seventhslide/audio/audio_source.hpp>
#include <seventhslide/audio/device_sink.hpp>
#include <seventhslide/audio/drift_buffer.hpp>
#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/audio/pipewire_source.hpp>
#include <seventhslide/media/audio_levels.hpp>

#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace audio = seventhslide::audio;
namespace media = seventhslide::media;
using seventhslide::library::ChannelMix;
using Catch::Approx;
using namespace std::chrono_literals;

namespace {

constexpr std::size_t k_block = audio::k_block_frames;
constexpr double k_inf = std::numeric_limits<double>::infinity();

/// Writes a constant frame, and counts how often it was read.
class ConstSource final : public audio::AudioSource {
public:
    ConstSource(float left, float right) : left_(left), right_(right) {}
    void read(float* out, std::size_t frames) override
    {
        ++reads;
        for (std::size_t f = 0; f < frames; ++f) {
            out[f * 2] = left_;
            out[f * 2 + 1] = right_;
        }
    }
    std::atomic<int> reads{0};

private:
    float left_;
    float right_;
};

/// Keeps what an output sends.
class CaptureSink final : public audio::OutputAudioSink {
public:
    void push(const float* frames, std::size_t count) override
    {
        last.assign(frames, frames + count * audio::k_channels);
        ++pushes;
    }
    std::vector<float> last;
    int pushes = 0;
};

std::vector<float> block_of_silence()
{
    return std::vector<float>(k_block * audio::k_channels, 0.0f);
}

/// One block through the mixer; the Listen mix.
std::vector<float> mix_block(audio::Mixer& mixer)
{
    auto listen = block_of_silence();
    mixer.process(listen.data(), k_block);
    return listen;
}

float gain(double db)
{
    return static_cast<float>(media::db_to_gain(db));
}

/// The test files' signal: a ramp, so a sample's value says which frame it
/// is. Left rises, right falls.
float ramp_at(std::size_t frame, std::size_t frames)
{
    return 0.9f * static_cast<float>(frame) / static_cast<float>(frames);
}

/// A 32-bit float WAV at 48 kHz: ``frames`` frames of ramp_at, stereo
/// (right = -left) or mono.
std::filesystem::path write_ramp_wav(const QTemporaryDir& dir, const char* name,
                                     std::size_t frames, int channels = 2)
{
    const auto path = std::filesystem::path(dir.path().toStdString()) / name;
    std::ofstream out(path, std::ios::binary);
    const auto put32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(frames * channels * 4);
    out.write("RIFF", 4);
    put32(36 + data_bytes);
    out.write("WAVEfmt ", 8);
    put32(16);
    put16(3);  // IEEE float
    put16(static_cast<std::uint16_t>(channels));
    put32(audio::k_sample_rate);
    put32(static_cast<std::uint32_t>(audio::k_sample_rate * channels * 4));
    put16(static_cast<std::uint16_t>(channels * 4));
    put16(32);
    out.write("data", 4);
    put32(data_bytes);
    for (std::size_t f = 0; f < frames; ++f) {
        const float v = ramp_at(f, frames);
        out.write(reinterpret_cast<const char*>(&v), 4);
        if (channels == 2) {
            const float r = -v;
            out.write(reinterpret_cast<const char*>(&r), 4);
        }
    }
    return path;
}

/// Read ``frames`` from ``player`` a block at a time, as the mixer would,
/// waiting for the decoder whenever the test outruns it.
std::vector<float> read_frames(audio::AudioFilePlayer& player, std::size_t frames)
{
    std::vector<float> out(frames * audio::k_channels);
    for (std::size_t done = 0; done < frames; done += k_block) {
        REQUIRE(player.wait_buffered(5s));
        player.read(out.data() + done * audio::k_channels, std::min(k_block, frames - done));
    }
    return out;
}

std::filesystem::path fixture(const char* name)
{
    return std::filesystem::path(SEVENTHSLIDE_FIXTURES_DIR) / name;
}

using seventhslide::test::wait_until;

/// Two stacks, two outputs: o1 shows stack a, o2 shows both.
audio::Routing two_by_two()
{
    return {{"a", "b"}, {{"o1", {"a"}}, {"o2", {"a", "b"}}}};
}

}  // namespace

// --- Mixer -----------------------------------------------------------------

TEST_CASE("Mixer: each output carries the stacks it shows, after their faders",
          "[audio][mixer]")
{
    audio::Mixer mixer;
    ConstSource a(0.5f, -0.5f);
    ConstSource b(0.25f, 0.25f);
    CaptureSink o1;
    CaptureSink o2;
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{-6.0, false, false});
    mixer.set_routing(two_by_two());
    mixer.add_source("a", &a);
    mixer.add_source("b", &b);
    mixer.add_sink("o1", &o1);
    mixer.add_sink("o2", &o2);

    const auto listen = mix_block(mixer);
    REQUIRE(o1.pushes == 1);
    for (std::size_t f = 0; f < k_block; ++f) {
        REQUIRE(o1.last[f * 2] == Approx(0.5f * gain(-6.0)));
        REQUIRE(o1.last[f * 2 + 1] == Approx(-0.5f * gain(-6.0)));
        REQUIRE(o2.last[f * 2] == Approx(0.5f * gain(-6.0) + 0.25f));
        REQUIRE(o2.last[f * 2 + 1] == Approx(-0.5f * gain(-6.0) + 0.25f));
    }
    // Nothing is on Listen by default: the headphones hear nothing.
    REQUIRE(std::ranges::all_of(listen, [](float v) { return v == 0.0f; }));

    // An output's own fader comes after the sum.
    mixer.set_strip(audio::StripKind::Output, "o2", ChannelMix{-12.0, false, false});
    mix_block(mixer);  // the ramp
    mix_block(mixer);
    REQUIRE(o2.last[0] == Approx((0.5f * gain(-6.0) + 0.25f) * gain(-12.0)));
    REQUIRE(o1.last[0] == Approx(0.5f * gain(-6.0)));
}

TEST_CASE("Mixer: a muted stack is silent everywhere, but its meter still reads",
          "[audio][mixer]")
{
    audio::Mixer mixer;
    ConstSource a(0.5f, 0.5f);
    ConstSource b(0.25f, 0.25f);
    CaptureSink o1;
    CaptureSink o2;
    mixer.set_routing(two_by_two());
    mixer.add_source("a", &a);
    mixer.add_source("b", &b);
    mixer.add_sink("o1", &o1);
    mixer.add_sink("o2", &o2);
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{0.0, true, true});

    mix_block(mixer);  // the ramp down
    const auto listen = mix_block(mixer);
    REQUIRE(std::ranges::all_of(o1.last, [](float v) { return v == 0.0f; }));
    REQUIRE(o2.last[0] == Approx(0.25f));
    // On Listen, but muted: still silent there.
    REQUIRE(std::ranges::all_of(listen, [](float v) { return v == 0.0f; }));
    REQUIRE(mixer.meter(audio::StripKind::Stack, "a").level_db[0]
            == Approx(media::gain_to_db(0.5)).margin(0.01));
}

TEST_CASE("Mixer: level, mute and Listen changes ramp across one block", "[audio][mixer]")
{
    audio::Mixer mixer;
    ConstSource a(0.5f, 0.5f);
    CaptureSink o1;
    mixer.set_routing({{"a"}, {{"o1", {"a"}}}});
    mixer.add_source("a", &a);
    mixer.add_sink("o1", &o1);
    mix_block(mixer);
    REQUIRE(o1.last[0] == Approx(0.5f));

    // Down to silence: a straight line from full to nothing over 10 ms.
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{-k_inf, false, false});
    mix_block(mixer);
    REQUIRE(o1.last[0] == Approx(0.5f * (1.0f - 1.0f / k_block)));
    REQUIRE(o1.last[(k_block / 2 - 1) * 2] == Approx(0.25f).margin(1e-6));
    REQUIRE(o1.last[(k_block - 1) * 2] == Approx(0.0f).margin(1e-7));
    mix_block(mixer);
    REQUIRE(std::ranges::all_of(o1.last, [](float v) { return v == 0.0f; }));

    // Listen fades in the same way.
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{0.0, false, false});
    REQUIRE(std::ranges::all_of(mix_block(mixer), [](float v) { return v == 0.0f; }));
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{0.0, false, true});
    const auto listen = mix_block(mixer);
    REQUIRE(listen[0] == Approx(0.5f / k_block));
    REQUIRE(listen[(k_block - 1) * 2] == Approx(0.5f));
}

TEST_CASE("Mixer: Listen hears the stacks and outputs on it, clipped at full scale",
          "[audio][mixer]")
{
    audio::Mixer mixer;
    ConstSource a(0.5f, 0.5f);
    ConstSource b(0.25f, -0.25f);
    mixer.set_strip(audio::StripKind::Stack, "b", ChannelMix{0.0, false, true});
    mixer.set_strip(audio::StripKind::Output, "o1", ChannelMix{-6.0, false, true});
    mixer.set_routing(two_by_two());
    mixer.add_source("a", &a);
    mixer.add_source("b", &b);

    auto listen = mix_block(mixer);
    REQUIRE(listen[0] == Approx(0.25f + 0.5f * gain(-6.0)));
    REQUIRE(listen[1] == Approx(-0.25f + 0.5f * gain(-6.0)));

    // A stack also on Listen through an output is heard twice, as in OBS;
    // what goes over full scale is clipped, not wrapped.
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{0.0, false, true});
    mixer.set_strip(audio::StripKind::Output, "o2", ChannelMix{0.0, false, true});
    mix_block(mixer);
    listen = mix_block(mixer);
    REQUIRE(listen[0] == 1.0f);
}

TEST_CASE("Mixer: meters fall back at OBS's rate and hold their peak", "[audio][mixer]")
{
    audio::Mixer mixer;
    ConstSource loud(0.5f, 0.25f);
    mixer.set_routing({{"a"}, {{"o1", {"a"}}}});
    mixer.add_source("a", &loud);
    mix_block(mixer);
    auto m = mixer.meter(audio::StripKind::Stack, "a");
    REQUIRE(m.level_db[0] == Approx(-6.02).margin(0.01));
    REQUIRE(m.level_db[1] == Approx(-12.04).margin(0.01));
    REQUIRE(mixer.meter(audio::StripKind::Output, "o1").level_db[0] == Approx(-6.02).margin(0.01));

    // The block it fades out across, then one second of silence: 23.53 dB
    // lower, the peak still held.
    mixer.remove_source(&loud);
    for (int i = 0; i < 101; ++i) {
        mix_block(mixer);
    }
    m = mixer.meter(audio::StripKind::Stack, "a");
    REQUIRE(m.level_db[0] == Approx(-6.02 - 23.53).margin(0.05));
    REQUIRE(m.peak_hold_db[0] == Approx(-6.02).margin(0.01));

    // Strips the routing does not name read as silence.
    REQUIRE(std::isinf(mixer.meter(audio::StripKind::Stack, "nope").level_db[0]));
}

TEST_CASE("Mixer: a removed source is never read again; an unrouted stack's are not read",
          "[audio][mixer]")
{
    audio::Mixer mixer;
    ConstSource a(0.5f, 0.5f);
    ConstSource stray(0.5f, 0.5f);
    mixer.set_routing({{"a"}, {}});
    mixer.add_source("a", &a);
    mixer.add_source("not-a-stack", &stray);
    mix_block(mixer);
    REQUIRE(a.reads == 1);
    REQUIRE(stray.reads == 0);

    mixer.remove_source(&a);
    const int at_removal = a.reads;  // one more, for the block that fades out
    REQUIRE(at_removal == 2);
    mix_block(mixer);
    REQUIRE(a.reads == at_removal);

    // A removed sink gets nothing more.
    CaptureSink sink;
    mixer.set_routing({{"a"}, {{"o1", {"a"}}}});
    mixer.add_sink("o1", &sink);
    mix_block(mixer);
    REQUIRE(sink.pushes == 1);
    mixer.remove_sink(&sink);
    mix_block(mixer);
    REQUIRE(sink.pushes == 1);

    // Settings survive the stack leaving the routing and coming back.
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{-3.0, true, false});
    mixer.set_routing({{}, {}});
    mixer.set_routing({{"a"}, {}});
    REQUIRE(mixer.strip(audio::StripKind::Stack, "a") == ChannelMix{-3.0, true, false});
}

TEST_CASE("Mixer: a removed source fades out across the next block", "[audio][mixer]")
{
    audio::Mixer mixer;
    ConstSource a(0.5f, -0.5f);
    // Set before it is routed, so Listen is on from the first block.
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{0.0, false, true});
    mixer.set_routing({{"a"}, {}});
    mixer.add_source("a", &a);
    REQUIRE(mix_block(mixer)[0] == 0.5f);

    mixer.remove_source(&a);
    const auto heard = mix_block(mixer);
    for (std::size_t f = 0; f < k_block; ++f) {
        const float fade = 1.0f - static_cast<float>(f + 1) / static_cast<float>(k_block);
        REQUIRE(heard[f * 2] == Approx(0.5f * fade).margin(1e-6));
        REQUIRE(heard[f * 2 + 1] == Approx(-0.5f * fade).margin(1e-6));
    }
    REQUIRE(std::ranges::all_of(mix_block(mixer), [](float v) { return v == 0.0f; }));
}

TEST_CASE("Mixer: a moved source plays on its new stack without a break", "[audio][mixer]")
{
    audio::Mixer mixer;
    ConstSource a(0.5f, 0.5f);
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{0.0, false, true});
    mixer.set_strip(audio::StripKind::Stack, "b", ChannelMix{0.0, false, true});
    mixer.set_routing({{"a", "b"}, {}});
    mixer.add_source("a", &a);
    mix_block(mixer);

    mixer.move_source(&a, "b");
    REQUIRE(a.reads == 1);  // not read ahead, as a removal would
    const auto heard = mix_block(mixer);
    REQUIRE(std::ranges::all_of(heard, [](float v) { return v == 0.5f; }));
    REQUIRE(mixer.meter(audio::StripKind::Stack, "b").level_db[0] > -7.0f);
    mixer.remove_source(&a);
}

TEST_CASE("Mixer: a cue strip is metered and heard on Listen, and summed into no output",
          "[audio][mixer][preview]")
{
    audio::Mixer mixer;
    ConstSource cued(0.5f, 0.5f);
    const auto cue = audio::cue_strip_id("main");
    CHECK(cue != "main");
    audio::Routing routing;
    routing.stacks = {"main"};
    // An output naming the cue strip among its stacks still never carries it.
    routing.outputs = {{"o1", {"main", cue}}};
    routing.cues = {cue};
    mixer.set_routing(routing);
    CaptureSink sink;
    mixer.add_sink("o1", &sink);
    mixer.add_source(cue, &cued);

    // Read, so its clock runs; metered; silent on the output.
    auto heard = mix_block(mixer);
    CHECK(cued.reads == 1);
    CHECK(mixer.meter(audio::StripKind::Stack, cue).level_db[0] > -7.0f);
    REQUIRE(sink.pushes == 1);
    CHECK(std::ranges::all_of(sink.last, [](float v) { return v == 0.0f; }));
    CHECK(std::ranges::all_of(heard, [](float v) { return v == 0.0f; }));

    // On Listen, the operator hears it there alone.
    mixer.set_strip(audio::StripKind::Stack, cue, ChannelMix{0.0, false, true});
    mix_block(mixer);
    heard = mix_block(mixer);
    CHECK(std::ranges::all_of(heard, [](float v) { return v == 0.5f; }));
    CHECK(std::ranges::all_of(sink.last, [](float v) { return v == 0.0f; }));

    // Taken: moved to the stack's strip, it reaches the output unbroken.
    mixer.move_source(&cued, "main");
    mix_block(mixer);
    CHECK(std::ranges::all_of(sink.last, [](float v) { return v == 0.5f; }));
    mixer.remove_source(&cued);
    mixer.remove_sink(&sink);
}

TEST_CASE("Mixer: an output leaving a type of sound out hears the rest of the stack",
          "[audio][mixer]")
{
    audio::Mixer mixer;
    ConstSource video(0.5f, 0.5f);
    ConstSource bed(0.25f, 0.25f);
    CaptureSink thirds;
    CaptureSink main;
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{-6.0, false, true});
    mixer.set_routing({{"a"}, {{"thirds", {"a"}, {"video"}}, {"main", {"a"}}}});
    mixer.add_source("a", &video, "video");
    mixer.add_source("a", &bed, "audio");
    mixer.add_sink("thirds", &thirds);
    mixer.add_sink("main", &main);

    // The stack's strip and Listen have all of it, after its fader; an output
    // has what it carries, and its meter says so.
    const auto listen = mix_block(mixer);
    REQUIRE(listen[0] == Approx(0.75f * gain(-6.0)));
    REQUIRE(main.last[0] == Approx(0.75f * gain(-6.0)));
    REQUIRE(thirds.last[0] == Approx(0.25f * gain(-6.0)));
    REQUIRE(mixer.meter(audio::StripKind::Stack, "a").level_db[0]
            == Approx(media::gain_to_db(0.75 * gain(-6.0))).margin(0.01));
    REQUIRE(mixer.meter(audio::StripKind::Output, "thirds").level_db[0]
            == Approx(media::gain_to_db(0.25 * gain(-6.0))).margin(0.01));

    // The stack's mute ramps what each output carries alike.
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{-6.0, true, false});
    mix_block(mixer);
    REQUIRE(thirds.last[(k_block / 2 - 1) * 2]
            == Approx(0.25f * gain(-6.0) * 0.5f).margin(1e-6));
    REQUIRE(main.last[(k_block / 2 - 1) * 2] == Approx(0.75f * gain(-6.0) * 0.5f).margin(1e-6));
    mix_block(mixer);
    REQUIRE(std::ranges::all_of(thirds.last, [](float v) { return v == 0.0f; }));
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{0.0, false, false});
    mix_block(mixer);

    // Which types it leaves out can change while they play.
    mixer.set_routing({{"a"}, {{"thirds", {"a"}, {"audio"}}, {"main", {"a"}}}});
    mix_block(mixer);
    REQUIRE(thirds.last[0] == Approx(0.5f));
    REQUIRE(main.last[0] == Approx(0.75f));

    mixer.remove_source(&video);
    mixer.remove_source(&bed);
    mixer.remove_sink(&thirds);
    mixer.remove_sink(&main);
}

TEST_CASE("Mixer: one type handing over to another fades out only where it was carried",
          "[audio][mixer]")
{
    audio::Mixer mixer;
    ConstSource page(0.5f, 0.5f);
    ConstSource video(0.25f, 0.25f);
    CaptureSink thirds;
    CaptureSink main;
    const auto fade = [](std::size_t f) {
        return 1.0f - static_cast<float>(f + 1) / static_cast<float>(k_block);
    };
    mixer.set_routing({{"a"}, {{"thirds", {"a"}, {"web"}}, {"main", {"a"}}}});
    mixer.add_source("a", &page, "web");
    mixer.add_sink("thirds", &thirds);
    mixer.add_sink("main", &main);
    mix_block(mixer);
    REQUIRE(std::ranges::all_of(thirds.last, [](float v) { return v == 0.0f; }));

    // From what it leaves out to what it carries: none of the page reaches it,
    // its fade-out included.
    mixer.add_source("a", &video, "video");
    mixer.remove_source(&page);
    mix_block(mixer);
    for (std::size_t f = 0; f < k_block; ++f) {
        REQUIRE(thirds.last[f * 2] == Approx(0.25f));
        REQUIRE(main.last[f * 2] == Approx(0.25f + 0.5f * fade(f)).margin(1e-6));
    }
    mix_block(mixer);
    REQUIRE(main.last[0] == Approx(0.25f));

    // From what it carries to what it leaves out: the video fades out rather
    // than stopping dead, and none of the page comes in.
    mixer.add_source("a", &page, "web");
    mixer.remove_source(&video);
    mix_block(mixer);
    for (std::size_t f = 0; f < k_block; ++f) {
        REQUIRE(thirds.last[f * 2] == Approx(0.25f * fade(f)).margin(1e-6));
        REQUIRE(main.last[f * 2] == Approx(0.5f + 0.25f * fade(f)).margin(1e-6));
    }
    mix_block(mixer);
    REQUIRE(std::ranges::all_of(thirds.last, [](float v) { return v == 0.0f; }));

    // Stopping with nothing after it: the same, and then quiet.
    mixer.remove_source(&page);
    mix_block(mixer);
    REQUIRE(std::ranges::all_of(thirds.last, [](float v) { return v == 0.0f; }));
    REQUIRE(main.last[0] == Approx(0.5f * fade(0)).margin(1e-6));
    mix_block(mixer);
    REQUIRE(std::ranges::all_of(main.last, [](float v) { return v == 0.0f; }));
    mixer.remove_sink(&thirds);
    mixer.remove_sink(&main);
}

TEST_CASE("Mixer: each output fades a source by its own", "[audio][mixer][fades]")
{
    audio::Mixer mixer;
    ConstSource a(0.5f, 0.5f);
    CaptureSink o1;
    CaptureSink o2;
    mixer.set_routing(two_by_two());
    mixer.add_source("a", &a);
    mixer.add_sink("o1", &o1);
    mixer.add_sink("o2", &o2);
    mix_block(mixer);
    REQUIRE(o1.last[0] == Approx(0.5f));
    REQUIRE(o2.last[0] == Approx(0.5f));

    // o1 down over two blocks, in a straight line; o2 at once.
    mixer.fade_source(&a, "o1", 0.0f, 20ms);
    mixer.fade_source(&a, "o2", 0.0f, 0ms);
    mix_block(mixer);
    REQUIRE(o1.last[0] == Approx(0.5f * (1.0f - 0.5f / k_block)).margin(1e-6));
    REQUIRE(o1.last[(k_block - 1) * 2] == Approx(0.25f).margin(1e-6));
    REQUIRE(std::ranges::all_of(o2.last, [](float v) { return v == 0.0f; }));
    REQUIRE(mixer.source_level(&a, "o1").value() == Approx(0.5f));
    REQUIRE(mixer.source_level(&a, "o2") == 0.0f);
    mix_block(mixer);
    REQUIRE(o1.last[(k_block - 1) * 2] == Approx(0.0f).margin(1e-6));
    REQUIRE(mixer.source_level(&a, "o1") == 0.0f);

    // Back up on o1 alone.
    mixer.fade_source(&a, "o1", 1.0f, 0ms);
    mix_block(mixer);
    REQUIRE(o1.last[0] == Approx(0.5f));
    REQUIRE(std::ranges::all_of(o2.last, [](float v) { return v == 0.0f; }));

    // Every level at once.
    mixer.set_source_level(&a, 1.0f);
    mix_block(mixer);
    REQUIRE(o2.last[0] == Approx(0.5f));
    REQUIRE_FALSE(mixer.source_level(nullptr, "o1"));
    mixer.remove_source(&a);
    mixer.remove_sink(&o1);
    mixer.remove_sink(&o2);
}

TEST_CASE("Mixer: a stack's strip hears a source at the loudest output carrying it",
          "[audio][mixer][fades]")
{
    audio::Mixer mixer;
    ConstSource a(0.5f, 0.5f);
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{0.0, false, true});
    mixer.set_routing(two_by_two());
    mixer.add_source("a", &a);
    REQUIRE(mix_block(mixer)[0] == Approx(0.5f));

    // One output still has it at full: so has the strip, its meter and Listen.
    mixer.fade_source(&a, "o1", 0.0f, 0ms);
    REQUIRE(mix_block(mixer)[0] == Approx(0.5f));
    REQUIRE(mixer.source_level(&a, {}) == 1.0f);
    // Half on the other: half on the strip.
    mixer.fade_source(&a, "o2", 0.5f, 0ms);
    REQUIRE(mix_block(mixer)[0] == Approx(0.25f));
    REQUIRE(mixer.source_level(&a, {}) == 0.5f);
    mixer.fade_source(&a, "o2", 0.0f, 0ms);
    REQUIRE(std::ranges::all_of(mix_block(mixer), [](float v) { return v == 0.0f; }));

    // What no output carries from, a cue strip, hears the level outputs get
    // that are not named.
    ConstSource cued(0.5f, 0.5f);
    const auto cue = audio::cue_strip_id("a");
    auto routing = two_by_two();
    routing.cues = {cue};
    mixer.set_routing(routing);
    mixer.set_strip(audio::StripKind::Stack, "a", ChannelMix{0.0, false, false});
    mixer.set_strip(audio::StripKind::Stack, cue, ChannelMix{0.0, false, true});
    mixer.add_source(cue, &cued);
    mix_block(mixer);
    REQUIRE(mix_block(mixer)[0] == Approx(0.5f));
    mixer.fade_source(&cued, {}, 0.5f, 0ms);
    REQUIRE(mix_block(mixer)[0] == Approx(0.25f));
    REQUIRE(mixer.source_level(&cued, {}) == 0.5f);
    mixer.remove_source(&a);
    mixer.remove_source(&cued);
}

TEST_CASE("Mixer: an output that comes to carry a source starts at the level of those not named",
          "[audio][mixer][fades]")
{
    audio::Mixer mixer;
    ConstSource a(0.5f, 0.5f);
    CaptureSink o1;
    CaptureSink o2;
    mixer.set_routing({{"a"}, {{"o1", {"a"}}}});
    mixer.add_source("a", &a);
    mixer.add_sink("o1", &o1);
    mixer.add_sink("o2", &o2);
    // o1 is routed, so has a level of its own; the rest are at half.
    mixer.fade_source(&a, {}, 0.5f, 0ms);
    mix_block(mixer);
    REQUIRE(o1.last[0] == Approx(0.5f));

    mixer.set_routing(two_by_two());
    mix_block(mixer);
    REQUIRE(o1.last[0] == Approx(0.5f));
    REQUIRE(o2.last[0] == Approx(0.25f));

    // One faded before the routing names it keeps its level for when it does.
    ConstSource b(0.5f, 0.5f);
    mixer.add_source("a", &b);
    mixer.fade_source(&b, "o3", 0.0f, 0ms);
    REQUIRE(mixer.source_level(&b, "o3") == 0.0f);
    auto routing = two_by_two();
    routing.outputs.push_back({"o3", {"a"}});
    CaptureSink o3;
    mixer.set_routing(routing);
    mixer.add_sink("o3", &o3);
    mix_block(mixer);
    // o3 carries a at its rest level, half, and none of b.
    REQUIRE(o3.last[0] == Approx(0.25f));
    mixer.remove_source(&a);
    mixer.remove_source(&b);
    mixer.remove_sink(&o1);
    mixer.remove_sink(&o2);
    mixer.remove_sink(&o3);
}

TEST_CASE("Mixer: a source removed mid-fade fades out from where each output had it",
          "[audio][mixer][fades]")
{
    audio::Mixer mixer;
    ConstSource a(0.5f, 0.5f);
    CaptureSink o1;
    CaptureSink o2;
    mixer.set_routing(two_by_two());
    mixer.add_source("a", &a);
    mixer.add_sink("o1", &o1);
    mixer.add_sink("o2", &o2);
    mixer.fade_source(&a, "o2", 0.5f, 0ms);
    mix_block(mixer);
    REQUIRE(o2.last[0] == Approx(0.25f));

    mixer.remove_source(&a);
    mix_block(mixer);
    for (std::size_t f = 0; f < k_block; ++f) {
        const float fade = 1.0f - static_cast<float>(f + 1) / static_cast<float>(k_block);
        REQUIRE(o1.last[f * 2] == Approx(0.5f * fade).margin(1e-6));
        REQUIRE(o2.last[f * 2] == Approx(0.25f * fade).margin(1e-6));
    }
    mix_block(mixer);
    REQUIRE(std::ranges::all_of(o1.last, [](float v) { return v == 0.0f; }));
    REQUIRE(std::ranges::all_of(o2.last, [](float v) { return v == 0.0f; }));
    mixer.remove_sink(&o1);
    mixer.remove_sink(&o2);
}

TEST_CASE("Mixer: a fade carries on through a rebuild and a move", "[audio][mixer][fades]")
{
    audio::Mixer mixer;
    ConstSource a(0.5f, 0.5f);
    CaptureSink o2;
    mixer.set_routing(two_by_two());
    mixer.add_source("a", &a);
    mixer.add_sink("o2", &o2);
    // Down over a second: a block is 1/100 of it.
    mixer.fade_source(&a, "o2", 0.0f, 1000ms);
    mix_block(mixer);
    REQUIRE(mixer.source_level(&a, "o2").value() == Approx(0.99f).margin(1e-4));

    mixer.set_routing(two_by_two());
    ConstSource other(0.0f, 0.0f);
    mixer.add_source("b", &other);
    mixer.move_source(&a, "b");
    mix_block(mixer);
    REQUIRE(mixer.source_level(&a, "o2").value() == Approx(0.98f).margin(1e-4));
    REQUIRE(o2.last[(k_block - 1) * 2] == Approx(0.5f * 0.98f).margin(1e-4));
    mixer.remove_source(&a);
    mixer.remove_source(&other);
    mixer.remove_sink(&o2);
}

// --- DriftBuffer -----------------------------------------------------------

TEST_CASE("DriftBuffer passes sound through untouched while the clocks agree", "[audio][drift]")
{
    audio::DriftBuffer buffer;  // 40 ms
    std::vector<float> in(k_block * 2);
    std::vector<float> out(k_block * 2);
    std::vector<float> sent;
    std::vector<float> heard;
    for (int block = 0; block < 200; ++block) {
        for (std::size_t i = 0; i < in.size(); ++i) {
            in[i] = static_cast<float>(block * in.size() + i) * 1e-5f;
        }
        sent.insert(sent.end(), in.begin(), in.end());
        buffer.push(in.data(), k_block);
        buffer.pull(out.data(), k_block);
        heard.insert(heard.end(), out.begin(), out.end());
    }
    REQUIRE(buffer.ratio() == 1.0);
    REQUIRE(buffer.underruns() == 0);
    // Silent until it holds its target, then the input exactly, that much later.
    const std::size_t delay = (buffer.target() - k_block) * 2;
    REQUIRE(std::all_of(heard.begin(), heard.begin() + static_cast<std::ptrdiff_t>(delay),
                        [](float v) { return v == 0.0f; }));
    for (std::size_t i = delay; i < heard.size(); ++i) {
        REQUIRE(heard[i] == sent[i - delay]);
    }
}

namespace {

/// Two clocks for two minutes: the engine pushes 10 ms blocks at 48 kHz, the
/// sink pulls ``pull_frames`` at 48 kHz times ``sink_speed``. Returns the sink's
/// output once settled, and leaves the buffer as it ended.
std::vector<float> run_two_clocks(audio::DriftBuffer& buffer, double sink_speed,
                                  std::uint64_t& late_underruns, std::size_t pull_frames = 512)
{
    constexpr double seconds = 120.0;
    const double push_every = static_cast<double>(k_block) / audio::k_sample_rate;
    const double pull_every = static_cast<double>(pull_frames) / (audio::k_sample_rate * sink_speed);
    std::vector<float> block(k_block * 2);
    std::vector<float> out(pull_frames * 2);
    std::vector<float> settled;
    double phase = 0.0;
    double next_push = 0.0;
    double next_pull = 0.0;
    std::uint64_t underruns_at_settle = 0;
    while (next_pull < seconds) {
        if (next_push <= next_pull) {
            for (std::size_t f = 0; f < k_block; ++f) {
                const auto v = static_cast<float>(0.5 * std::sin(phase));
                block[f * 2] = v;
                block[f * 2 + 1] = -v;
                phase += 2.0 * std::numbers::pi * 440.0 / audio::k_sample_rate;
            }
            buffer.push(block.data(), k_block);
            next_push += push_every;
        } else {
            buffer.pull(out.data(), pull_frames);
            if (next_pull >= 1.0) {
                if (settled.empty()) {
                    underruns_at_settle = buffer.underruns();
                }
                settled.insert(settled.end(), out.begin(), out.end());
            }
            next_pull += pull_every;
        }
    }
    late_underruns = buffer.underruns() - underruns_at_settle;
    return settled;
}

}  // namespace

TEST_CASE("DriftBuffer follows a sink clock 0.3% fast or slow, without a click",
          "[audio][drift]")
{
    for (const double speed : {1.003, 0.997}) {
        audio::DriftBuffer buffer;
        std::uint64_t late_underruns = 0;
        const auto heard = run_two_clocks(buffer, speed, late_underruns);

        REQUIRE(late_underruns == 0);
        REQUIRE(buffer.overruns() == 0);
        // Settled: reading at the sink's pace, the fill back near its target.
        REQUIRE(buffer.ratio() == Approx(1.0 / speed).margin(0.0003));
        REQUIRE(buffer.fill() > buffer.target() / 2);
        REQUIRE(buffer.fill() < buffer.target() * 2);
        // No frame dropped or repeated: a 440 Hz sine never jumps further
        // than it moves in one frame.
        const double max_step = 0.5 * 2.0 * std::numbers::pi * 440.0 / audio::k_sample_rate * 1.05;
        double worst = 0.0;
        for (std::size_t i = 2; i < heard.size(); i += 2) {
            worst = std::max(worst, static_cast<double>(std::abs(heard[i] - heard[i - 2])));
        }
        REQUIRE(worst < max_step);
    }
}

TEST_CASE("DriftBuffer never runs dry for a sink that pulls a target's worth at a time",
          "[audio][drift]")
{
    // DeckLink tops its card up once a video frame: 1920 frames at 25p, 2002
    // at 23.98; a PipeWire graph may ask for 2048. With the default 40 ms
    // target, each would settle where its pulls ran dry, a gap every few
    // seconds.
    for (const std::size_t pull : {std::size_t{1920}, std::size_t{2002}, std::size_t{2048}}) {
        for (const double speed : {1.0, 1.003, 0.997}) {
            INFO("pull " << pull << " speed " << speed);
            audio::DriftBuffer buffer;
            std::uint64_t late_underruns = 0;
            (void)run_two_clocks(buffer, speed, late_underruns, pull);
            CHECK(late_underruns == 0);
            CHECK(buffer.overruns() == 0);
        }
    }
}

TEST_CASE("DriftBuffer goes quiet when the engine stops, and fills again", "[audio][drift]")
{
    audio::DriftBuffer buffer;
    std::vector<float> in(k_block * 2, 0.5f);
    std::vector<float> out(k_block * 2);
    for (int i = 0; i < 8; ++i) {
        buffer.push(in.data(), k_block);
    }
    buffer.pull(out.data(), k_block);
    REQUIRE(out[0] == 0.5f);
    // The engine stops: once dry, silence (counted), not a stale frame.
    for (int i = 0; i < 12; ++i) {
        buffer.pull(out.data(), k_block);
    }
    REQUIRE(buffer.underruns() >= 1);
    REQUIRE(std::ranges::all_of(out, [](float v) { return v == 0.0f; }));
    // Back again: silent until it holds its target, then the sound.
    for (int i = 0; i < 4; ++i) {
        buffer.push(in.data(), k_block);
    }
    buffer.pull(out.data(), k_block);
    REQUIRE(out[0] == 0.5f);

    // A sink that stops pulling: the oldest frames make room.
    for (int i = 0; i < 1000; ++i) {
        buffer.push(in.data(), k_block);
    }
    REQUIRE(buffer.overruns() > 0);
}

TEST_CASE("DriftBuffer drops back to its target soon after a sink stalls, without a click",
          "[audio][drift]")
{
    // The engine pushes a 10 ms block every 10 ms; the sink pulls one as
    // often, but stops for 200 ms and then goes on at its pace, as a device
    // or a card that hiccupped does, never catching up.
    audio::DriftBuffer buffer;  // 40 ms
    constexpr double k_hz = 437.0;
    constexpr int k_stall_from = 200;  // ticks of 10 ms
    constexpr int k_stall_ticks = 20;
    constexpr int k_resumed = k_stall_from + k_stall_ticks;
    std::vector<float> block(k_block * 2);
    std::vector<float> out(k_block * 2);
    std::vector<float> heard;
    double phase = 0.0;
    std::size_t peak = 0;
    std::size_t fill_a_second_on = 0;
    std::uint64_t underruns_before = 0;
    for (int tick = 0; tick < k_resumed + 500; ++tick) {
        for (std::size_t f = 0; f < k_block; ++f) {
            const auto v = static_cast<float>(0.5 * std::sin(phase));
            block[f * 2] = v;
            block[f * 2 + 1] = -v;
            phase += 2.0 * std::numbers::pi * k_hz / audio::k_sample_rate;
        }
        buffer.push(block.data(), k_block);
        if (tick >= k_stall_from && tick < k_resumed) {
            continue;
        }
        if (tick == k_stall_from - 1) {
            underruns_before = buffer.underruns();
        }
        buffer.pull(out.data(), k_block);
        if (tick >= 100) {
            heard.insert(heard.end(), out.begin(), out.end());
        }
        peak = std::max(peak, buffer.fill());
        if (tick == k_resumed + 100) {
            fill_a_second_on = buffer.fill();
        }
    }
    INFO("peak " << peak << ", a second on " << fill_a_second_on);
    REQUIRE(peak > buffer.target() * 4);
    REQUIRE(fill_a_second_on < buffer.target() * 2);
    REQUIRE(buffer.fill() > buffer.target() / 2);
    REQUIRE(buffer.underruns() == underruns_before);
    REQUIRE(buffer.overruns() == 0);
    // The frames dropped are faded across, not cut: the sine moves no further
    // in a frame than it does on its own, give or take the fade's own step.
    const double max_step = 0.5 * 2.0 * std::numbers::pi * k_hz / audio::k_sample_rate;
    double worst = 0.0;
    for (std::size_t i = 2; i < heard.size(); i += 2) {
        worst = std::max(worst, static_cast<double>(std::abs(heard[i] - heard[i - 2])));
    }
    REQUIRE(worst < max_step * 1.5);
}

// --- AudioFilePlayer -------------------------------------------------------

TEST_CASE("AudioFilePlayer plays a file sample for sample, its clock counting frames",
          "[audio][player]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::size_t frames = audio::k_sample_rate;  // 1 s
    const auto wav = write_ramp_wav(dir, "ramp.wav", frames);

    audio::AudioFilePlayer player;
    REQUIRE(player.load(wav.string(), false));
    REQUIRE(player.has_media());
    REQUIRE(player.wait_buffered(5s));
    REQUIRE(player.duration_sec() == Approx(1.0));
    REQUIRE_FALSE(player.loop());

    // It loads paused: silence, and the clock waits.
    REQUIRE_FALSE(player.is_playing());
    auto heard = read_frames(player, k_block);
    REQUIRE(std::ranges::all_of(heard, [](float v) { return v == 0.0f; }));
    REQUIRE(player.position_sec() == 0.0);

    player.play();
    REQUIRE(player.is_playing());
    heard = read_frames(player, 4800);
    for (std::size_t f = 0; f < 4800; ++f) {
        REQUIRE(heard[f * 2] == ramp_at(f, frames));
        REQUIRE(heard[f * 2 + 1] == -ramp_at(f, frames));
    }
    REQUIRE(player.position_sec() == 0.1);

    // A pause fades out across a block, then the clock waits.
    player.pause();
    REQUIRE_FALSE(player.is_playing());
    heard = read_frames(player, k_block);
    for (std::size_t f = 0; f < k_block; ++f) {
        const float fade = 1.0f - static_cast<float>(f + 1) / static_cast<float>(k_block);
        REQUIRE(heard[f * 2] == Approx(ramp_at(4800 + f, frames) * fade).margin(1e-5));
    }
    REQUIRE(heard[(k_block - 1) * 2] == 0.0f);
    REQUIRE(player.position_sec() == Approx(0.11));
    heard = read_frames(player, k_block);
    REQUIRE(std::ranges::all_of(heard, [](float v) { return v == 0.0f; }));
    REQUIRE(player.position_sec() == Approx(0.11));

    // Played again mid-file, it fades in from where it stopped.
    player.play();
    heard = read_frames(player, 2 * k_block);
    for (std::size_t f = 0; f < 2 * k_block; ++f) {
        const float fade = std::min(1.0f, static_cast<float>(f + 1) / static_cast<float>(k_block));
        REQUIRE(heard[f * 2] == Approx(ramp_at(5280 + f, frames) * fade).margin(1e-5));
    }
    REQUIRE(heard[(2 * k_block - 1) * 2] == ramp_at(5280 + 2 * k_block - 1, frames));
}

TEST_CASE("AudioFilePlayer paused with its sound decoded ahead waits without looking", "[audio][player]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto wav = write_ramp_wav(dir, "ramp.wav", audio::k_sample_rate);
    audio::AudioFilePlayer player;
    REQUIRE(player.load(wav.string(), false));
    REQUIRE(player.wait_buffered(5s));
    // Loaded paused: the decoder looks for room no more, however long it is
    // left (a playing one would have looked five times by now).
    const auto looks = player.refill_looks();
    std::this_thread::sleep_for(100ms);
    CHECK(player.refill_looks() == looks);
    // Played, the mixer takes some, and the decoder fills it up again.
    player.play();
    (void)read_frames(player, 4 * k_block);
    CHECK(player.refill_looks() > looks);
    // Paused again, it fades out across the first block, which the decoder
    // fills again before the second (read_frames waits for that); the second
    // takes nothing, so the decoder rests.
    player.pause();
    (void)read_frames(player, 2 * k_block);
    const auto paused = player.refill_looks();
    std::this_thread::sleep_for(100ms);
    CHECK(player.refill_looks() == paused);
}

TEST_CASE("AudioFilePlayer loaded at a place plays from there", "[audio][player]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::size_t frames = audio::k_sample_rate;  // 1 s
    const auto wav = write_ramp_wav(dir, "ramp.wav", frames);

    audio::AudioFilePlayer player;
    REQUIRE(player.load(wav.string(), false, 0.5));
    // It stands there at once, before a frame is decoded.
    REQUIRE(player.position_sec() == 0.5);
    REQUIRE(player.wait_buffered(5s));
    player.play();
    // Mid-file, it fades in from there.
    auto heard = read_frames(player, 2 * k_block);
    REQUIRE(heard[(2 * k_block - 1) * 2] == ramp_at(frames / 2 + 2 * k_block - 1, frames));
    REQUIRE(player.position_sec()
            == Approx(0.5 + static_cast<double>(2 * k_block) / audio::k_sample_rate));

    // A seek sent as a file opens is not lost to its decoder starting up.
    for (int i = 0; i < 20; ++i) {
        REQUIRE(player.load(wav.string(), false));
        player.seek(0.25);
        REQUIRE(player.wait_buffered(5s));
        player.play();
        heard = read_frames(player, 2 * k_block);
        REQUIRE(heard[(2 * k_block - 1) * 2] == ramp_at(frames / 4 + 2 * k_block - 1, frames));
    }
}

TEST_CASE("AudioFilePlayer fades its level over a transition's length", "[audio][player][transition]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::size_t frames = audio::k_sample_rate;  // 1 s
    const auto wav = write_ramp_wav(dir, "ramp.wav", frames);
    audio::AudioFilePlayer player;
    REQUIRE(player.load(wav.string(), false));
    REQUIRE(player.wait_buffered(5s));
    CHECK(player.level() == 1.0f);

    // In from nothing over 100 ms: 4800 frames, evenly.
    player.fade_level(0.0f, 0ms);
    CHECK(player.level() == 0.0f);
    player.fade_level(1.0f, 100ms);
    CHECK(player.level_target() == 1.0f);
    player.play();
    auto heard = read_frames(player, 4800);
    for (std::size_t f = 0; f < 4800; f += 480) {
        const float level = static_cast<float>(f + 1) / 4800.0f;
        REQUIRE(heard[f * 2] == Approx(ramp_at(f, frames) * level).margin(1e-4));
    }
    CHECK(player.level() == 1.0f);

    // Out to nothing over 50 ms, then silence while it still plays.
    player.fade_level(0.0f, 50ms);
    heard = read_frames(player, 2400);
    CHECK(std::abs(heard[2 * 2399]) < 1e-3f);
    heard = read_frames(player, 480);
    CHECK(std::ranges::all_of(heard, [](float v) { return v == 0.0f; }));
    CHECK(player.is_playing());
}

TEST_CASE("AudioFilePlayer seeks to the frame", "[audio][player]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::size_t frames = audio::k_sample_rate;
    const auto wav = write_ramp_wav(dir, "ramp.wav", frames);

    audio::AudioFilePlayer player;
    REQUIRE(player.load(wav.string(), false));
    player.play();
    read_frames(player, 2400);

    player.seek(0.5);
    REQUIRE(player.position_sec() == 0.5);  // at once, before the decoder gets there
    // Across a block, what was playing fades out as the new place fades in;
    // then the new place, to the frame.
    auto heard = read_frames(player, 2 * k_block);
    for (std::size_t f = 0; f < k_block; ++f) {
        const float in = static_cast<float>(f + 1) / static_cast<float>(k_block);
        const float out = 1.0f - in;
        REQUIRE(heard[f * 2] == Approx(ramp_at(24000 + f, frames) * in
                                       + ramp_at(2400 + f, frames) * out)
                                    .margin(1e-5));
    }
    REQUIRE(heard[k_block * 2] == ramp_at(24000 + k_block, frames));
    REQUIRE(heard[(2 * k_block - 1) * 2] == ramp_at(24000 + 2 * k_block - 1, frames));
    REQUIRE(player.position_sec() == Approx(0.52));

    // Out of range: clamped to the file. The file's start plays as it is,
    // under what was playing fading out.
    player.seek(-3.0);
    REQUIRE(player.position_sec() == 0.0);
    heard = read_frames(player, 2 * k_block);
    REQUIRE(heard[0] == Approx(ramp_at(0, frames) + ramp_at(24960, frames) * (1.0f - 1.0f / k_block))
                            .margin(1e-5));
    REQUIRE(heard[k_block * 2] == ramp_at(k_block, frames));
}

TEST_CASE("AudioFilePlayer fades out a file replaced while it plays", "[audio][player]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::size_t frames = audio::k_sample_rate;
    const auto first = write_ramp_wav(dir, "first.wav", frames);
    const auto second = write_ramp_wav(dir, "second.wav", frames / 2);

    audio::AudioFilePlayer player;
    REQUIRE(player.load(first.string(), false));
    player.play();
    read_frames(player, 2400);

    // The next file from its start, under the first fading out.
    REQUIRE(player.load(second.string(), false));
    player.play();
    const auto heard = read_frames(player, 2 * k_block);
    for (std::size_t f = 0; f < k_block; ++f) {
        const float fade = 1.0f - static_cast<float>(f + 1) / static_cast<float>(k_block);
        REQUIRE(heard[f * 2] == Approx(ramp_at(f, frames / 2) + ramp_at(2400 + f, frames) * fade)
                                    .margin(1e-5));
    }
    REQUIRE(heard[k_block * 2] == ramp_at(k_block, frames / 2));

    // Replaced after the mixer stopped reading it (taken out of the mixer
    // first): nothing of it is left to fade out later.
    read_frames(player, 2400);
    std::this_thread::sleep_for(600ms);
    REQUIRE(player.load(first.string(), false));
    player.play();
    const auto after = read_frames(player, k_block);
    for (std::size_t f = 0; f < k_block; ++f) {
        REQUIRE(after[f * 2] == ramp_at(f, frames));
    }
}

TEST_CASE("AudioFilePlayer loops without a gap, its clock starting over", "[audio][player]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::size_t frames = audio::k_sample_rate / 4;  // 0.25 s
    const auto wav = write_ramp_wav(dir, "short.wav", frames);

    audio::AudioFilePlayer player;
    REQUIRE(player.load(wav.string(), true));
    REQUIRE(player.loop());
    player.play();
    const std::size_t total = 28800;  // 0.6 s: two passes and a bit
    const auto heard = read_frames(player, total);
    for (std::size_t f = 0; f < total; ++f) {
        REQUIRE(heard[f * 2] == ramp_at(f % frames, frames));
    }
    REQUIRE(player.position_sec() == Approx(0.1));
    REQUIRE(player.is_playing());
}

TEST_CASE("AudioFilePlayer seeks within a looping file, and loops on from there",
          "[audio][player]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::size_t frames = audio::k_sample_rate / 4;  // 0.25 s
    const auto wav = write_ramp_wav(dir, "short.wav", frames);

    audio::AudioFilePlayer player;
    REQUIRE(player.load(wav.string(), true));
    player.play();
    // Past the first restart, so the ring holds loop marks the seek must drop.
    read_frames(player, frames + 2400);
    REQUIRE(player.position_sec() == Approx(0.05));

    player.seek(0.2);  // 9600 frames in: 2400 before the end
    REQUIRE(player.position_sec() == Approx(0.2));
    const auto heard = read_frames(player, 4800);
    // Past the cross-fade.
    for (std::size_t f = k_block; f < 4800; ++f) {
        REQUIRE(heard[f * 2] == ramp_at((9600 + f) % frames, frames));
    }
    REQUIRE(player.position_sec() == Approx(0.05));
}

TEST_CASE("AudioFilePlayer clamps a seek past the end to the end", "[audio][player]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::size_t frames = audio::k_sample_rate / 4;
    const auto wav = write_ramp_wav(dir, "short.wav", frames);

    audio::AudioFilePlayer player;
    REQUIRE(player.load(wav.string(), false));
    REQUIRE(player.wait_buffered(5s));  // the length is known
    player.play();
    player.seek(10.0);
    REQUIRE(player.position_sec() == Approx(0.25));
    const auto heard = read_frames(player, k_block);
    REQUIRE(std::ranges::all_of(heard, [](float v) { return v == 0.0f; }));
    REQUIRE_FALSE(player.is_playing());
}

TEST_CASE("AudioFilePlayer stops at the end, and restarts", "[audio][player]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::size_t frames = audio::k_sample_rate / 4;
    const auto wav = write_ramp_wav(dir, "short.wav", frames);

    audio::AudioFilePlayer player;
    REQUIRE(player.load(wav.string(), false));
    player.play();
    const auto heard = read_frames(player, frames + 2400);
    REQUIRE(heard[(frames - 1) * 2] == ramp_at(frames - 1, frames));
    REQUIRE(std::all_of(heard.begin() + static_cast<std::ptrdiff_t>(frames * 2), heard.end(),
                        [](float v) { return v == 0.0f; }));
    REQUIRE_FALSE(player.is_playing());
    REQUIRE(player.position_sec() == Approx(0.25));
    REQUIRE(player.has_media());

    player.restart();
    REQUIRE(player.is_playing());
    REQUIRE(player.position_sec() == 0.0);
    REQUIRE(read_frames(player, k_block)[2] == ramp_at(1, frames));
}

TEST_CASE("AudioFilePlayer sought back from its end stays paused there", "[audio][player]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::size_t frames = audio::k_sample_rate / 4;
    const auto wav = write_ramp_wav(dir, "short.wav", frames);

    audio::AudioFilePlayer player;
    REQUIRE(player.load(wav.string(), false));
    player.play();
    (void)read_frames(player, frames + 2400);
    REQUIRE_FALSE(player.is_playing());

    // Ended shows paused, and the seek leaves it so: the clock waits.
    player.seek(0.1);
    REQUIRE_FALSE(player.is_playing());
    (void)read_frames(player, k_block);
    REQUIRE(player.position_sec() == Approx(0.1));

    // Play goes on from the new place, fading in over the first block.
    player.play();
    REQUIRE(player.is_playing());
    const auto heard = read_frames(player, 2 * k_block);
    REQUIRE(heard[k_block * 2] == ramp_at(4800 + k_block, frames));
    REQUIRE(player.position_sec() == Approx(0.12));
}

TEST_CASE("AudioFilePlayer plays mono at full level on both sides", "[audio][player]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::size_t frames = audio::k_sample_rate / 4;
    const auto wav = write_ramp_wav(dir, "mono.wav", frames, 1);

    audio::AudioFilePlayer player;
    REQUIRE(player.load(wav.string(), false));
    player.play();
    const auto heard = read_frames(player, 1000);
    for (std::size_t f = 0; f < 1000; ++f) {
        REQUIRE(heard[f * 2] == ramp_at(f, frames));
        REQUIRE(heard[f * 2 + 1] == ramp_at(f, frames));
    }
}

TEST_CASE("AudioFilePlayer decodes an MP3 and resamples it to 48 kHz", "[audio][player]")
{
    audio::AudioFilePlayer player;
    REQUIRE(player.load(fixture("audio/tone_440hz_1s.mp3").string(), false));
    REQUIRE(player.wait_buffered(5s));
    REQUIRE(player.has_media());
    // A second of tone. FFmpeg 6 counts the encoder's padding in it (1.045 s);
    // newer FFmpeg leaves the padding out, as gapless playback does (1.000 s).
    REQUIRE(player.duration_sec() == Approx(1.02).margin(0.03));
    player.play();
    const auto heard = read_frames(player, audio::k_sample_rate / 2);

    // Past the encoder's lead-in: a 440 Hz sine at half scale.
    const std::size_t from = audio::k_sample_rate / 20;
    const std::size_t to = from + audio::k_sample_rate * 2 / 5;  // 0.4 s
    int crossings = 0;
    float peak = 0.0f;
    for (std::size_t f = from + 1; f < to; ++f) {
        const float now = heard[f * 2];
        const float before = heard[(f - 1) * 2];
        crossings += (before < 0.0f) != (now < 0.0f) ? 1 : 0;
        peak = std::max(peak, std::abs(now));
    }
    REQUIRE(crossings >= 348);  // 440 Hz: 352 in 0.4 s
    REQUIRE(crossings <= 356);
    REQUIRE(peak == Approx(0.5f).margin(0.05f));
}

TEST_CASE("AudioFilePlayer keeps a video's time: a late sound track starts late",
          "[audio][player]")
{
    // The picture starts at 0 and the sound at 0.5 s. mpv counts the
    // picture's time from the start of the file, so the sound must too.
    audio::AudioFilePlayer player;
    REQUIRE(player.load(fixture("video/tone_3366cc_64x36_3s_sound_at_500ms.mp4").string(), false));
    REQUIRE(player.wait_buffered(5s));
    REQUIRE(player.duration_sec() == Approx(3.0).margin(0.05));
    player.play();
    const auto peak = [](const std::vector<float>& frames, std::size_t from, std::size_t to) {
        float p = 0.0f;
        for (std::size_t i = from * audio::k_channels; i < to * audio::k_channels; ++i) {
            p = std::max(p, std::abs(frames[i]));
        }
        return p;
    };
    const auto at = [](double sec) { return static_cast<std::size_t>(sec * audio::k_sample_rate); };
    auto heard = read_frames(player, at(1.0));
    // Silence until just before 0.5 s, the tone (-18 dBFS) soon after.
    REQUIRE(peak(heard, 0, at(0.45)) == 0.0f);
    REQUIRE(peak(heard, at(0.55), at(1.0)) > 0.1f);

    // A seek counts the same way: to 0.2 s is silence for 0.3 s (once the
    // tone playing before it has faded out), then sound.
    player.seek(0.2);
    heard = read_frames(player, at(0.6));
    REQUIRE(peak(heard, k_block, at(0.25)) == 0.0f);
    REQUIRE(peak(heard, at(0.35), at(0.6)) > 0.1f);
    // Past the start of the sound, the sound is there at once.
    player.seek(1.0);
    heard = read_frames(player, at(0.1));
    REQUIRE(peak(heard, 0, at(0.1)) > 0.1f);
}

TEST_CASE("AudioFilePlayer turns down what it cannot play", "[audio][player]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    audio::AudioFilePlayer player;

    // Not there: refused at once.
    REQUIRE_FALSE(player.load((std::filesystem::path(dir.path().toStdString()) / "gone.mp3").string(),
                              false));
    REQUIRE_FALSE(player.has_media());
    REQUIRE_FALSE(player.load("", false));

    // There, but not sound: refused a moment later, as mpv did.
    const auto text = std::filesystem::path(dir.path().toStdString()) / "notes.mp3";
    std::ofstream(text) << "not audio at all\n";
    REQUIRE(player.load(text.string(), false));
    REQUIRE(wait_until([&] { return !player.has_media(); }));
    player.play();
    REQUIRE_FALSE(player.is_playing());

    // A video with no sound track.
    REQUIRE(player.load(fixture("video/solid_3366cc_64x36.mp4").string(), false));
    REQUIRE(wait_until([&] { return !player.has_media(); }));

    // Unloaded: nothing, and silence.
    const auto wav = write_ramp_wav(dir, "ramp.wav", 4800);
    REQUIRE(player.load(wav.string(), false));
    player.play();
    player.unload();
    REQUIRE_FALSE(player.has_media());
    REQUIRE(player.duration_sec() == 0.0);
    auto out = block_of_silence();
    out[0] = 1.0f;
    player.read(out.data(), k_block);
    REQUIRE(std::ranges::all_of(out, [](float v) { return v == 0.0f; }));
}

// --- AudioEngine -----------------------------------------------------------

TEST_CASE("AudioEngine: a file through a stack and an output to Listen", "[audio][engine]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::size_t frames = audio::k_sample_rate / 4;
    const auto wav = write_ramp_wav(dir, "ramp.wav", frames);

    audio::AudioEngine engine(audio::AudioEngine::Backend::Null);
    auto& mixer = engine.mixer();
    mixer.set_strip(audio::StripKind::Output, "o1", ChannelMix{0.0, false, true});
    mixer.set_routing({{"main"}, {{"o1", {"main"}}}});
    audio::AudioFilePlayer player;
    REQUIRE(player.load(wav.string(), false));
    mixer.add_source("main", &player);
    player.play();

    std::vector<float> listen(4800 * 2);
    REQUIRE(player.wait_buffered(5s));
    engine.render(listen.data(), 4800);
    for (std::size_t f = 0; f < 4800; ++f) {
        REQUIRE(listen[f * 2] == ramp_at(f, frames));
    }
    REQUIRE(player.position_sec() == 0.1);
    REQUIRE(mixer.meter(audio::StripKind::Stack, "main").level_db[0] > -60.0f);
    mixer.remove_source(&player);
}

TEST_CASE("AudioEngine keeps time on a timer when there is no device", "[audio][engine]")
{
    audio::AudioEngine engine(audio::AudioEngine::Backend::Null);
    ConstSource source(0.1f, 0.1f);
    engine.mixer().set_routing({{"main"}, {}});
    engine.mixer().add_source("main", &source);

    engine.start_timer();
    REQUIRE(engine.running());
    REQUIRE(engine.device_name().empty());
    REQUIRE(wait_until([&] { return source.reads >= 5; }));
    engine.stop();
    REQUIRE_FALSE(engine.running());
    const int reads = source.reads;
    std::this_thread::sleep_for(50ms);
    REQUIRE(source.reads == reads);
    engine.mixer().remove_source(&source);
}

TEST_CASE("AudioEngine runs on a device's clock", "[audio][engine]")
{
    // miniaudio's null device keeps real time without making a sound.
    audio::AudioEngine engine(audio::AudioEngine::Backend::Null);
    ConstSource source(0.1f, 0.1f);
    engine.mixer().set_routing({{"main"}, {}});
    engine.mixer().add_source("main", &source);

    REQUIRE(engine.start());
    REQUIRE(engine.running());
    REQUIRE_FALSE(engine.device_name().empty());
    REQUIRE(wait_until([&] { return source.reads >= 5; }));

    // The device stops, as one unplugged does: the timer keeps the time.
    engine.stop_device_for_tests();
    std::this_thread::sleep_for(150ms);  // past the 100 ms the timer waits
    const int stalled_at = source.reads;
    REQUIRE(wait_until([&] { return source.reads >= stalled_at + 5; }));

    // Started again while on the timer, it plays on a device again, as the
    // app moves from the timer it starts on to the Listen device.
    REQUIRE(engine.start());
    REQUIRE(engine.running());
    REQUIRE_FALSE(engine.device_name().empty());
    engine.stop();
    REQUIRE_FALSE(engine.running());
    engine.mixer().remove_source(&source);
}

// --- DeviceSink ---------------------------------------------------------------

TEST_CASE("DeviceSink plays an output's mix on a device of its own", "[audio][device]")
{
    using Backend = audio::AudioEngine::Backend;
    // The null device: real time, no sound.
    const auto devices = audio::playback_devices(Backend::Null);
    REQUIRE_FALSE(devices.empty());

    audio::AudioEngine engine(Backend::Null);
    ConstSource half(0.5f, 0.5f);
    engine.mixer().set_routing({{"main"}, {{"out", {"main"}}}});
    engine.mixer().add_source("main", &half);
    engine.start_timer();

    audio::DeviceSink sink(Backend::Null);
    REQUIRE(sink.start(engine.mixer(), "out", devices.front().id));
    REQUIRE(sink.running());
    REQUIRE(sink.device_name() == devices.front().name);
    // The device takes what the mixer gives it, at its own pace, and the
    // buffer between them settles near its target.
    REQUIRE(wait_until([&] { return sink.frames_played() > audio::k_sample_rate / 2; }, 5s));
    const auto fill = sink.buffer().fill();
    CHECK(fill > 0);
    CHECK(fill < 3 * sink.buffer().target());

    sink.stop();
    CHECK_FALSE(sink.running());
    CHECK(sink.device_name().empty());
    engine.stop();
    engine.mixer().remove_source(&half);
}

TEST_CASE("DeviceSink stays silent on a device that is not there", "[audio][device]")
{
    using Backend = audio::AudioEngine::Backend;
    audio::Mixer mixer;
    mixer.set_routing({{"main"}, {{"out", {"main"}}}});
    audio::DeviceSink sink(Backend::Null);
    // Never the default instead: a projector's sound must not come out of
    // the room's speakers.
    CHECK_FALSE(sink.start(mixer, "out", "unplugged-projector"));
    CHECK_FALSE(sink.running());
    CHECK_FALSE(sink.start(mixer, "out", ""));
    // Nothing joined the mixer: a block reaches no sink.
    std::vector<float> listen(audio::k_block_frames * audio::k_channels);
    mixer.process(listen.data(), audio::k_block_frames);
    CHECK(sink.buffer().fill() == 0);
}

// --- PipeWireSource -----------------------------------------------------------

namespace {

/// The float samples of a WAV ``pw-record`` wrote (``--format f32``).
std::vector<float> wav_floats(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = f.readAll();
    const auto at = bytes.indexOf("data");
    if (at < 0 || at + 8 > bytes.size()) {
        return {};
    }
    std::uint32_t size = 0;
    std::memcpy(&size, bytes.constData() + at + 4, 4);
    const auto start = at + 8;
    const auto have = std::min<qsizetype>(size, bytes.size() - start);
    std::vector<float> out(static_cast<std::size_t>(have) / sizeof(float));
    std::memcpy(out.data(), bytes.constData() + start, out.size() * sizeof(float));
    return out;
}

} // namespace

TEST_CASE("PipeWireSource offers an output's mix as a microphone", "[audio][pipewire]")
{
    if (!audio::pipewire_available()) {
        SKIP("no PipeWire here");
    }
    const QString recorder = QStandardPaths::findExecutable(QStringLiteral("pw-record"));
    audio::AudioEngine engine(audio::AudioEngine::Backend::Null);
    ConstSource tone(0.5f, 0.25f);
    engine.mixer().set_routing({{"main"}, {{"out", {"main"}}}});
    engine.mixer().add_source("main", &tone);
    engine.start_timer();

    audio::PipeWireSource mic;
    REQUIRE(mic.start(engine.mixer(), "out", "SeventhSlide MicTest"));
    REQUIRE(mic.running());
    CHECK(mic.node_name() == "seventhslide_mictest");
    // Apps on the PulseAudio API (browsers, Zoom) list it with the sources.
    const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (!pactl.isEmpty()) {
        CHECK(wait_until(
            [&] {
                QProcess list;
                list.start(pactl, {QStringLiteral("list"), QStringLiteral("short"),
                                   QStringLiteral("sources")});
                list.waitForFinished(5000);
                return list.readAllStandardOutput().contains("seventhslide_mictest");
            },
            5s));
    }

    if (!recorder.isEmpty()) {
        // An app records from it: what it hears is the output's mix, each
        // side at its level.
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("mic.wav"));
        QProcess record;
        record.start(recorder, {QStringLiteral("--target"), QString::fromStdString(mic.node_name()),
                                QStringLiteral("--format"), QStringLiteral("f32"),
                                QStringLiteral("--rate"), QStringLiteral("48000"),
                                QStringLiteral("--channels"), QStringLiteral("2"), file});
        REQUIRE(record.waitForStarted(5000));
        REQUIRE(wait_until([&] { return mic.frames_sent() > audio::k_sample_rate; }, 10s));
        record.terminate();
        REQUIRE(record.waitForFinished(5000));
        const auto heard = wav_floats(file);
        REQUIRE(heard.size() > audio::k_sample_rate);
        // The last half second: past the buffer filling.
        const std::size_t from = heard.size() - audio::k_sample_rate;
        bool left_ok = true;
        bool right_ok = true;
        for (std::size_t i = from; i + 1 < heard.size(); i += 2) {
            left_ok = left_ok && std::abs(heard[i] - 0.5f) < 1e-3f;
            right_ok = right_ok && std::abs(heard[i + 1] - 0.25f) < 1e-3f;
        }
        CHECK(left_ok);
        CHECK(right_ok);
    }

    mic.stop();
    CHECK_FALSE(mic.running());
    CHECK(mic.node_name().empty());
    engine.stop();
    engine.mixer().remove_source(&tone);
}
