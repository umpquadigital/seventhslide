#pragma once

// Playback seam for timed media: a stack's video or audio. Session / app
// depend on this interface - never on mpv types. Implementation:
// render/MpvMediaPlayer (libmpv required).
// Seam: plain C++20 (no Qt).

#include <string>
#include <string_view>

namespace seventhslide::media {

/**
 * Decode / transport interface for one video or audio file.
 *
 * ``load`` takes a filesystem path (resolved from ``/static/videos/...`` or
 * ``/static/audio/...``). Position and duration are seconds; unknown
 * duration -> 0.
 */
class IMediaPlayer {
public:
    virtual ~IMediaPlayer() = default;

    /// Load ``path``, paused, standing at ``start_sec`` (clamped by the
    /// impl), so what was playing goes on from where it was. ``loop`` maps
    /// to endless playback, which comes round to the start of the file, not
    /// to ``start_sec``. Returns false if missing.
    bool load(std::string_view path, bool loop, double start_sec = 0.0)
    {
        return do_load(path, loop, start_sec);
    }
    virtual void play() = 0;
    virtual void pause() = 0;
    /// Absolute seek (clamped by impl).
    virtual void seek(double position_sec) = 0;
    /// Seek 0 and play.
    virtual void restart() = 0;
    virtual void unload() = 0;

    [[nodiscard]] virtual bool has_media() const = 0;
    [[nodiscard]] virtual bool is_playing() const = 0;
    /// It played to its end and stopped there (never while it loops).
    [[nodiscard]] virtual bool ended() const = 0;
    [[nodiscard]] virtual double position_sec() const = 0;
    [[nodiscard]] virtual double duration_sec() const = 0;
    [[nodiscard]] virtual bool loop() const = 0;

private:
    /// ``load``: where a file starts is said with the load, not by a seek
    /// after it, which a file still opening may not take.
    virtual bool do_load(std::string_view path, bool loop, double start_sec) = 0;
};

/// ``player``'s position and length as a clock shows them: 0 with no player
/// or file, and for anything not a finite, positive number of seconds.
[[nodiscard]] double shown_position_sec(const IMediaPlayer* player) noexcept;
[[nodiscard]] double shown_duration_sec(const IMediaPlayer* player) noexcept;

/// Format remaining time as ``-MM:SS`` (or empty).
[[nodiscard]] std::string format_video_countdown(double position_sec,
                                                 double duration_sec);

} // namespace seventhslide::media
