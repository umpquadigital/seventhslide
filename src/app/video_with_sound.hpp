#pragma once

// The content video as the stacks drive it: mpv's picture and the Audio
// Mixer's sound of the same file, one transport. The sound's clock leads and
// the picture follows:
//
// - mpv takes a moment to start its clock after a play or a seek, so the
//   sound waits for it (at most a second) and then starts from the picture's
//   place. Nothing is heard twice or skipped: the sound was silent until then.
// - Every tick, keep_in_step() plays the picture a little faster or slower to
//   close any gap, and jumps it to the sound when the two are far apart. mpv
//   plays no sound, so a change in its speed is never heard. A jump aims
//   ahead of the sound by as long as mpv has been taking to show a new place
//   while playing (learned from each jump and each seek made while playing;
//   a start from a stop takes longer and says nothing about it), so the
//   picture lands where the sound will be. Until the picture moves again
//   after a jump there is nothing to steer by, and it is left alone.
// - mpv's clock moves a frame at a time (it is the time of the frame on
//   show), so the gap is smoothed over a few ticks and measured against half
//   a frame, where a picture exactly in step sits on average.
//
// A file with no sound track plays its picture on mpv's own clock.

#include <seventhslide/media/media_player.hpp>

#include <chrono>

namespace seventhslide::audio {
class AudioFilePlayer;
}

namespace seventhslide::render {
class MpvMediaPlayer;
}

namespace seventhslide::app {

/// How far the picture is ahead of its sound (behind when negative), the
/// short way round when it loops: at a loop's restart one may have started
/// over a moment before the other.
[[nodiscard]] double picture_drift(double picture_sec, double sound_sec, double duration_sec,
                                   bool loop) noexcept;

/// The speed that closes ``drift_sec`` (measured from where the picture
/// should be): 1 within 10 ms; a picture ahead plays slower, one behind
/// faster, by twice the drift per second, at most 10% (a picture without
/// its sound shows no difference). A 100 ms gap is within lip sync (45 ms)
/// in under a second.
[[nodiscard]] double picture_speed_for_drift(double drift_sec) noexcept;

class VideoWithSound final : public media::IMediaPlayer {
public:
    using Clock = std::chrono::steady_clock;

    /// Within this, picture and sound are in step.
    static constexpr double k_in_step_sec = 0.01;
    /// Further apart than this (or two frames, for a video of few frames),
    /// for three ticks running, the picture jumps to the sound: out of lip
    /// sync, as after mpv starts a loop over, which takes it a moment...
    static constexpr double k_seek_drift_sec = 0.1;
    static constexpr int k_seek_after_ticks = 3;
    /// ...and at once when further than this.
    static constexpr double k_jump_drift_sec = 0.5;
    /// Not again sooner than this: a seek takes mpv a moment, and a big file
    /// longer.
    static constexpr auto k_seek_rest = std::chrono::milliseconds(1500);
    static constexpr double k_max_speed_change = 0.10;
    static constexpr double k_speed_gain_per_sec = 2.0;
    /// The longest the sound waits for the picture to start.
    static constexpr auto k_start_wait = std::chrono::milliseconds(1000);
    /// How long the measured gap is smoothed over.
    static constexpr double k_drift_smoothing_sec = 0.3;
    /// What a seek while playing takes mpv to show until one has been seen
    /// (60-150 ms measured on 2026-10-31, idle and under load).
    static constexpr double k_first_seek_delay_sec = 0.12;

    /// Both must outlive this.
    VideoWithSound(render::MpvMediaPlayer& picture, audio::AudioFilePlayer& sound);

    void play() override;
    void pause() override;
    void seek(double position_sec) override;
    void restart() override;
    void unload() override;

    /// The transport reads the picture: it is what has the length and what a
    /// file without sound still has.
    [[nodiscard]] bool has_media() const override;
    [[nodiscard]] bool is_playing() const override;
    [[nodiscard]] bool ended() const override;
    [[nodiscard]] double position_sec() const override;
    [[nodiscard]] double duration_sec() const override;
    [[nodiscard]] bool loop() const override;

    /// Start the sound once the picture moves, and bring the picture into
    /// step with it. Call every tick while the video plays.
    void keep_in_step(Clock::time_point now = Clock::now());

    /// The sound is waiting for mpv's clock to start (tests).
    [[nodiscard]] bool sound_waiting() const noexcept { return waiting_; }

private:
    /// The picture and the sound, both from ``start_sec``.
    bool do_load(std::string_view path, bool loop, double start_sec) override;
    void set_speed(double speed);
    /// Hold the sound until mpv's clock runs.
    void wait_for_picture();
    /// mpv took ``took_sec`` to show a new place while playing.
    void learn_seek_delay(double took_sec);

    render::MpvMediaPlayer& picture_;
    audio::AudioFilePlayer& sound_;
    double speed_ = 1.0;
    /// The transport says play (whatever mpv has got to).
    bool playing_ = false;
    bool waiting_ = false;
    /// The wait follows a seek made while playing: how long it takes is
    /// what a jump takes.
    bool wait_is_seek_ = false;
    double wait_mark_sec_ = 0.0;
    Clock::time_point wait_since_{};
    Clock::time_point last_seek_{};
    /// A jump was sent and the picture has not moved since.
    bool jumping_ = false;
    /// Ticks in a row the picture has been too far out.
    int far_ticks_ = 0;
    double seek_delay_sec_ = k_first_seek_delay_sec;
    bool smoothed_valid_ = false;
    double smoothed_drift_ = 0.0;
    Clock::time_point last_tick_{};
};

} // namespace seventhslide::app
