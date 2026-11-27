#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"

#include <seventhslide/audio/audio_engine.hpp>
#include <seventhslide/audio/pipewire_source.hpp>
#include <seventhslide/render/pixel_pack.hpp>
#include <seventhslide/render/virtual_camera.hpp>

#include <QImage>
#include <QRgb>
#include <QSize>

using seventhslide::render::clamp_camera_size;
using seventhslide::render::encode_yuyv;
using seventhslide::render::is_virtual_camera_device;
using seventhslide::render::list_virtual_camera_devices;
using seventhslide::render::virtual_camera_implemented;

TEST_CASE("clamp_camera_size forces even 1080p-capped dimensions", "[render][vcam]")
{
    REQUIRE(clamp_camera_size(QSize(1920, 1080)) == QSize(1920, 1080));
    REQUIRE(clamp_camera_size(QSize(1919, 1079)) == QSize(1918, 1078));
    REQUIRE(clamp_camera_size(QSize(3840, 2160)) == QSize(1920, 1080));
    REQUIRE(clamp_camera_size(QSize(0, 0)) == QSize(1920, 1080));
    REQUIRE(clamp_camera_size(QSize(1, 1)) == QSize(2, 2));
}

TEST_CASE("encode_yuyv writes BT.601 limited YUYV", "[render][vcam]")
{
    REQUIRE(encode_yuyv(QImage()).isEmpty());

    QImage black(2, 1, QImage::Format_RGB32);
    black.fill(Qt::black);
    const QByteArray yuyv = encode_yuyv(black);
    REQUIRE(yuyv.size() == 4);
    REQUIRE(static_cast<unsigned char>(yuyv[0]) == 16);   // Y0
    REQUIRE(static_cast<unsigned char>(yuyv[1]) == 128);  // U
    REQUIRE(static_cast<unsigned char>(yuyv[2]) == 16);   // Y1
    REQUIRE(static_cast<unsigned char>(yuyv[3]) == 128);  // V

    QImage red(2, 1, QImage::Format_RGB32);
    red.fill(qRgb(255, 0, 0));
    const QByteArray red_yuyv = encode_yuyv(red);
    REQUIRE(red_yuyv.size() == 4);
    REQUIRE(static_cast<unsigned char>(red_yuyv[0]) == 82);  // Y
    REQUIRE(static_cast<unsigned char>(red_yuyv[2]) == 82);
    REQUIRE(static_cast<unsigned char>(red_yuyv[3]) > 200);  // V high for red
}

TEST_CASE("virtual camera listing ignores capture-only webcams", "[render][vcam]")
{
    seventhslide::test::ensure_qt_application();

    const auto devices = list_virtual_camera_devices();
    for (const auto& d : devices) {
        REQUIRE(is_virtual_camera_device(d.path));
        REQUIRE(d.path.startsWith(QLatin1String("/dev/video")));
        REQUIRE_FALSE(d.label.isEmpty());
    }

    // Real integrated cameras must never be treated as loopback sinks.
    if (!is_virtual_camera_device(QStringLiteral("/dev/video0"))) {
        for (const auto& d : devices) {
            REQUIRE(d.path != QStringLiteral("/dev/video0"));
        }
    }

    REQUIRE(virtual_camera_implemented() ==
#if defined(__linux__)
            true
#else
            false
#endif
    );
}

TEST_CASE("A virtual camera with no device offers no microphone", "[render][vcam][audio]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::audio::Mixer mixer;
    mixer.set_routing({{"main"}, {{"out", {"main"}}}});
    seventhslide::render::VirtualCameraSink sink;
    sink.set_microphone_name(QStringLiteral("SeventhSlide CamTest"));
    sink.set_audio(&mixer, "out", true);
    // Not live: no microphone, until the camera itself starts.
    CHECK_FALSE(sink.sending_audio());
    REQUIRE_FALSE(sink.start(QStringLiteral("/dev/no-such-camera"), QSize(640, 360)));
    CHECK_FALSE(sink.sending_audio());
    CHECK(sink.microphone_node().empty());
}

TEST_CASE("A virtual camera offers its output's sound as a microphone beside it",
          "[render][vcam][audio][pipewire]")
{
    seventhslide::test::ensure_qt_application();
    const auto devices = list_virtual_camera_devices();
    if (devices.empty() || !seventhslide::audio::pipewire_available()) {
        SKIP("needs a v4l2loopback device and PipeWire");
    }
    seventhslide::audio::AudioEngine engine(seventhslide::audio::AudioEngine::Backend::Null);
    engine.mixer().set_routing({{"main"}, {{"out", {"main"}}}});
    engine.start_timer();

    seventhslide::render::VirtualCameraSink sink;
    sink.set_microphone_name(QStringLiteral("SeventhSlide CamTest"));
    sink.set_audio(&engine.mixer(), "out", true);
    if (!sink.start(devices.front().path, QSize(640, 360))) {
        engine.stop();
        SKIP("the loopback device would not open");
    }
    CHECK(sink.sending_audio());
    CHECK(sink.microphone_node() == "seventhslide_camtest");
    // Switched off and on: the camera stays up.
    sink.set_audio(&engine.mixer(), "out", false);
    CHECK_FALSE(sink.sending_audio());
    CHECK(sink.is_live());
    sink.set_audio(&engine.mixer(), "out", true);
    CHECK(sink.sending_audio());
    sink.stop();
    CHECK_FALSE(sink.sending_audio());
    engine.stop();
}
