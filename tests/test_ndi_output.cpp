#include <catch2/catch_test_macros.hpp>

#include "ndi_test_receiver.hpp"
#include "test_qt_app.hpp"
#include "test_wait.hpp"

#include <seventhslide/audio/audio_engine.hpp>
#include <seventhslide/audio/audio_source.hpp>
#include <seventhslide/library/audio_mix_store.hpp>

#include <seventhslide/render/ndi_output.hpp>
#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <QCoreApplication>
#include <QImage>
#include <QSize>
#include <QString>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>
#include <vector>

using seventhslide::render::NdiSink;
using seventhslide::render::ndi_default_source_name;
using seventhslide::render::ndi_runtime_available;
using seventhslide::render::ndi_sanitize_source_name;
using seventhslide::test::run_events_for;

namespace {

/// A 1 kHz sine, left at half scale and right at a quarter, so the two
/// channels can be told apart at the far end.
class StereoTone final : public seventhslide::audio::AudioSource {
public:
    void read(float* out, std::size_t frames) override
    {
        for (std::size_t i = 0; i < frames; ++i) {
            const auto v = static_cast<float>(
                std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(n_++) / 48000.0));
            out[2 * i] = 0.5f * v;
            out[2 * i + 1] = 0.25f * v;
        }
    }

private:
    std::uint64_t n_ = 0;
};

/// The largest magnitude of channel ``channel`` in interleaved stereo.
float peak(const std::vector<float>& interleaved, int channel)
{
    float p = 0.0f;
    for (std::size_t i = static_cast<std::size_t>(channel); i < interleaved.size(); i += 2) {
        p = std::max(p, std::abs(interleaved[i]));
    }
    return p;
}

}  // namespace

TEST_CASE("NDI source names are sanitized for mDNS", "[render][ndi]")
{
    REQUIRE(ndi_sanitize_source_name(QStringLiteral("  Main  "))
            == QStringLiteral("Main"));
    // Parentheses and commas are how the runtime and receivers split
    // machine/source names; strip them rather than emit an ambiguous name.
    REQUIRE(ndi_sanitize_source_name(QStringLiteral("Main (Left), rear"))
            == QStringLiteral("Main Left rear"));
    REQUIRE(ndi_sanitize_source_name(QStringLiteral("a\tb\nc")) == QStringLiteral("a b c"));
    REQUIRE(ndi_sanitize_source_name(QStringLiteral("   ")).isEmpty());
    REQUIRE(ndi_sanitize_source_name(QString()).isEmpty());
    // Non-ASCII names are legal and must survive.
    REQUIRE(ndi_sanitize_source_name(QStringLiteral("Café")) == QStringLiteral("Café"));
}

TEST_CASE("NDI default source name is derived from the output", "[render][ndi]")
{
    REQUIRE(ndi_default_source_name(QStringLiteral("Main"))
            == QStringLiteral("SeventhSlide Main"));
    REQUIRE(ndi_default_source_name(QString()) == QStringLiteral("SeventhSlide"));
    REQUIRE(ndi_default_source_name(QStringLiteral("  ")) == QStringLiteral("SeventhSlide"));
}

TEST_CASE("NDI receivers are told the keepalive's frame rate", "[render][ndi]")
{
    using seventhslide::render::OffscreenSink;
    CHECK(seventhslide::render::k_ndi_frame_rate_d == 1);
    CHECK(seventhslide::render::k_ndi_frame_rate_n == 1000 / OffscreenSink::k_keepalive_ms);
}

TEST_CASE("NdiSink refuses to start without a source name", "[render][ndi]")
{
    seventhslide::test::ensure_qt_application();

    NdiSink sink;
    REQUIRE_FALSE(sink.start(QString(), QSize(640, 360)));
    REQUIRE_FALSE(sink.is_live());
    REQUIRE_FALSE(sink.last_error().isEmpty());
    REQUIRE(sink.status().receivers == -1);
}

TEST_CASE("NdiSink reports the missing runtime instead of crashing", "[render][ndi]")
{
    seventhslide::test::ensure_qt_application();

    if (ndi_runtime_available()) {
        SKIP("an NDI runtime is installed on this machine");
    }
    NdiSink sink;
    REQUIRE_FALSE(sink.start(QStringLiteral("SeventhSlide Test"), QSize(640, 360)));
    REQUIRE_FALSE(sink.is_live());
    REQUIRE(sink.last_error().contains(QStringLiteral("NDI")));
}

TEST_CASE("NdiSink streams to a real runtime", "[render][ndi][live]")
{
    seventhslide::test::ensure_qt_application();

    if (!ndi_runtime_available()) {
        SKIP("no NDI runtime installed");
    }

    NdiSink sink;
    REQUIRE(sink.start(QStringLiteral("SeventhSlide SelfTest"), QSize(640, 360)));
    REQUIRE(sink.is_live());
    REQUIRE(sink.source_name() == QStringLiteral("SeventhSlide SelfTest"));
    REQUIRE(sink.output_size() == QSize(640, 360));
    // No frame presented yet: the base fills with the letterbox color and the
    // sink still streams, so receivers see black rather than nothing.
    REQUIRE(sink.status().receivers >= 0);

    // Let the keepalive timer run: this exercises deliver() on unchanged
    // frames, the ping-pong buffers, and the status poll.
    run_events_for(250);
    REQUIRE(sink.is_live());
    REQUIRE(sink.status().receivers >= 0);

    SECTION("a presented frame resizes the stream")
    {
        seventhslide::scene::SceneFrame frame;
        frame.canvas_width = 1280;
        frame.canvas_height = 720;
        sink.present(seventhslide::scene::share_frame(frame));
        REQUIRE(sink.output_size() == QSize(1280, 720));
        run_events_for(100);
        REQUIRE(sink.is_live());
    }

    SECTION("alpha can be toggled on a live sender")
    {
        REQUIRE_FALSE(sink.alpha_enabled());
        sink.set_alpha_enabled(true);
        REQUIRE(sink.alpha_enabled());
        run_events_for(100);
        REQUIRE(sink.is_live());
        sink.set_alpha_enabled(false);
        run_events_for(100);
        REQUIRE(sink.is_live());
    }

    sink.stop();
    REQUIRE_FALSE(sink.is_live());
    REQUIRE(sink.status().receivers == -1);

    // Restarting the same sink must work (destination re-enable).
    REQUIRE(sink.start(QStringLiteral("SeventhSlide SelfTest"), QSize(320, 180)));
    REQUIRE(sink.is_live());
    sink.stop();
}

TEST_CASE("NdiSink odd canvas sizes are rounded even", "[render][ndi]")
{
    seventhslide::test::ensure_qt_application();

    if (!ndi_runtime_available()) {
        SKIP("no NDI runtime installed");
    }
    NdiSink sink;
    REQUIRE(sink.start(QStringLiteral("SeventhSlide OddSize"), QSize(641, 361)));
    REQUIRE(sink.output_size() == QSize(640, 360));
    // Unlike the virtual camera, NDI is not capped at 1080p.
    seventhslide::scene::SceneFrame frame;
    frame.canvas_width = 3840;
    frame.canvas_height = 2160;
    sink.present(seventhslide::scene::share_frame(frame));
    REQUIRE(sink.output_size() == QSize(3840, 2160));
    sink.stop();
}

TEST_CASE("NdiSink sends its output's mix as NDI audio", "[render][ndi][live][audio]")
{
    namespace audio = seventhslide::audio;
    seventhslide::test::ensure_qt_application();
    if (!ndi_runtime_available()) {
        SKIP("no NDI runtime installed");
    }
    // The mixer on its own timer: a tone on stack "main", which output "out"
    // carries.
    audio::AudioEngine engine(audio::AudioEngine::Backend::Null);
    StereoTone tone;
    engine.mixer().set_routing({{"main"}, {{"out", {"main"}}}});
    engine.mixer().add_source("main", &tone);
    engine.start_timer();

    NdiSink sink;
    sink.set_audio(&engine.mixer(), "out", true);
    REQUIRE_FALSE(sink.sending_audio());  // not live yet
    REQUIRE(sink.start(QStringLiteral("SeventhSlide AudioTest"), QSize(320, 180)));
    REQUIRE(sink.sending_audio());

    seventhslide::test::NdiTestReceiver receiver;
    REQUIRE(receiver.ready());
    if (!receiver.connect("SeventhSlide AudioTest", std::chrono::seconds(10))) {
        engine.stop();
        SKIP("this machine does not see its own NDI sources (mDNS)");
    }
    // A receiver's first frames can be from before it settled: take a second
    // and judge the last half.
    auto heard = receiver.capture(48000, std::chrono::seconds(10));
    REQUIRE(heard.sample_rate == 48000);
    REQUIRE(heard.channels == 2);
    REQUIRE(heard.fourcc == seventhslide::render::ndi::k_fourcc_fltp);
    REQUIRE(heard.interleaved.size() >= 48000 * 2);
    const std::vector<float> settled(heard.interleaved.end() - 24000 * 2, heard.interleaved.end());
    // Left and right arrive as sent, each on its own side.
    CHECK(std::abs(peak(settled, 0) - 0.5f) < 0.02f);
    CHECK(std::abs(peak(settled, 1) - 0.25f) < 0.02f);
    CHECK(sink.audio_frames_sent() >= 48000);

    // The output's fader acts on what is sent: -6 dB is half.
    engine.mixer().set_strip(audio::StripKind::Output, "out",
                             seventhslide::library::ChannelMix{-6.0206, false, false});
    (void)receiver.capture(24000, std::chrono::seconds(5));
    heard = receiver.capture(24000, std::chrono::seconds(5));
    CHECK(std::abs(peak(heard.interleaved, 0) - 0.25f) < 0.02f);

    // Send audio off: the pictures carry on, the sound stops.
    sink.set_audio(&engine.mixer(), "out", false);
    CHECK_FALSE(sink.sending_audio());
    CHECK(sink.is_live());
    sink.set_audio(&engine.mixer(), "out", true);
    CHECK(sink.sending_audio());

    // Stopped, it has left the mixer: the clock runs on without it.
    sink.stop();
    CHECK_FALSE(sink.sending_audio());
    run_events_for(50);
    engine.stop();
    engine.mixer().remove_source(&tone);
}

TEST_CASE("NdiSink sends its sound evenly when the mixer runs in bursts",
          "[render][ndi][live][audio]")
{
    namespace audio = seventhslide::audio;
    seventhslide::test::ensure_qt_application();
    if (!ndi_runtime_available()) {
        SKIP("no NDI runtime installed");
    }
    // The mixer as a busy Listen device drives it: four blocks at once every
    // 40 ms, never one every 10 ms.
    audio::AudioEngine engine(audio::AudioEngine::Backend::Null);
    StereoTone tone;
    engine.mixer().set_routing({{"main"}, {{"out", {"main"}}}});
    engine.mixer().add_source("main", &tone);

    NdiSink sink;
    sink.set_audio(&engine.mixer(), "out", true);
    REQUIRE(sink.start(QStringLiteral("SeventhSlide BurstTest"), QSize(320, 180)));
    REQUIRE(sink.sending_audio());

    std::atomic<bool> done{false};
    std::thread clock([&] {
        std::vector<float> listen(audio::k_block_frames * audio::k_channels);
        auto next = std::chrono::steady_clock::now();
        while (!done) {
            for (int i = 0; i < 4; ++i) {
                engine.render(listen.data(), audio::k_block_frames);
            }
            next += std::chrono::milliseconds(40);
            std::this_thread::sleep_until(next);
        }
    });

    seventhslide::test::NdiTestReceiver receiver;
    REQUIRE(receiver.ready());
    const bool connected = receiver.connect("SeventhSlide BurstTest", std::chrono::seconds(10));
    seventhslide::test::NdiTestReceiver::Audio heard;
    if (connected) {
        // Past the start, while the buffer fills: then two seconds.
        (void)receiver.capture(24000, std::chrono::seconds(10));
        heard = receiver.capture(96000, std::chrono::seconds(10));
    }
    done = true;
    clock.join();
    sink.stop();
    engine.mixer().remove_source(&tone);
    if (!connected) {
        SKIP("this machine does not see its own NDI sources (mDNS)");
    }

    REQUIRE(heard.interleaved.size() >= 96000 * 2);
    // Block by block, each frame timed to follow the last exactly, as a
    // receiver plays it.
    CHECK(std::ranges::all_of(heard.frame_samples, [](int n) {
        return n == static_cast<int>(audio::k_block_frames);
    }));
    int jumps = 0;
    for (std::size_t i = 1; i < heard.timecodes.size(); ++i) {
        const std::int64_t length =
            std::int64_t{heard.frame_samples[i - 1]} * 10'000'000 / audio::k_sample_rate;
        jumps += heard.timecodes[i] - heard.timecodes[i - 1] != length ? 1 : 0;
    }
    CHECK(jumps == 0);
    // And the tone unbroken: no step larger than a 1 kHz sine at half scale
    // makes between samples.
    const float max_step = 0.5f * 2.0f * std::numbers::pi_v<float> * 1000.0f / 48000.0f * 1.1f;
    int clicks = 0;
    for (std::size_t i = 2; i < heard.interleaved.size(); i += 2) {
        clicks += std::abs(heard.interleaved[i] - heard.interleaved[i - 2]) > max_step ? 1 : 0;
    }
    CHECK(clicks == 0);
}

TEST_CASE("NdiSink sends its picture no taller than its limit, or its sound alone",
          "[render][ndi][live][audio]")
{
    namespace audio = seventhslide::audio;
    seventhslide::test::ensure_qt_application();
    if (!ndi_runtime_available()) {
        SKIP("no NDI runtime installed");
    }
    audio::AudioEngine engine(audio::AudioEngine::Backend::Null);
    StereoTone tone;
    engine.mixer().set_routing({{"main"}, {{"out", {"main"}}}});
    engine.mixer().add_source("main", &tone);
    engine.start_timer();

    NdiSink sink;
    sink.set_audio(&engine.mixer(), "out", true);
    sink.set_max_height(360);
    REQUIRE(sink.start(QStringLiteral("SeventhSlide FormatTest"), QSize(1280, 720)));
    CHECK(sink.output_size() == QSize(640, 360));

    seventhslide::test::NdiTestReceiver receiver;
    REQUIRE(receiver.ready());
    if (!receiver.connect("SeventhSlide FormatTest", std::chrono::seconds(10), true)) {
        sink.stop();
        engine.stop();
        engine.mixer().remove_source(&tone);
        SKIP("this machine does not see its own NDI sources (mDNS)");
    }
    const auto events = [] { QCoreApplication::processEvents(); };
    // What arrives after a change has settled: what was in flight goes first.
    const auto settled = [&] {
        (void)receiver.watch(std::chrono::milliseconds(500), events);
        return receiver.watch(std::chrono::milliseconds(1500), events);
    };

    // Its picture at its limit, its sound beside it.
    auto seen = settled();
    CHECK(seen.video_frames > 0);
    CHECK(seen.width == 640);
    CHECK(seen.height == 360);
    CHECK(seen.audio_frames > 0);

    // The limit lifted, the same sender sends the canvas's size.
    sink.set_max_height(0);
    seen = settled();
    CHECK(seen.width == 1280);
    CHECK(seen.height == 720);

    // Sound alone: no pictures, the sound carries on, and the receiver is
    // still counted.
    sink.set_video_enabled(false);
    seen = settled();
    CHECK(seen.video_frames == 0);
    CHECK(seen.audio_frames > 0);
    CHECK(sink.status().receivers >= 1);

    // And the pictures back.
    sink.set_video_enabled(true);
    seen = settled();
    CHECK(seen.video_frames > 0);
    CHECK(sink.is_live());

    sink.stop();
    run_events_for(50);
    engine.stop();
    engine.mixer().remove_source(&tone);
}

TEST_CASE("NdiSink sends pictures only without a mixer", "[render][ndi][audio]")
{
    seventhslide::test::ensure_qt_application();
    if (!ndi_runtime_available()) {
        SKIP("no NDI runtime installed");
    }
    NdiSink sink;
    REQUIRE(sink.start(QStringLiteral("SeventhSlide NoAudio"), QSize(320, 180)));
    CHECK_FALSE(sink.sending_audio());
    sink.set_audio(nullptr, "out", true);
    CHECK_FALSE(sink.sending_audio());
    CHECK(sink.audio_frames_sent() == 0);
    sink.stop();
}
