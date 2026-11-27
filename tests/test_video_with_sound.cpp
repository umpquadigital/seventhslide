#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"
#include "video_with_sound.hpp"

#include <seventhslide/audio/audio_engine.hpp>
#include <seventhslide/audio/audio_file_player.hpp>
#include <seventhslide/render/mpv_media_player.hpp>

#include <QTest>

#include <cmath>

using seventhslide::app::picture_drift;
using seventhslide::app::picture_speed_for_drift;
using seventhslide::app::VideoWithSound;
using Catch::Approx;
namespace audio = seventhslide::audio;

TEST_CASE("The picture's drift from its sound, the short way round a loop", "[app][video][audio]")
{
    REQUIRE(picture_drift(5.10, 5.00, 20.0, false) == Approx(0.10));
    REQUIRE(picture_drift(4.95, 5.00, 20.0, false) == Approx(-0.05));
    // At a loop's restart the picture has started over and the sound not yet
    // (or the other way): a few hundredths apart, not a whole loop.
    REQUIRE(picture_drift(0.02, 19.99, 20.0, true) == Approx(0.03));
    REQUIRE(picture_drift(19.99, 0.02, 20.0, true) == Approx(-0.03));
    // Not looping, the same numbers mean what they say.
    REQUIRE(picture_drift(0.02, 19.99, 20.0, false) == Approx(-19.97));
    // An unknown length cannot wrap.
    REQUIRE(picture_drift(0.02, 19.99, 0.0, true) == Approx(-19.97));
}

TEST_CASE("The picture's speed closes its drift, at most 10% either way", "[app][video][audio]")
{
    // In step: real time.
    REQUIRE(picture_speed_for_drift(0.0) == 1.0);
    REQUIRE(picture_speed_for_drift(0.009) == 1.0);
    REQUIRE(picture_speed_for_drift(-0.009) == 1.0);
    REQUIRE(picture_speed_for_drift(std::nan("")) == 1.0);
    // Ahead: slower. Behind: faster. By twice the drift per second.
    REQUIRE(picture_speed_for_drift(0.02) == Approx(0.96));
    REQUIRE(picture_speed_for_drift(-0.03) == Approx(1.06));
    // Never more than 10%: a far drift is the seek's job.
    REQUIRE(picture_speed_for_drift(0.4) == Approx(0.90));
    REQUIRE(picture_speed_for_drift(-0.4) == Approx(1.10));
}

namespace {

/// A video with sound as the app plays it: the picture on mpv, the sound on a
/// mixer whose clock is miniaudio's null device (real time, no sound).
/// Declared in this order so the engine, whose clock reads the sound, stops
/// first.
struct SyncRig {
    seventhslide::render::MpvMediaPlayer picture;
    audio::AudioFilePlayer sound;
    VideoWithSound video{picture, sound};
    audio::AudioEngine engine{audio::AudioEngine::Backend::Null};

    explicit SyncRig(bool loop)
    {
        seventhslide::test::ensure_qt_application();
        engine.mixer().set_routing({{"main"}, {}});
        engine.mixer().add_source("main", &sound);
        engine.start();
        REQUIRE(video.load(SEVENTHSLIDE_FIXTURES_DIR "/video/tone_3366cc_64x36_8s.mp4", loop));
        REQUIRE(QTest::qWaitFor([&] { return video.duration_sec() > 7.5; }, 10000));
        REQUIRE(sound.has_media());
    }

    ~SyncRig()
    {
        engine.stop();
        engine.mixer().remove_source(&sound);
    }

    [[nodiscard]] double drift() const
    {
        return picture_drift(picture.position_sec(), sound.position_sec(), video.duration_sec(),
                             video.loop());
    }

    /// As the app does: pull a frame and keep in step every 33 ms. Returns
    /// the drift from where a picture in step sits (half a frame back, at
    /// 25 fps), averaged over the last half second.
    double run_for(int ms)
    {
        constexpr double half_frame = 0.5 / 25.0;
        double sum = 0.0;
        int n = 0;
        for (int t = 0; t < ms; t += 33) {
            (void)picture.current_frame();
            video.keep_in_step();
            if (t >= ms - 500 && !video.sound_waiting()) {
                sum += drift() + half_frame;
                ++n;
            }
            QTest::qWait(33);
        }
        return n > 0 ? sum / n : 1.0;
    }
};

/// Lip sync holds: within about 45 ms.
constexpr double k_lip_sync_sec = 0.045;

}  // namespace

TEST_CASE("A video's picture follows its sound's clock", "[app][video][audio][mpv]")
{
    SyncRig rig(false);
    auto& video = rig.video;

    // The sound waits for mpv's clock to start, then the two go together.
    video.play();
    REQUIRE(video.sound_waiting());
    REQUIRE_FALSE(rig.sound.is_playing());
    REQUIRE(std::abs(rig.run_for(1500)) < k_lip_sync_sec);
    REQUIRE(rig.sound.is_playing());
    REQUIRE(std::abs(rig.run_for(1000)) < k_lip_sync_sec);

    // Playing on, play again (as the app sends after any change) is nothing new.
    video.play();
    REQUIRE_FALSE(video.sound_waiting());

    // Knocked well out of step, the picture jumps to the sound.
    rig.sound.seek(rig.sound.position_sec() + 1.2);
    REQUIRE(std::abs(rig.drift()) > 1.0);
    REQUIRE(std::abs(rig.run_for(1500)) < k_lip_sync_sec);

    // A seek on the transport: both go, the sound waiting for mpv.
    video.seek(0.5);
    REQUIRE(video.sound_waiting());
    REQUIRE(std::abs(rig.run_for(1500)) < k_lip_sync_sec);

    // Paused, both stop; played again, they start together.
    video.pause();
    REQUIRE_FALSE(rig.sound.is_playing());
    REQUIRE(QTest::qWaitFor([&] { return !video.is_playing(); }, 5000));
    video.play();
    REQUIRE(std::abs(rig.run_for(1500)) < k_lip_sync_sec);
}

TEST_CASE("A looping video's picture keeps up with its sound across the restart",
          "[app][video][audio][mpv]")
{
    SyncRig rig(true);
    REQUIRE(rig.video.loop());
    rig.video.seek(7.2);
    rig.video.play();
    // mpv starts a loop over a little late (a seek of its own); within about
    // a second and a half of the restart the picture is back in step.
    REQUIRE(std::abs(rig.run_for(3000)) < k_lip_sync_sec);
    REQUIRE(rig.sound.position_sec() < 4.0);  // it went round
}

TEST_CASE("A video without a sound track plays its picture alone", "[app][video][audio][mpv]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::render::MpvMediaPlayer picture;
    audio::AudioFilePlayer sound;
    VideoWithSound video(picture, sound);

    REQUIRE(video.load(SEVENTHSLIDE_FIXTURES_DIR "/video/solid_3366cc_64x36.mp4", false));
    REQUIRE(QTest::qWaitFor([&] { return !sound.has_media(); }, 5000));
    REQUIRE(video.has_media());
    video.play();
    // Nothing to follow: the picture plays on its own clock.
    REQUIRE(QTest::qWaitFor([&] {
        (void)picture.current_frame();
        video.keep_in_step();
        return video.position_sec() > 0.2;
    }, 10000));
    REQUIRE(video.is_playing());
    REQUIRE_FALSE(sound.is_playing());
    video.unload();
    REQUIRE_FALSE(video.has_media());
}
