#pragma once

// An audio item's player as the session drives it: its sound, but the
// transport's Play and Pause go to ``hook`` (PlaybackController), which fades
// them on each output when the item's fades say so (``on_play_pause``), or
// passes them straight to the sound. A Pause that fades leaves the sound
// playing until the fade is over; meanwhile it says it is paused, so the
// session's clock and the wells show what was asked. What the controller does
// itself (loading, taking, cueing) goes to the sound directly.

#include <seventhslide/audio/audio_file_player.hpp>
#include <seventhslide/media/media_player.hpp>

#include <functional>
#include <string_view>
#include <utility>

namespace seventhslide::app {

class AudioTransport final : public media::IMediaPlayer {
public:
    /// ``true`` for Play, ``false`` for Pause.
    using Hook = std::function<void(bool play)>;

    /// ``sound`` must outlive this.
    AudioTransport(audio::AudioFilePlayer& sound, Hook hook)
        : sound_(sound)
        , hook_(std::move(hook))
    {
    }

    void play() override { hook_(true); }
    void pause() override { hook_(false); }
    void seek(double position_sec) override { sound_.seek(position_sec); }
    /// From the start, playing: the start, then Play as the transport's.
    void restart() override
    {
        sound_.seek(0.0);
        hook_(true);
    }
    void unload() override
    {
        pausing_ = false;
        sound_.unload();
    }

    [[nodiscard]] bool has_media() const override { return sound_.has_media(); }
    /// Playing, and not fading out to a Pause.
    [[nodiscard]] bool is_playing() const override { return sound_.is_playing() && !pausing_; }
    [[nodiscard]] bool ended() const override { return sound_.ended(); }
    [[nodiscard]] double position_sec() const override { return sound_.position_sec(); }
    [[nodiscard]] double duration_sec() const override { return sound_.duration_sec(); }
    [[nodiscard]] bool loop() const override { return sound_.loop(); }

    /// A Pause is fading out: the sound still plays, but not as far as the
    /// session is concerned.
    void set_pausing(bool pausing) noexcept { pausing_ = pausing; }
    [[nodiscard]] bool pausing() const noexcept { return pausing_; }

private:
    bool do_load(std::string_view path, bool loop, double start_sec) override
    {
        pausing_ = false;
        return sound_.load(path, loop, start_sec);
    }

    audio::AudioFilePlayer& sound_;
    Hook hook_;
    bool pausing_ = false;
};

} // namespace seventhslide::app
