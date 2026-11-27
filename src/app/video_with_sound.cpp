#include "video_with_sound.hpp"

#include <seventhslide/audio/audio_file_player.hpp>
#include <seventhslide/render/mpv_media_player.hpp>

#include <algorithm>
#include <cmath>

namespace seventhslide::app {
namespace {

/// A speed change smaller than this is not sent: mpv need not hear every tick.
constexpr double k_speed_step = 0.002;

} // namespace

double picture_drift(double picture_sec, double sound_sec, double duration_sec, bool loop) noexcept
{
    const double drift = picture_sec - sound_sec;
    if (loop && duration_sec > 0.0) {
        return std::remainder(drift, duration_sec);
    }
    return drift;
}

double picture_speed_for_drift(double drift_sec) noexcept
{
    if (!std::isfinite(drift_sec) || std::abs(drift_sec) < VideoWithSound::k_in_step_sec) {
        return 1.0;
    }
    return 1.0 - std::clamp(drift_sec * VideoWithSound::k_speed_gain_per_sec,
                            -VideoWithSound::k_max_speed_change,
                            VideoWithSound::k_max_speed_change);
}

VideoWithSound::VideoWithSound(render::MpvMediaPlayer& picture, audio::AudioFilePlayer& sound)
    : picture_(picture)
    , sound_(sound)
{
}

bool VideoWithSound::do_load(std::string_view path, bool loop, double start_sec)
{
    set_speed(1.0);
    playing_ = false;
    waiting_ = false;
    if (!picture_.load(path, loop, start_sec)) {
        sound_.unload();
        return false;
    }
    // A file without a sound track turns the sound's has_media false a
    // moment later; the picture plays on regardless.
    sound_.load(path, loop, start_sec);
    return true;
}

void VideoWithSound::wait_for_picture()
{
    sound_.pause();
    waiting_ = true;
    wait_is_seek_ = false;
    jumping_ = false;
    far_ticks_ = 0;
    wait_mark_sec_ = picture_.position_sec();
    wait_since_ = Clock::now();
    smoothed_valid_ = false;
}

void VideoWithSound::play()
{
    picture_.play();
    // The app sends play again after every change while a video plays: only
    // a real start waits for the picture.
    if (playing_) {
        if (!waiting_) {
            sound_.play();
        }
        return;
    }
    playing_ = true;
    wait_for_picture();
}

void VideoWithSound::pause()
{
    playing_ = false;
    waiting_ = false;
    picture_.pause();
    sound_.pause();
    set_speed(1.0);
}

void VideoWithSound::seek(double position_sec)
{
    picture_.seek(position_sec);
    sound_.seek(position_sec);
    if (playing_) {
        wait_for_picture();
        wait_mark_sec_ = position_sec;
        wait_is_seek_ = true;
    }
}

void VideoWithSound::restart()
{
    picture_.restart();
    sound_.seek(0.0);
    playing_ = true;
    wait_for_picture();
    wait_mark_sec_ = 0.0;
}

void VideoWithSound::unload()
{
    // mpv keeps its speed from file to file: back to real time first.
    set_speed(1.0);
    playing_ = false;
    waiting_ = false;
    picture_.unload();
    sound_.unload();
}

bool VideoWithSound::has_media() const
{
    return picture_.has_media();
}

bool VideoWithSound::is_playing() const
{
    return picture_.is_playing();
}

bool VideoWithSound::ended() const
{
    // A sound track shorter than the picture ends first: the picture says.
    return picture_.ended();
}

double VideoWithSound::position_sec() const
{
    return picture_.position_sec();
}

double VideoWithSound::duration_sec() const
{
    return picture_.duration_sec();
}

bool VideoWithSound::loop() const
{
    return picture_.loop();
}

void VideoWithSound::keep_in_step(Clock::time_point now)
{
    if (!playing_ || !picture_.is_playing()) {
        set_speed(1.0);
        return;
    }
    if (waiting_) {
        // mpv's clock starts once it has finished starting or seeking, which
        // can take from nothing to a few hundred milliseconds.
        const bool started = picture_.clock_running();
        if (!started && now - wait_since_ < k_start_wait) {
            return;
        }
        if (started && wait_is_seek_) {
            // How long mpv took to show the new place: what the next jump
            // aims ahead by.
            learn_seek_delay(std::chrono::duration<double>(now - wait_since_).count());
        }
        // The sound starts from the picture, or from where both were told
        // to start when the picture took too long. It is usually there
        // already, and a seek it does not need would empty what it has
        // decoded ahead.
        waiting_ = false;
        const double from = started ? picture_.position_sec() : wait_mark_sec_;
        if (std::abs(sound_.position_sec() - from) > k_in_step_sec * 2) {
            sound_.seek(from);
        }
        sound_.play();
        return;
    }
    if (!sound_.is_playing()) {
        set_speed(1.0);
        return;
    }
    if (jumping_) {
        // Until the picture moves from where it jumped to, its place is only
        // where it was sent: nothing to steer by.
        if (!picture_.clock_running()) {
            if (now - last_seek_ < k_start_wait) {
                return;
            }
        } else {
            learn_seek_delay(std::chrono::duration<double>(now - last_seek_).count());
        }
        jumping_ = false;
    }
    const double sound_at = sound_.position_sec();
    const double drift =
        picture_drift(picture_.position_sec(), sound_at, picture_.duration_sec(), picture_.loop());
    // In step, the frame on show started up to a frame before the sound's
    // place: half a frame back on average.
    const double fps = picture_.frame_rate();
    const double frame_sec = fps > 0.0 ? 1.0 / fps : 0.0;
    const double in_step_at = -0.5 * frame_sec;
    // mpv's clock moves a frame at a time, so a single tick may read up to a
    // frame out; a real gap stays.
    const bool far = std::abs(drift - in_step_at) > std::max(k_seek_drift_sec, 2.0 * frame_sec);
    far_ticks_ = far ? far_ticks_ + 1 : 0;
    if (std::abs(drift) > k_jump_drift_sec || far_ticks_ >= k_seek_after_ticks) {
        far_ticks_ = 0;
        if (now - last_seek_ >= k_seek_rest) {
            // Where the sound will be once mpv shows the new place.
            double target = sound_at + seek_delay_sec_;
            const double length = picture_.duration_sec();
            if (length > 0.0) {
                target = picture_.loop() ? std::fmod(target, length) : std::min(target, length);
            }
            picture_.seek(target);
            last_seek_ = now;
            jumping_ = true;
            smoothed_valid_ = false;
            set_speed(1.0);
        }
        return;
    }
    if (!smoothed_valid_) {
        smoothed_drift_ = drift;
        smoothed_valid_ = true;
    } else {
        const double dt = std::chrono::duration<double>(now - last_tick_).count();
        const double alpha = dt > 0.0 ? dt / (k_drift_smoothing_sec + dt) : 0.0;
        smoothed_drift_ += (drift - smoothed_drift_) * alpha;
    }
    last_tick_ = now;
    set_speed(picture_speed_for_drift(smoothed_drift_ - in_step_at));
}

void VideoWithSound::learn_seek_delay(double took_sec)
{
    seek_delay_sec_ = 0.5 * seek_delay_sec_ + 0.5 * std::min(took_sec, 1.0);
}

void VideoWithSound::set_speed(double speed)
{
    // Back to real time always goes through; a small nudge waits until it
    // adds up.
    const bool to_real_time = speed == 1.0 && speed_ != 1.0;
    if (!to_real_time && std::abs(speed - speed_) < k_speed_step) {
        return;
    }
    speed_ = speed;
    picture_.set_speed(speed);
}

} // namespace seventhslide::app
