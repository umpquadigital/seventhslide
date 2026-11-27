#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pixel_compare.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/media/media_player.hpp>
#include <seventhslide/render/mpv_media_player.hpp>
#include <seventhslide/render/video_painter.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QTest>
#include <QThread>

#include <chrono>
#include <cmath>
#include <fstream>
#include <string>

#ifdef __linux__
#include <sys/resource.h>
#endif

using seventhslide::media::format_video_countdown;
using seventhslide::render::MpvMediaPlayer;
using seventhslide::render::paint_video_media;
using seventhslide::render::video_area_rect;
using seventhslide::scene::SceneFrame;
using seventhslide::scene::SceneMode;
using seventhslide::scene::SceneVideoMedia;

using seventhslide::test::nearly_rgb;

TEST_CASE("format_video_countdown counts down with a minus sign", "[media]")
{
    REQUIRE(format_video_countdown(10.0, 70.0) == "-01:00");
    REQUIRE(format_video_countdown(65.0, 70.0) == "-00:05");
    REQUIRE(format_video_countdown(0.0, 0.0).empty());
    REQUIRE(format_video_countdown(5.0, -1.0).empty());
}

TEST_CASE("MpvMediaPlayer constructs and idles", "[media][mpv]")
{
    seventhslide::test::ensure_qt_application();
    MpvMediaPlayer p;
    REQUIRE_FALSE(p.has_media());
    REQUIRE_FALSE(p.is_playing());
    REQUIRE_FALSE(p.load("", false));
}

// mpv leaves the fourth byte of each pixel undefined; the frame handed to
// painters must still be opaque, in the format the painters expect.
TEST_CASE("MpvMediaPlayer frames are opaque premultiplied ARGB", "[media][mpv]")
{
    seventhslide::test::ensure_qt_application();
    MpvMediaPlayer p;
    REQUIRE(p.load(SEVENTHSLIDE_FIXTURES_DIR "/video/solid_3366cc_64x36.mp4", true));
    p.play();

    // Before the first frame decodes, mpv clears the target and that black
    // plate is what comes back; wait for the clip itself.
    QImage frame;
    REQUIRE(QTest::qWaitFor([&] {
        frame = p.current_frame();
        return !frame.isNull() && nearly_rgb(frame.pixel(0, 0), 0x33, 0x66, 0xcc);
    }, 10000));
    REQUIRE(frame.size() == QSize(64, 36));
    REQUIRE(frame.format() == QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            const QRgb px = frame.pixel(x, y);
            INFO("pixel " << x << "," << y << " = " << std::hex << px);
            REQUIRE(qAlpha(px) == 255);
            REQUIRE(nearly_rgb(px, 0x33, 0x66, 0xcc));
        }
    }
}

namespace {

/// How long ``fn`` took, in milliseconds.
template <typename Fn>
double ms_taken(Fn&& fn)
{
    const auto start = std::chrono::steady_clock::now();
    fn();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

/// How many times this thread has stopped to wait for something (a lock, a
/// reply), not counting the times it was only set aside for another thread.
/// Where that cannot be told, every call counts as one that may have waited.
long waits_so_far()
{
#ifdef __linux__
    rusage usage{};
    getrusage(RUSAGE_THREAD, &usage);
    return usage.ru_nvcsw;
#else
    static long calls = 0;
    return ++calls;
#endif
}

}  // namespace

// The window's thread asks the player on every tick and paints every output:
// a call that waits for mpv freezes them all. mpv's core is busy
// as a file opens, after a seek, and as a looping clip goes round.
TEST_CASE("No player call waits on mpv while a video starts, seeks or loops", "[media][mpv]")
{
    seventhslide::test::ensure_qt_application();
    MpvMediaPlayer p;
    double slowest = 0.0;
    std::string slowest_call;
    // A call counts only if it stopped to wait: on a busy computer one that
    // was set aside for other work can take as long without waiting at all.
    const auto timed = [&](const char* call, auto&& fn) {
        const long waits = waits_so_far();
        const double ms = ms_taken(fn);
        if (waits_so_far() != waits && ms > slowest) {
            slowest = ms;
            slowest_call = call;
        }
    };
    // 3 s long, looping: it goes round once in the 4 s below.
    timed("load", [&] {
        REQUIRE(p.load(SEVENTHSLIDE_FIXTURES_DIR "/video/tone_3366cc_64x36_3s_sound_at_500ms.mp4", true));
    });
    timed("play", [&] { p.play(); });
    bool sought = false;
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 4000) {
        // As the playback tick asks.
        timed("current_frame", [&] { (void)p.current_frame(); });
        timed("position_sec", [&] { (void)p.position_sec(); });
        timed("is_playing", [&] { (void)p.is_playing(); });
        timed("clock_running", [&] { (void)p.clock_running(); });
        timed("duration_sec", [&] { (void)p.duration_sec(); });
        timed("frame_rate", [&] { (void)p.frame_rate(); });
        // As the app tells it: lip sync sets the speed every tick, and the
        // session sends play again after each live change.
        timed("set_speed", [&] { p.set_speed(1.0); });
        timed("play again", [&] { p.play(); });
        if (!sought && clock.elapsed() > 1500) {
            sought = true;
            timed("seek", [&] { p.seek(0.5); });
        }
        QThread::msleep(10);
    }
    timed("pause", [&] { p.pause(); });
    timed("unload", [&] { p.unload(); });
    INFO("slowest: " << slowest_call << ", " << slowest << " ms");
    CHECK(slowest < 50.0);
}

TEST_CASE("A player loaded at a place starts there, and loops round to the file's start",
          "[media][mpv]")
{
    seventhslide::test::ensure_qt_application();
    MpvMediaPlayer p;
    REQUIRE(p.load(SEVENTHSLIDE_FIXTURES_DIR "/video/solid_3366cc_64x36_20s.mp4", false, 8.0));
    // It stands there at once, and plays on from there.
    CHECK(p.position_sec() == Catch::Approx(8.0).margin(0.05));
    p.play();
    REQUIRE(QTest::qWaitFor([&] {
        (void)p.current_frame();
        return p.clock_running() && p.position_sec() > 8.2;
    }, 10000));
    CHECK(p.position_sec() < 10.0);

    // Started part way, a looping file still comes round to its start.
    REQUIRE(p.load(SEVENTHSLIDE_FIXTURES_DIR "/video/tone_3366cc_64x36_3s_sound_at_500ms.mp4",
                   true, 2.0));
    p.play();
    REQUIRE(QTest::qWaitFor([&] {
        (void)p.current_frame();
        return p.clock_running() && p.position_sec() > 2.2;
    }, 10000));
    REQUIRE(QTest::qWaitFor([&] {
        (void)p.current_frame();
        return p.position_sec() < 1.0;
    }, 10000));
}

TEST_CASE("A player answers at once what it was just told", "[media][mpv]")
{
    seventhslide::test::ensure_qt_application();
    MpvMediaPlayer p;
    REQUIRE(p.load(SEVENTHSLIDE_FIXTURES_DIR "/video/solid_3366cc_64x36_20s.mp4", false));
    CHECK_FALSE(p.is_playing());
    CHECK(p.position_sec() == 0.0);
    p.play();
    CHECK(p.is_playing());
    // Running means the picture moves: mpv stops being idle a moment before
    // its first frame moves, and the sound waits for the picture itself.
    bool ran_still = false;
    REQUIRE(QTest::qWaitFor([&] {
        (void)p.current_frame();
        const bool running = p.clock_running();
        ran_still = ran_still || (running && p.position_sec() == 0.0);
        return running && p.position_sec() > 0.3;
    }, 10000));
    CHECK_FALSE(ran_still);
    CHECK(p.duration_sec() == Catch::Approx(20.0).margin(0.1));
    CHECK(p.frame_rate() > 0.0);

    // A seek is where it was sent at once, and the clock waits for mpv to
    // start again from there.
    p.seek(5.0);
    CHECK(p.position_sec() == Catch::Approx(5.0).margin(0.05));
    CHECK_FALSE(p.clock_running());
    REQUIRE(QTest::qWaitFor([&] {
        (void)p.current_frame();
        return p.clock_running();
    }, 3000));
    CHECK(p.position_sec() >= 5.0);
    CHECK(p.position_sec() < 6.0);

    p.pause();
    CHECK_FALSE(p.is_playing());
    CHECK_FALSE(p.clock_running());
    const double paused_at = p.position_sec();
    QTest::qWait(300);
    CHECK(p.position_sec() == Catch::Approx(paused_at).margin(0.05));
}

TEST_CASE("A file mpv cannot open is not media", "[media][mpv]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    const auto path = dir.path / "broken.mp4";
    {
        std::ofstream out(path, std::ios::binary);
        for (int i = 0; i < 4096; ++i) {
            out.put(static_cast<char>((i * 7919) % 251));
        }
    }
    MpvMediaPlayer p;
    // The file is there, so loading starts; mpv says later that it failed.
    REQUIRE(p.load(path.string(), false));
    p.play();
    REQUIRE(QTest::qWaitFor([&] { return !p.is_playing() && !p.has_media(); }, 10000));
    CHECK(p.current_frame().isNull());
    CHECK(p.duration_sec() == 0.0);
}

TEST_CASE("video_area_rect zero size fills canvas", "[render]")
{
    SceneFrame frame;
    frame.canvas_width = 1280;
    frame.canvas_height = 720;
    SceneVideoMedia media;
    media.url = "/static/videos/a.mp4";
    media.area = {.enabled = true, .x = 5, .y = 10, .width = 0, .height = 0};
    frame.video = media;
    const QRectF r = video_area_rect(frame);
    REQUIRE(r.width() == Catch::Approx(1280.0));
    REQUIRE(r.height() == Catch::Approx(720.0));
}

TEST_CASE("paint_video_media black plate", "[render]")
{
    seventhslide::test::ensure_qt_application();

    SceneFrame frame;
    frame.mode = SceneMode::Video;
    frame.canvas_width = 200;
    frame.canvas_height = 100;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#112233";
    SceneVideoMedia media;
    media.url = "/static/videos/a.mp4";
    media.area = {.enabled = true, .x = 0, .y = 0, .width = 100, .height = 100};
    frame.video = media;

    QImage out(200, 100, QImage::Format_ARGB32_Premultiplied);
    out.fill(QColor(0x11, 0x22, 0x33));
    QPainter p(&out);
    paint_video_media(p, frame);
    p.end();

    REQUIRE(nearly_rgb(out.pixel(50, 50), 0, 0, 0));
    REQUIRE(nearly_rgb(out.pixel(150, 50), 0x11, 0x22, 0x33));
}

