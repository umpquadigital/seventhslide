// Live input through FFmpeg (capture devices, step 5c), read for real from
// FFmpeg's own test sources (lavfi): a picture at its size and rate, paced to
// real time, its sound at its level; an input that cannot be read says why
// and is tried again; and capture devices listed without one plugged in.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ffmpeg_live_input.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "test_wait.hpp"

#include <seventhslide/audio/audio_format.hpp>

#include <QElapsedTimer>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using seventhslide::app::FfmpegLiveInput;
using seventhslide::test::wait_until;

TEST_CASE("A live input's picture and sound arrive, paced to real time", "[live][ffmpeg]")
{
    seventhslide::test::ensure_qt_application();
    namespace audio = seventhslide::audio;
    std::atomic<int> told{0};
    // A test picture at 30 frames a second, and a 1 kHz tone at FFmpeg's
    // default level (an eighth of full scale).
    FfmpegLiveInput input({"lavfi",
                           "testsrc2=size=320x180:rate=30 [out0]; sine=frequency=1000:sample_rate=48000 [out1]",
                           {}},
                          [&] { ++told; });
    REQUIRE(wait_until([&] { return !input.frame().isNull(); }, 10000));
    CHECK(input.frame().size() == QSize(320, 180));
    CHECK(input.stats().frame_rate == Catch::Approx(30.0));
    CHECK(input.problem().empty());

    // Paced: about 30 pictures a second, not as fast as they can be made.
    const auto before = input.stats().frames;
    QThread::msleep(1000);
    const auto in_a_second = input.stats().frames - before;
    CHECK(in_a_second >= 20);
    CHECK(in_a_second <= 45);
    CHECK(told > 0);

    // Its sound, at its level (mono, on both sides at full level).
    std::vector<float> block(audio::k_block_frames * audio::k_channels);
    float loudest = 0.0f;
    REQUIRE(wait_until(
        [&] {
            input.sound().read(block.data(), audio::k_block_frames);
            loudest = 0.0f;
            for (const float v : block) {
                loudest = std::max(loudest, std::abs(v));
            }
            return loudest > 0.1f;
        },
        5000));
    CHECK(loudest == Catch::Approx(0.125f).margin(0.01f));
}

TEST_CASE("A live input that cannot be read says why, and stops at once", "[live][ffmpeg]")
{
    seventhslide::test::ensure_qt_application();
    QElapsedTimer t;
    t.start();
    {
        FfmpegLiveInput input({"lavfi", "no_such_filter", {}}, {});
        REQUIRE(wait_until([&] { return !input.problem().empty(); }, 5000));
        CHECK(input.frame().isNull());
        CHECK(input.problem().find("Cannot open it") != std::string::npos);
    }
    // Gone without waiting out its two seconds between tries.
    CHECK(t.elapsed() < FfmpegLiveInput::k_retry_ms + 1500);
}

#if !defined(_WIN32)
TEST_CASE("An input stuck inside FFmpeg is let go, and stopping it does not hang",
          "[live][ffmpeg]")
{
    seventhslide::test::ensure_qt_application();
    // Opening a pipe nobody writes to blocks in open(), where FFmpeg's
    // interrupt never reaches: as a Mac camera that sends no frame blocks
    // avfoundation.
    seventhslide::test::TempDir dir;
    const auto pipe = dir.path / "nobody_writes";
    REQUIRE(::mkfifo(pipe.c_str(), 0600) == 0);
    QElapsedTimer t;
    t.start();
    {
        FfmpegLiveInput input({"", pipe.string(), {}}, {});
        QThread::msleep(300);
    }
    CHECK(t.elapsed() < 300 + FfmpegLiveInput::k_stop_wait_ms + 1000);
    // The reader it let go ends once the pipe has a writer and is closed.
    const int writer = ::open(pipe.c_str(), O_WRONLY | O_NONBLOCK);
    if (writer >= 0) {
        ::close(writer);
    }
}
#endif

TEST_CASE("Capture devices are listed, none here or some", "[live][ffmpeg]")
{
    // A machine with none lists none; one with some lists each once, named.
    const auto devices = seventhslide::app::capture_devices();
    for (const auto& d : devices) {
        CHECK_FALSE(d.name.empty());
        CHECK_FALSE(d.url.empty());
    }
    CHECK(std::string(seventhslide::app::capture_input_format()).size() > 0);
}

TEST_CASE("A live input's sound can go to a sound that outlives it", "[live][ffmpeg]")
{
    seventhslide::test::ensure_qt_application();
    // As a receiver's is, which a stack's strip holds across inputs.
    seventhslide::audio::StreamSource lasting;
    {
        FfmpegLiveInput input({"lavfi", "sine=frequency=1000:sample_rate=48000 [out1]; "
                                        "testsrc2=size=160x90:rate=30 [out0]", {}},
                              {}, &lasting);
        CHECK(&input.sound() == &lasting);
        REQUIRE(wait_until([&] { return lasting.buffer().fill() > 0; }, 5000));
    }
    CHECK(lasting.buffer().fill() > 0);
}


TEST_CASE("Two capture devices of one name are told apart by their ports", "[live][ffmpeg]")
{
    using seventhslide::app::ListedDevice;
    const auto names = [](std::vector<ListedDevice> listed) {
        std::map<std::string, std::string> by_url;
        for (const auto& d : seventhslide::app::tell_apart(std::move(listed))) {
            by_url[d.url] = d.name;
        }
        return by_url;
    };
    // Listed in one order, then the other: each camera keeps its name.
    const ListedDevice left{{"C920", "/dev/video2"}, "usb-0000:00:14.0-1"};
    const ListedDevice right{{"C920", "/dev/video0"}, "usb-0000:00:14.0-2"};
    const ListedDevice other{{"Capture", "/dev/video4"}, "usb-0000:00:14.0-3"};
    const auto first = names({left, right, other});
    CHECK(first.at("/dev/video2") == "C920");
    CHECK(first.at("/dev/video0") == "C920 (2)");
    CHECK(first.at("/dev/video4") == "Capture");
    CHECK(names({other, right, left}) == first);
}
