#pragma once

// SRT destination sink: the output's pictures and its mix streamed over SRT
// (see stream::SrtStreamer), to a receiver or to whichever one connects.
// Seam: Qt Gui. Encoding and sending are the streamer's, on its own thread.

#include "seventhslide/render/offscreen_sink.hpp"

#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/audio/mixer_link.hpp>
#include <seventhslide/stream/srt_streamer.hpp>
#include <seventhslide/stream/stream_settings.hpp>

#include <QImage>
#include <QSize>

#include <cstddef>
#include <string>

namespace seventhslide::render {

/**
 * SceneFrame -> SRT stream.
 *
 * Each changed picture is handed to the streamer, which sends the newest 30
 * times a second by its own clock; a keepalive tick hands over nothing.
 * The stream's format is fixed while it runs, so a new size starts it again
 * (the receiver reconnects). Its sound is the output's mix, joined to the
 * mixer while live; with Send audio off the stream carries silence, so a
 * receiver keeps its sound track.
 */
class SrtSink final : public OffscreenSink, public audio::OutputAudioSink {
    Q_OBJECT

public:
    explicit SrtSink(QObject* parent = nullptr);
    ~SrtSink() override;

    /// How to stream. Takes effect at the next start.
    void set_settings(stream::StreamSettings settings) { settings_ = std::move(settings); }

    /// Where the stream is, and with what encoder.
    [[nodiscard]] Status status() const override;

    void set_audio(audio::Mixer* mixer, std::string output_id, bool on) override;
    [[nodiscard]] bool sending_audio() const override { return link_.joined(); }

    /// The mixer's clock thread: the output's mix.
    void push(const float* frames, std::size_t count) override;

    /// Pictures sent over the current connection (tests).
    [[nodiscard]] std::int64_t pictures_sent() const noexcept { return streamer_.pictures_sent(); }

protected:
    bool on_start(QSize size) override;
    void on_stop() override;
    void deliver(const QImage& image, bool changed) override;
    bool on_resize(QSize size) override;

private:
    stream::StreamSettings settings_;
    stream::SrtStreamer streamer_;
    audio::MixerLink link_{*this};
};

}  // namespace seventhslide::render
