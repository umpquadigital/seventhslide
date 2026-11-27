#pragma once

// NDI sender sink. Cross-platform: works wherever the operator has installed
// an NDI runtime (see ndi_runtime.hpp). SeventhSlide never ships NDI itself.
// Seam: Qt Gui + the dlopen'd NDI C ABI.

#include "seventhslide/render/ndi_abi.hpp"
#include "seventhslide/render/offscreen_sink.hpp"

#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/audio/mixer_link.hpp>

#include <QByteArray>
#include <QImage>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace seventhslide::render {

/// Frame rate advertised to receivers: the keepalive's, 30 a second. The
/// sender does not clock its video, and sends a picture on each keepalive and
/// each change between, so this is what receivers are told to expect rather
/// than a pace kept to the frame.
inline constexpr int k_ndi_frame_rate_n = 30;
inline constexpr int k_ndi_frame_rate_d = 1;

/// Trim and strip characters that break NDI's mDNS source naming. Returns an
/// empty string when nothing usable is left.
[[nodiscard]] QString ndi_sanitize_source_name(const QString& name);

/// Default source name for an output, e.g. "SeventhSlide (Main)". Receivers
/// see this prefixed with the machine name by the NDI runtime itself.
[[nodiscard]] QString ndi_default_source_name(const QString& output_name);

/**
 * Offscreen SceneFrame -> NDI sender.
 *
 * Qt's Format_RGB32 / Format_ARGB32 are B,G,R,X / B,G,R,A in memory on
 * little-endian, which is exactly NDI's BGRX / BGRA. Each picture is converted
 * once from the composite's premultiplied form to one of those (unpremultiplied,
 * as NDI takes it), and sent as it is.
 *
 * Delivery uses ``send_video_async_v2``, which returns immediately but keeps
 * referencing the buffer until the *next* send. Two images are ping-ponged so
 * a frame in flight is never overwritten, and the sender is flushed with a
 * null frame before teardown. The normalization and the send happen on a
 * thread of the sink's own; the window's thread only hands the picture over.
 *
 * Sound: given the Audio Mixer (``set_audio``), a live sender is one of the
 * mixer's sinks for its output and sends that output's mix beside the
 * pictures, as planar float at 48 kHz. The mixer's clock thread only copies
 * each block into a DriftBuffer; a thread of the sink's own sends a block
 * every 10 ms by the system clock, so a slow network never holds up the
 * mixer (and with it the headphones), and receivers get the sound evenly
 * however unevenly the Listen device asks for it. A runtime too old to send
 * audio streams pictures only. With video off (``set_video_enabled``) the
 * source carries the sound alone.
 *
 * Receivers and tally are polled on a timer of the sink's own, so a source
 * sending sound alone still reports them.
 */
class NdiSink final : public OffscreenSink, public audio::OutputAudioSink {
    Q_OBJECT

public:
    explicit NdiSink(QObject* parent = nullptr);
    ~NdiSink() override;

    /// The NDI source name receivers see. Alias for ``target()``.
    [[nodiscard]] QString source_name() const { return target(); }

    /// Send BGRA over a transparent background instead of opaque BGRX, so a
    /// downstream switcher can key the output. Takes effect on the next frame.
    void set_alpha_enabled(bool on) override;
    [[nodiscard]] bool alpha_enabled() const noexcept { return alpha_; }

    /// Receivers connected (-1 when the runtime cannot report it), and the
    /// program / preview tally from a downstream switcher.
    [[nodiscard]] Status status() const override;

    void set_audio(audio::Mixer* mixer, std::string output_id, bool on) override;
    /// The runtime must also be able to send audio.
    [[nodiscard]] bool sending_audio() const override { return audio_ != nullptr; }
    /// Sound frames sent (one per channel) since the sound last started
    /// (tests).
    [[nodiscard]] std::uint64_t audio_frames_sent() const noexcept;

    /// The mixer's clock thread: ``count`` interleaved stereo frames of the
    /// output's mix.
    void push(const float* frames, std::size_t count) override;

protected:
    bool on_start(QSize size) override;
    void on_stop() override;
    void deliver(const QImage& image, bool changed) override;
    [[nodiscard]] QColor letterbox_color() const override;

private:
    /// Ask the runtime for receivers and tally; never blocks.
    void poll_status();
    /// Join the mixer when everything sound needs is there.
    void attach_audio();
    /// Leave the mixer, then stop sending: after this nothing calls push.
    void detach_audio();

    struct AudioPath;
    struct VideoPath;

    const ndi::Api* api_ = nullptr;
    ndi::send_instance_t sender_ = nullptr;
    bool alpha_ = false;
    int connections_ = -1;
    bool on_program_ = false;
    bool on_preview_ = false;
    /// Runs poll_status while live.
    QTimer* status_timer_ = nullptr;

    /// While sending sound; set and cleared only outside the mixer's reach
    /// (before joining, after leaving).
    std::unique_ptr<AudioPath> audio_;
    audio::MixerLink link_{*this};

    QByteArray name_utf8_;  // must outlive send_create
    /// While live: sends the pictures.
    std::unique_ptr<VideoPath> pictures_;
};

}  // namespace seventhslide::render
