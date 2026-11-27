#pragma once

// Blackmagic DeckLink sender sink. Works wherever the operator has installed
// Desktop Video (see bmd_runtime.hpp). SeventhSlide never ships any part of it.
// Seam: Qt Gui + the vendored DeckLink SDK, which stays behind the pimpl -
// no SDK type appears in this header.

#include "seventhslide/render/bmd_runtime.hpp"
#include "seventhslide/render/offscreen_sink.hpp"

#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/audio/mixer_link.hpp>

#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace seventhslide::render {

/// What a DeckLink card keeps of its output's sound buffered: two of its
/// frames, and never under 40 ms, so a picture that is late by a frame does
/// not leave the sound short.
[[nodiscard]] std::uint32_t bmd_audio_target_frames(const BmdDisplayModeInfo& mode) noexcept;

/// How many sound frames to write so a card holding ``buffered`` holds
/// ``target`` again; none while it holds that much.
[[nodiscard]] std::uint32_t bmd_audio_top_up(std::uint32_t buffered, std::uint32_t target) noexcept;

/// ``samples`` floats (full scale at 1) as the card's signed 32-bit samples,
/// clipped.
void bmd_audio_pack(const float* in, std::size_t samples, std::int32_t* out) noexcept;

/**
 * Offscreen SceneFrame -> DeckLink output.
 *
 * Unlike NDI, a DeckLink output is locked to a broadcast video mode rather than
 * carrying the canvas size per frame. The mode is chosen by the operator and
 * the canvas is letterboxed into its raster, so ``negotiate_size`` returns the
 * mode and ``on_resize`` refuses: a canvas change must never drop a live SDI
 * feed mid-service.
 *
 * Threading: ``DisplayVideoFrameSync`` can block until the frame is displayed,
 * so every DeckLink object is created and driven on a private writer thread -
 * which on Windows doubles as the MTA apartment those objects want. The GUI
 * thread only renders and packs.
 *
 * Sound: the card embeds its output's mix from the Audio Mixer (``set_audio``)
 * as 48 kHz, 32-bit, two-channel audio. The mixer's clock pushes into a
 * DriftBuffer; after each picture the writer thread tops the card's own
 * buffer up to ``bmd_audio_target_frames``, so the card's clock decides how
 * much is taken and the buffer follows it. Without a mixer, or switched off,
 * the card plays silence.
 */
class BmdSink final : public OffscreenSink, public audio::OutputAudioSink {
    Q_OBJECT

public:
    explicit BmdSink(QObject* parent = nullptr);
    ~BmdSink() override;

    /// Video mode key from ``bmd_list_display_modes``. Empty picks the
    /// device's best 1080p mode. Set before ``start()``; changing it on a live
    /// sink does nothing until the next start.
    void set_mode_key(QString key);
    [[nodiscard]] QString mode_key() const;

    /// The mode this sink resolved to. Valid once the device has been seen,
    /// even before ``start()``.
    [[nodiscard]] BmdDisplayModeInfo active_mode() const;

    /// Send an alpha channel and turn on the card's keyer, so a downstream
    /// switcher can key the output. Takes effect on the next start.
    void set_alpha_enabled(bool on) override;
    [[nodiscard]] bool alpha_enabled() const noexcept;

    /// True when the open actually negotiated the keyer on - alpha was asked
    /// for AND the card has one.
    [[nodiscard]] bool keying_active() const noexcept;

    [[nodiscard]] Status status() const override;

    void set_audio(audio::Mixer* mixer, std::string output_id, bool on) override;
    /// The card must also have taken audio output.
    [[nodiscard]] bool sending_audio() const override { return link_.joined(); }
    /// The mixer's clock thread: ``count`` interleaved stereo frames.
    void push(const float* frames, std::size_t count) override;

protected:
    bool on_start(QSize size) override;
    void on_stop() override;
    void deliver(const QImage& image, bool changed) override;
    bool on_resize(QSize size) override;
    [[nodiscard]] QSize negotiate_size(QSize want) const override;
    [[nodiscard]] QColor letterbox_color() const override;
    [[nodiscard]] int tick_interval_ms() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    /// After d_: it leaves the mixer before d_'s buffer goes.
    audio::MixerLink link_{*this};

    void worker_loop(BmdDisplayModeInfo mode);
    [[nodiscard]] BmdDisplayModeInfo resolve_mode() const;
};

}  // namespace seventhslide::render
