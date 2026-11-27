#pragma once

// libmpv backend for IMediaPlayer: a video's picture, its frames rendered in
// software to QImage for QPainter outputs, on a thread of the player's own. mpv plays no sound: a video's
// sound plays through the Audio Mixer (audio::AudioFilePlayer), and the app
// keeps the picture in step with it (set_speed).
// Seam: Qt Gui + libmpv.

#include "seventhslide/media/media_player.hpp"

#include <QImage>

#include <memory>
#include <mutex>
#include <string>

namespace seventhslide::render {

/**
 * libmpv player. Inject via ``Session::set_players``; Session never sees mpv types.
 */
class MpvMediaPlayer final : public media::IMediaPlayer {
public:
    MpvMediaPlayer();
    ~MpvMediaPlayer() override;

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

    /// The newest frame (ARGB32_Premultiplied), at most ``max_w`` by
    /// ``max_h``; null until the first. Frames are rendered on a thread of
    /// the player's own as mpv makes them; this only takes the newest. A
    /// size other than the last asked for comes a moment later.
    [[nodiscard]] QImage current_frame(int max_w = 1920, int max_h = 1080) const;

    /// Play faster or slower than real time (1 is real time), so the picture
    /// can follow its sound's clock.
    void set_speed(double speed);
    /// Frames per second, as the file says; 0 until known. ``position_sec``
    /// moves a frame at a time.
    [[nodiscard]] double frame_rate() const;
    /// The picture is moving: playing, past any start or seek, and its
    /// position has moved since (mpv stops being idle a moment before its
    /// first frame moves).
    [[nodiscard]] bool clock_running() const;

private:
    /// Starts opening ``path`` and returns at once, its first frame the one
    /// at ``start_sec``. ``current_frame`` stays null until that frame
    /// decodes; ``has_media`` turns false if mpv cannot open it.
    bool do_load(std::string_view path, bool loop, double start_sec) override;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace seventhslide::render
