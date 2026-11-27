#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"

#include <seventhslide/audio/audio_engine.hpp>
#include <seventhslide/render/bmd_output.hpp>
#include <seventhslide/render/bmd_runtime.hpp>

#include <QSize>
#include <QString>

#include <array>
#include <cstdint>
#include <limits>

using seventhslide::render::BmdDisplayModeInfo;
using seventhslide::render::BmdSink;
using seventhslide::render::bmd_audio_pack;
using seventhslide::render::bmd_audio_target_frames;
using seventhslide::render::bmd_audio_top_up;
using seventhslide::render::bmd_list_devices;
using seventhslide::render::bmd_missing_message;
using seventhslide::render::bmd_no_device_message;
using seventhslide::render::bmd_runtime_available;

TEST_CASE("a Blackmagic sink with no device fails cleanly", "[render][bmd]")
{
    seventhslide::test::ensure_qt_application();

    BmdSink sink;
    REQUIRE_FALSE(sink.is_live());

    // Empty target: no device chosen at all.
    REQUIRE_FALSE(sink.start(QString(), QSize(1920, 1080)));
    REQUIRE_FALSE(sink.last_error().isEmpty());
    REQUIRE_FALSE(sink.is_live());

    // A device id that is not attached. Whichever of the two failures applies,
    // the operator gets the message that names their actual problem.
    REQUIRE_FALSE(sink.start(QStringLiteral("no-such-device"), QSize(1920, 1080)));
    REQUIRE_FALSE(sink.is_live());
    if (bmd_runtime_available()) {
        REQUIRE(sink.last_error() == bmd_no_device_message());
    } else {
        REQUIRE(sink.last_error() == bmd_missing_message());
    }
}

TEST_CASE("A Blackmagic card keeps two frames of sound, at least 40 ms", "[render][bmd][audio]")
{
    const auto mode = [](long long duration, long long scale) {
        BmdDisplayModeInfo m;
        m.frame_duration = duration;
        m.time_scale = scale;
        return m;
    };
    // 1080p29.97: 1601.6 frames of sound to a picture.
    CHECK(bmd_audio_target_frames(mode(1001, 30000)) == 3204);
    // 1080i50 (25 frames): 1920 a picture.
    CHECK(bmd_audio_target_frames(mode(1000, 25000)) == 3840);
    // 1080p60: two pictures are only 33 ms, so the floor holds.
    CHECK(bmd_audio_target_frames(mode(1000, 60000)) == 1920);
    // A mode with no rate: the floor.
    CHECK(bmd_audio_target_frames(mode(0, 0)) == 1920);

    // Topped up to the target; nothing while it holds that much.
    CHECK(bmd_audio_top_up(0, 3204) == 3204);
    CHECK(bmd_audio_top_up(1200, 3204) == 2004);
    CHECK(bmd_audio_top_up(3204, 3204) == 0);
    CHECK(bmd_audio_top_up(5000, 3204) == 0);
}

TEST_CASE("A Blackmagic card's sound is 32-bit integers, clipped", "[render][bmd][audio]")
{
    const std::array<float, 6> in{0.0f, 1.0f, -1.0f, 0.5f, 1.7f, -3.0f};
    std::array<std::int32_t, 6> out{};
    bmd_audio_pack(in.data(), in.size(), out.data());
    constexpr auto top = std::numeric_limits<std::int32_t>::max();
    CHECK(out[0] == 0);
    CHECK(out[1] == top);
    CHECK(out[2] == -top);
    CHECK(out[3] == 1073741824);  // half scale, rounded
    CHECK(out[4] == top);          // clipped, not wrapped
    CHECK(out[5] == -top);
}

TEST_CASE("A Blackmagic sink joins the mixer only once a card takes its sound",
          "[render][bmd][audio]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::audio::AudioEngine engine(seventhslide::audio::AudioEngine::Backend::Null);
    engine.mixer().set_routing({{"main"}, {{"out", {"main"}}}});

    BmdSink sink;
    sink.set_audio(&engine.mixer(), "out", true);
    // Not live: nothing to play it on.
    CHECK_FALSE(sink.sending_audio());
    REQUIRE_FALSE(sink.start(QStringLiteral("no-such-device"), QSize(1920, 1080)));
    CHECK_FALSE(sink.sending_audio());
    // Switched off and on again without a card: still nothing, and safe.
    sink.set_audio(&engine.mixer(), "out", false);
    sink.set_audio(nullptr, "out", true);
    CHECK_FALSE(sink.sending_audio());
    sink.stop();
}

TEST_CASE("stopping a Blackmagic sink that never started is safe", "[render][bmd]")
{
    seventhslide::test::ensure_qt_application();

    BmdSink sink;
    sink.stop();  // must not join an unstarted thread or touch a null device
    sink.stop();
    REQUIRE_FALSE(sink.is_live());
}

TEST_CASE("Blackmagic sink settings round-trip before start", "[render][bmd]")
{
    seventhslide::test::ensure_qt_application();

    BmdSink sink;
    REQUIRE(sink.mode_key().isEmpty());
    REQUIRE_FALSE(sink.alpha_enabled());
    REQUIRE_FALSE(sink.keying_active());  // nothing negotiated yet

    sink.set_mode_key(QStringLiteral("Hi59"));
    REQUIRE(sink.mode_key() == QStringLiteral("Hi59"));

    sink.set_alpha_enabled(true);
    REQUIRE(sink.alpha_enabled());
    // Asking for alpha is not the same as getting a keyer: that is only known
    // once a card has actually been opened.
    REQUIRE_FALSE(sink.keying_active());

    sink.set_mode_key(QString());
    REQUIRE(sink.mode_key().isEmpty());
}

TEST_CASE("an unknown Blackmagic device resolves to no mode", "[render][bmd]")
{
    seventhslide::test::ensure_qt_application();

    BmdSink sink;
    const auto mode = sink.active_mode();
    REQUIRE(mode.width == 0);
    REQUIRE(mode.height == 0);
    REQUIRE(mode.key.isEmpty());
}

// OffscreenSink::start() resolves the size through negotiate_size() *before*
// handing it to on_start(), and keeps it even when on_start fails - so a failed
// start is how the negotiated size can be observed without a card.
TEST_CASE("a Blackmagic sink with no card falls back to an even canvas size", "[render][bmd]")
{
    seventhslide::test::ensure_qt_application();

    BmdSink sink;
    REQUIRE_FALSE(sink.start(QStringLiteral("no-such-device"), QSize(1919, 1079)));
    // No device means no mode raster to lock to, so the base class rule applies
    // and the odd dimensions are floored to even.
    REQUIRE(sink.output_size() == QSize(1918, 1078));
}

TEST_CASE("a live Blackmagic output locks to its mode raster", "[render][bmd]")
{
    seventhslide::test::ensure_qt_application();

    const auto devices = bmd_list_devices();
    if (devices.empty()) {
        // States (a) and (b): nothing to open. The failure paths above are what
        // this machine can prove; first light needs a card on the bench.
        SUCCEED("no Blackmagic output device attached");
        return;
    }

    // Its output's mix comes from a mixer on its own timer.
    seventhslide::audio::AudioEngine engine(seventhslide::audio::AudioEngine::Backend::Null);
    engine.mixer().set_routing({{"main"}, {{"out", {"main"}}}});
    engine.start_timer();

    BmdSink sink;
    sink.set_audio(&engine.mixer(), "out", true);
    if (!sink.start(devices.front().id, QSize(1024, 768))) {
        // A card that is present but already owned by another application.
        REQUIRE_FALSE(sink.last_error().isEmpty());
        return;
    }
    const auto mode = sink.active_mode();
    REQUIRE(mode.width > 0);
    REQUIRE(mode.height > 0);
    // The canvas asked for 1024x768 and got the broadcast raster instead.
    REQUIRE(sink.output_size() == QSize(mode.width, mode.height));

    // A canvas resize must never drop a live feed: the mode is fixed, so the
    // sink keeps the raster it opened with.
    seventhslide::scene::SceneFrame frame;
    frame.canvas_width = 1280;
    frame.canvas_height = 720;
    sink.present(seventhslide::scene::share_frame(frame));
    REQUIRE(sink.is_live());
    REQUIRE(sink.output_size() == QSize(mode.width, mode.height));
    // Every DeckLink output takes embedded audio.
    CHECK(sink.sending_audio());
    sink.set_audio(&engine.mixer(), "out", false);
    CHECK_FALSE(sink.sending_audio());
    CHECK(sink.is_live());
    sink.set_audio(&engine.mixer(), "out", true);
    CHECK(sink.sending_audio());

    sink.stop();
    REQUIRE_FALSE(sink.is_live());
    CHECK_FALSE(sink.sending_audio());
    engine.stop();
}
