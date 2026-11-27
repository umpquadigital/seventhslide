// Receiving NDI, from this machine's own NDI output: the finder lists it, a
// receiver takes its pictures and its sound. Skipped without an NDI runtime,
// or where this machine does not see its own sources (mDNS).

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"
#include "test_wait.hpp"

#include <seventhslide/audio/audio_engine.hpp>
#include <seventhslide/audio/audio_source.hpp>
#include <seventhslide/render/ndi_output.hpp>
#include <seventhslide/render/ndi_receive.hpp>
#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using seventhslide::render::NdiFinder;
using seventhslide::render::NdiReceiver;
using seventhslide::render::NdiSink;

namespace {

using seventhslide::test::wait_until;

/// A 1 kHz sine at half scale on both sides.
class Tone final : public seventhslide::audio::AudioSource {
public:
    void read(float* out, std::size_t frames) override
    {
        for (std::size_t i = 0; i < frames; ++i) {
            const auto v = static_cast<float>(
                0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(n_++) / 48000.0));
            out[2 * i] = v;
            out[2 * i + 1] = v;
        }
    }

private:
    std::uint64_t n_ = 0;
};

} // namespace

TEST_CASE("NDI from this machine is found and received, picture and sound",
          "[render][ndi][live][receive]")
{
    namespace audio = seventhslide::audio;
    seventhslide::test::ensure_qt_application();
    if (!seventhslide::render::ndi_receive_available()) {
        SKIP("no NDI runtime that receives");
    }
    // Our own output, sending a red frame and a tone.
    audio::AudioEngine engine(audio::AudioEngine::Backend::Null);
    Tone tone;
    engine.mixer().set_routing({{"main"}, {{"out", {"main"}}}});
    engine.mixer().add_source("main", &tone);
    engine.start_timer();
    NdiSink sink;
    sink.set_audio(&engine.mixer(), "out", true);
    REQUIRE(sink.start(QStringLiteral("SeventhSlide ReceiveTest"), QSize(320, 180)));
    seventhslide::scene::SceneFrame red;
    red.canvas_width = 320;
    red.canvas_height = 180;
    red.background.type = seventhslide::layout::BackgroundType::Color;
    red.background.color = "#ff0000";
    sink.present(seventhslide::scene::share_frame(red));

    // Found by name.
    NdiFinder finder;
    REQUIRE(finder.available());
    std::string name;
    const bool found = wait_until(
        [&] {
            for (const auto& s : finder.sources()) {
                if (s.find("SeventhSlide ReceiveTest") != std::string::npos) {
                    name = s;
                }
            }
            return !name.empty();
        },
        10000);
    if (!found) {
        sink.stop();
        engine.stop();
        engine.mixer().remove_source(&tone);
        SKIP("this machine does not see its own NDI sources (mDNS)");
    }

    // Received: its picture, at its size, and told when each arrives.
    std::atomic<int> told{0};
    NdiReceiver receiver(name, [&] { ++told; });
    REQUIRE(receiver.available());
    // NDI compresses (4:2:2): red arrives as nearly red.
    const auto reddish = [](const QColor& c) { return c.red() > 230 && c.green() < 30 && c.blue() < 30; };
    REQUIRE(wait_until([&] { return !receiver.frame().isNull()
                                    && reddish(receiver.frame().pixelColor(160, 90)); },
                       10000));
    CHECK(receiver.frame().size() == QSize(320, 180));
    CHECK(told > 0);
    const auto stats = receiver.stats();
    CHECK(stats.size == QSize(320, 180));
    CHECK(stats.frame_rate > 0.0);
    CHECK(stats.frames > 0);

    // NDI's preview stream: smaller, at the source's frame rate.
    {
        NdiReceiver preview(name, {}, NdiReceiver::Bandwidth::Preview);
        REQUIRE(wait_until([&] { return !preview.frame().isNull(); }, 10000));
        CHECK(preview.stats().frame_rate == stats.frame_rate);
    }

    // And its sound, at its level.
    std::vector<float> block(audio::k_block_frames * audio::k_channels);
    float loudest = 0.0f;
    REQUIRE(wait_until(
        [&] {
            receiver.sound().read(block.data(), audio::k_block_frames);
            loudest = 0.0f;
            for (const float v : block) {
                loudest = std::max(loudest, std::abs(v));
            }
            return loudest > 0.45f;
        },
        10000));
    CHECK(loudest < 0.55f);

    sink.stop();
    engine.stop();
    engine.mixer().remove_source(&tone);
}

TEST_CASE("NDI receiving without a runtime that receives finds nothing", "[render][ndi]")
{
    seventhslide::test::ensure_qt_application();
    if (seventhslide::render::ndi_receive_available()) {
        SKIP("this machine has an NDI runtime");
    }
    NdiFinder finder;
    CHECK_FALSE(finder.available());
    CHECK(finder.sources().empty());
    NdiReceiver receiver("HOST (Camera)", {});
    CHECK_FALSE(receiver.available());
    CHECK(receiver.frame().isNull());
}
