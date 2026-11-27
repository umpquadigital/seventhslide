// An SRT stream: the rules its few settings follow, and the streamer end to
// end - waiting for a receiver, or calling one, with the receiver reading
// what arrives the way OBS or a decoder does.

#include <catch2/catch_test_macros.hpp>

#include "srt_test_receiver.hpp"
#include "test_wait.hpp"

#include <seventhslide/stream/encoder_catalog.hpp>
#include <seventhslide/stream/srt_streamer.hpp>
#include <seventhslide/stream/stream_settings.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace seventhslide::stream;
using seventhslide::test::SrtTestReceiver;
using namespace std::chrono_literals;

namespace {

/// Stream settings for a test, on a free port: H.264 where this computer
/// encodes it, else whatever it does.
[[nodiscard]] std::optional<StreamSettings> streamable(SrtMode mode)
{
    if (!srt_supported()) {
        return std::nullopt;
    }
    StreamSettings s;
    s.mode = mode;
    s.port = seventhslide::test::free_udp_port();
    REQUIRE(s.port > 0);
    s.host = mode == SrtMode::Caller ? "127.0.0.1" : "";
    if (!usable_encoders(VideoCodec::H264).empty()) {
        s.codec = VideoCodec::H264;
    } else if (!usable_encoders(VideoCodec::H265).empty()) {
        s.codec = VideoCodec::H265;
    } else {
        return std::nullopt;
    }
    return s;
}

using seventhslide::test::wait_until;

/// A tone, so the stream's sound is not silence.
void push_tone(SrtStreamer& streamer, std::size_t frames)
{
    std::vector<float> block(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto v = static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * 440.0
                                                          * static_cast<double>(i) / 48000.0));
        block[2 * i] = v;
        block[2 * i + 1] = v;
    }
    streamer.push_audio(block.data(), frames);
}

}  // namespace

TEST_CASE("Stream settings check what the operator gave", "[stream]")
{
    StreamSettings s;
    CHECK(validate(s).empty());  // waiting on port 9000, as a new one is

    s.port = 0;
    CHECK_FALSE(validate(s).empty());
    s.port = 65536;
    CHECK_FALSE(validate(s).empty());
    s.port = 9000;

    // A caller needs somewhere to call before it sends, but may be saved
    // without (the editor asks for the address next); an address is a host,
    // not a URL.
    s.mode = SrtMode::Caller;
    CHECK(validate(s).empty());
    CHECK_FALSE(missing_to_send(s).empty());
    s.host = "studio.local";
    CHECK(validate(s).empty());
    CHECK(missing_to_send(s).empty());
    s.host = "srt://studio.local";
    CHECK_FALSE(validate(s).empty());
    s.host = "studio.local:9000";
    CHECK_FALSE(validate(s).empty());
    s.host = "fd00::12";  // IPv6 has its colons
    CHECK(validate(s).empty());
    // A listener waits on every address: what it kept from calling is not
    // used, so not checked.
    s.mode = SrtMode::Listener;
    s.host = "studio.local:9000";
    CHECK(validate(s).empty());
    s.mode = SrtMode::Caller;
    s.host = "192.168.1.20";

    // SRT's passphrase limits.
    s.passphrase = "short";
    CHECK_FALSE(validate(s).empty());
    s.passphrase = "long enough phrase";
    CHECK(validate(s).empty());
    s.passphrase = std::string(80, 'x');
    CHECK_FALSE(validate(s).empty());
    s.passphrase.clear();

    s.bitrate_kbps = 100;
    CHECK_FALSE(validate(s).empty());
    s.bitrate_kbps = 8000;
    CHECK(validate(s).empty());
}

TEST_CASE("Stream settings' names read back as they were written", "[stream]")
{
    for (const auto m : {SrtMode::Listener, SrtMode::Caller}) {
        CHECK(mode_from_name(mode_name(m)) == m);
    }
    for (const auto p : {StreamPreset::LowDelay, StreamPreset::Reliable}) {
        CHECK(preset_from_name(preset_name(p)) == p);
    }
    for (const auto c : {VideoCodec::H265, VideoCodec::H264}) {
        CHECK(codec_from_name(codec_name(c)) == c);
    }
    CHECK_FALSE(mode_from_name("rendezvous"));
    CHECK_FALSE(codec_from_name("av1"));
}

TEST_CASE("A stream's preset trades delay for reliability and bandwidth", "[stream]")
{
    CHECK(latency_ms(StreamPreset::LowDelay) < latency_ms(StreamPreset::Reliable));
    CHECK(keyframe_seconds(StreamPreset::LowDelay) <= keyframe_seconds(StreamPreset::Reliable));

    // Automatic: less for reliable, less for H.265, in proportion to area.
    const int low_265 = auto_bitrate_kbps(VideoCodec::H265, StreamPreset::LowDelay, 1920, 1080);
    const int low_264 = auto_bitrate_kbps(VideoCodec::H264, StreamPreset::LowDelay, 1920, 1080);
    const int rel_265 = auto_bitrate_kbps(VideoCodec::H265, StreamPreset::Reliable, 1920, 1080);
    CHECK(low_265 < low_264);
    CHECK(rel_265 < low_265);
    CHECK(auto_bitrate_kbps(VideoCodec::H265, StreamPreset::LowDelay, 1280, 720)
          == static_cast<int>(std::int64_t{low_265} * 1280 * 720 / (1920 * 1080)));
    // Kept within what a link can take.
    CHECK(auto_bitrate_kbps(VideoCodec::H264, StreamPreset::Reliable, 160, 90) >= 500);
    CHECK(auto_bitrate_kbps(VideoCodec::H264, StreamPreset::LowDelay, 7680, 4320) <= 50'000);

    StreamSettings s;
    CHECK(bitrate_kbps(s, 1920, 1080) == low_265);
    s.bitrate_kbps = 7000;
    CHECK(bitrate_kbps(s, 1920, 1080) == 7000);
}

TEST_CASE("A stream's address is written as a receiver is told it", "[stream]")
{
    StreamSettings s;
    s.port = 9001;
    CHECK(address_text(s) == "srt://:9001");
    // Waiting on every address, whatever address it kept from calling.
    s.host = "studio.local";
    CHECK(address_text(s) == "srt://:9001");
    s.mode = SrtMode::Caller;
    CHECK(address_text(s) == "srt://studio.local:9001");
    s.host = "fd00::12";
    CHECK(address_text(s) == "srt://[fd00::12]:9001");
}

TEST_CASE("Encoders are offered hardware first, and each that opens is remembered",
          "[stream]")
{
    for (const auto codec : {VideoCodec::H265, VideoCodec::H264}) {
        const auto candidates = encoder_candidates(codec);
        REQUIRE_FALSE(candidates.empty());
        // The software one last.
        CHECK(candidates.back().rfind("lib", 0) == 0);
        const auto usable = usable_encoders(codec);
        CHECK(usable == usable_encoders(codec));
        for (const auto& e : usable) {
            CHECK(e.codec == codec);
            CHECK(e.hardware == (e.name.rfind("lib", 0) != 0));
        }
    }
}

TEST_CASE("A stream waits for a receiver and sends it pictures and sound",
          "[stream][live]")
{
    auto settings = streamable(SrtMode::Listener);
    if (!settings) {
        SKIP("this FFmpeg cannot send SRT, or no H.264/H.265 encoder opens here");
    }
    // An address kept from sending to a receiver (192.0.2.1 is no one's)
    // is not where it waits: it waits on every address.
    settings->host = "192.0.2.1";
    SrtStreamer streamer;
    std::atomic<int> told{0};
    streamer.set_status_listener([&] { ++told; });
    std::string error;
    REQUIRE(streamer.start(*settings, 320, 180, error));
    REQUIRE(wait_until([&] { return streamer.status().state == StreamState::Waiting; }, 5s));
    CHECK(told > 0);

    // A picture to send, and its sound.
    std::vector<std::uint8_t> picture(320 * 180 * 4, 0x80);
    streamer.submit_picture(picture.data(), 320, 180, 320 * 4);
    push_tone(streamer, 4800);

    SrtTestReceiver receiver;
    REQUIRE(receiver.open("srt://127.0.0.1:" + std::to_string(settings->port),
                          {{"mode", "caller"}}, 15s));
    CHECK(streamer.status().state == StreamState::Sending);
    CHECK_FALSE(streamer.status().encoder.name.empty());
    CHECK(receiver.info().video_codec == (settings->codec == VideoCodec::H264 ? "h264" : "hevc"));
    CHECK(receiver.info().width == 320);
    CHECK(receiver.info().height == 180);
    CHECK(receiver.info().audio_codec == "aac");
    CHECK(receiver.info().sample_rate == 48000);
    CHECK(receiver.info().channels == 2);

    // About a second: 30 pictures, and the sound beside them.
    const auto counts = receiver.read(1500ms);
    CHECK(counts.video >= 20);
    CHECK(counts.audio >= 20);
    CHECK(streamer.pictures_sent() >= 20);

    streamer.stop();
    CHECK(streamer.status().state == StreamState::Stopped);
}

TEST_CASE("A stream's pictures arrive at the size they are sent at", "[stream][live]")
{
    // Sizes an encoder pads out to its block size, 360 lines to 368 and 854
    // columns to 896: the stream must say how much to crop. AMD's VAAPI
    // H.265 says nothing at 360 (1080p shows a green band) and too little
    // at 854.
    if (!srt_supported()) {
        SKIP("this FFmpeg cannot send SRT");
    }
    int streamed = 0;
    for (const auto codec : {VideoCodec::H265, VideoCodec::H264}) {
        if (usable_encoders(codec).empty()) {
            continue;
        }
        for (const auto& [w, h] : {std::pair{854, 480}, std::pair{640, 360}}) {
            StreamSettings s;
            s.codec = codec;
            s.port = seventhslide::test::free_udp_port();
            SrtStreamer streamer;
            std::string error;
            REQUIRE(streamer.start(s, w, h, error));
            SrtTestReceiver receiver;
            REQUIRE(receiver.open("srt://127.0.0.1:" + std::to_string(s.port),
                                  {{"mode", "caller"}}, 15s));
            INFO(streamer.status().encoder.name << " at " << w << "x" << h);
            CHECK(receiver.info().width == w);
            CHECK(receiver.info().height == h);
            ++streamed;
        }
    }
    if (streamed == 0) {
        SKIP("no H.264/H.265 encoder opens here");
    }
}

TEST_CASE("A stream calls its receiver, and tries again until it answers",
          "[stream][live]")
{
    const auto settings = streamable(SrtMode::Caller);
    if (!settings) {
        SKIP("this FFmpeg cannot send SRT, or no H.264/H.265 encoder opens here");
    }
    SrtStreamer streamer;
    std::string error;
    REQUIRE(streamer.start(*settings, 320, 180, error));
    // Nobody there yet: it says so, and keeps trying.
    REQUIRE(wait_until([&] { return streamer.status().state == StreamState::Retrying; }, 10s));
    CHECK(streamer.status().error.find("srt://127.0.0.1") != std::string::npos);

    // The receiver comes up: the next try reaches it.
    SrtTestReceiver receiver;
    REQUIRE(receiver.open("srt://0.0.0.0:" + std::to_string(settings->port),
                          {{"mode", "listener"}}, 20s));
    CHECK(wait_until([&] { return streamer.status().state == StreamState::Sending; }, 5s));
    CHECK(receiver.read(1000ms).video > 10);
    streamer.stop();
}

TEST_CASE("An encrypted stream reaches only a receiver with its passphrase",
          "[stream][live]")
{
    auto settings = streamable(SrtMode::Listener);
    if (!settings) {
        SKIP("this FFmpeg cannot send SRT, or no H.264/H.265 encoder opens here");
    }
    settings->passphrase = "a passphrase for testing";
    SrtStreamer streamer;
    std::string error;
    REQUIRE(streamer.start(*settings, 320, 180, error));
    const std::string url = "srt://127.0.0.1:" + std::to_string(settings->port);
    {
        SrtTestReceiver wrong;
        CHECK_FALSE(wrong.open(url, {{"mode", "caller"}, {"passphrase", "not the passphrase"}}, 5s));
    }
    SrtTestReceiver right;
    REQUIRE(right.open(url, {{"mode", "caller"}, {"passphrase", settings->passphrase}}, 15s));
    CHECK(right.read(1000ms).video > 10);
}

TEST_CASE("A stream waiting for a receiver stops at once", "[stream][live]")
{
    const auto settings = streamable(SrtMode::Listener);
    if (!settings) {
        SKIP("this FFmpeg cannot send SRT, or no H.264/H.265 encoder opens here");
    }
    SrtStreamer streamer;
    std::string error;
    REQUIRE(streamer.start(*settings, 320, 180, error));
    REQUIRE(wait_until([&] { return streamer.status().state == StreamState::Waiting; }, 5s));
    const auto before = std::chrono::steady_clock::now();
    streamer.stop();
    CHECK(std::chrono::steady_clock::now() - before < 2s);
    CHECK_FALSE(streamer.running());
}

TEST_CASE("A stream refuses settings it cannot send", "[stream]")
{
    SrtStreamer streamer;
    std::string error;
    StreamSettings s;
    s.mode = SrtMode::Caller;  // and no host
    CHECK_FALSE(streamer.start(s, 320, 180, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(streamer.running());
    s = StreamSettings{};
    CHECK_FALSE(streamer.start(s, 0, 0, error));
}

TEST_CASE("A stream tries encoders not tried yet on its own thread", "[stream]")
{
    if (!srt_supported()) {
        SKIP("this FFmpeg cannot send SRT");
    }
    // Every candidate refused, as on a computer where none opens: the first
    // stream to ask tries them all.
    const auto caller = std::this_thread::get_id();
    std::atomic<int> tried{0};
    std::atomic<bool> on_caller{false};
    set_encoder_trial_for_tests([&](const std::string&) {
        ++tried;
        if (std::this_thread::get_id() == caller) {
            on_caller = true;
        }
        return false;
    });
    struct PutBack {
        ~PutBack() { set_encoder_trial_for_tests({}); }
    } put_back;

    StreamSettings s;
    s.port = seventhslide::test::free_udp_port();
    s.codec = VideoCodec::H264;
    SrtStreamer streamer;
    std::string error;
    REQUIRE(streamer.start(s, 320, 180, error));
    REQUIRE(wait_until([&] { return streamer.status().state == StreamState::Failed; }, 5s));
    CHECK(tried == static_cast<int>(encoder_candidates(VideoCodec::H264).size()));
    CHECK_FALSE(on_caller);
    CHECK(streamer.status().error == "No H.264 encoder works on this computer.");
    // Known now: refused at once, as before any stream.
    CHECK_FALSE(streamer.start(s, 320, 180, error));
    CHECK(error == "No H.264 encoder works on this computer.");
    streamer.stop();
}

TEST_CASE("A stream stopped while its encoders are being tried stops at once", "[stream]")
{
    if (!srt_supported()) {
        SKIP("this FFmpeg cannot send SRT");
    }
    // Each candidate held until let go, or for 10 s at most.
    std::mutex mu;
    std::condition_variable let_go;
    bool released = false;
    std::atomic<bool> trying{false};
    set_encoder_trial_for_tests([&](const std::string&) {
        trying = true;
        std::unique_lock lock(mu);
        let_go.wait_for(lock, 10s, [&] { return released; });
        return false;
    });
    struct PutBack {
        std::mutex& mu;
        std::condition_variable& let_go;
        bool& released;
        ~PutBack()
        {
            {
                std::lock_guard lock(mu);
                released = true;
            }
            let_go.notify_all();
            // Waits for the probe still running.
            set_encoder_trial_for_tests({});
        }
    } put_back{mu, let_go, released};

    StreamSettings s;
    s.port = seventhslide::test::free_udp_port();
    s.codec = VideoCodec::H264;
    SrtStreamer streamer;
    std::string error;
    REQUIRE(streamer.start(s, 320, 180, error));
    REQUIRE(wait_until([&] { return trying.load(); }, 5s));
    const auto before = std::chrono::steady_clock::now();
    streamer.stop();
    CHECK(std::chrono::steady_clock::now() - before < 2s);
    CHECK_FALSE(streamer.running());
    CHECK(streamer.status().state == StreamState::Stopped);
    // The probe goes on, and nothing is known until it ends.
    CHECK_FALSE(known_encoders(VideoCodec::H264));
}
