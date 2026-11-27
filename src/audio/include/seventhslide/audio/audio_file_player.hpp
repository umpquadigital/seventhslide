#pragma once

// One stack's audio item, decoded by FFmpeg and played into the mixer. It is
// the stack's IMediaPlayer (the transport) and the mixer's AudioSource (the
// sound) at once. Any file with an audio track plays, a video's included.
// Seam: plain C++20 (no Qt); FFmpeg stays in the .cpp.

#include "seventhslide/audio/audio_source.hpp"
#include "seventhslide/media/media_player.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>

namespace seventhslide::audio {

/**
 * A file played into the mixer.
 *
 * Three threads touch it: the caller's (the transport below), its own decode
 * thread, which keeps about half a second decoded ahead, and the audio thread,
 * which takes it a block at a time through ``read``. They meet only under a
 * short lock round that half second.
 *
 * Like a video's player: ``load`` returns at once and a file FFmpeg
 * cannot play turns ``has_media`` false a moment later; a file loads paused;
 * at its end it stops (``is_playing`` false, the clock at the end) unless it
 * loops, and a seek from there leaves it paused at the new place. The clock is the frames the mixer has taken, so it is exact; while
 * the decoder has fallen behind the mixer hears silence and the clock waits.
 *
 * Nothing it does clicks: a pause fades out over 10 ms (the clock running on
 * through the fade), the sound fades in when it starts again anywhere but
 * the file's start, and a seek or a new file cross-fades with what was
 * playing. A decoder that falls behind fades out too.
 */
class AudioFilePlayer final : public media::IMediaPlayer, public AudioSource {
public:
    AudioFilePlayer();
    ~AudioFilePlayer() override;

    AudioFilePlayer(const AudioFilePlayer&) = delete;
    AudioFilePlayer& operator=(const AudioFilePlayer&) = delete;

    void play() override;
    void pause() override;
    void seek(double position_sec) override;
    void restart() override;
    void unload() override;

    [[nodiscard]] bool has_media() const override;
    [[nodiscard]] bool is_playing() const override;
    [[nodiscard]] bool ended() const override;
    [[nodiscard]] double position_sec() const override;
    [[nodiscard]] double duration_sec() const override;
    [[nodiscard]] bool loop() const override;

    void read(float* out, std::size_t frames) override;

    /// The player's own level on top of everything above, 0 to 1, moved to
    /// ``target`` evenly over ``over`` (at once for 0): the same on every
    /// output, as a video object's mute. A media item's fades are each
    /// output's, in the mixer (``Mixer::fade_source``). Starts at 1; a load
    /// leaves it where it is.
    void fade_level(float target, std::chrono::milliseconds over);
    /// Where the level is now, and where it is going.
    [[nodiscard]] float level() const;
    [[nodiscard]] float level_target() const;

    /// Wait until the decoder has filled what it keeps ahead, reached the end
    /// of the file, or failed. False on ``timeout``. For tests and tools,
    /// which read faster than real time; a zero ``timeout`` asks whether it
    /// has yet (the app's clock, waiting on a file's length).
    bool wait_buffered(std::chrono::milliseconds timeout) const;
    /// Times the decoder, with what it keeps ahead decoded, has looked again
    /// for room (tests): while it plays, every 20 ms; paused, only once the
    /// mixer has taken some.
    [[nodiscard]] std::uint64_t refill_looks() const;

private:
    /// The file's sound from ``start_sec``: the decoder starts there.
    bool do_load(std::string_view path, bool loop, double start_sec) override;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace seventhslide::audio
