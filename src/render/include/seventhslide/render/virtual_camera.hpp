#pragma once

// Linux virtual-camera sink (v4l2loopback), built on the shared OffscreenSink
// render/cadence base. Seam: Qt Gui + POSIX v4l2. Windows/macOS stay
// unimplemented (catalog off) - NDI is the cross-platform sink.

#include "seventhslide/render/latest_frame.hpp"
#include "seventhslide/render/offscreen_sink.hpp"

#include <seventhslide/audio/pipewire_source.hpp>

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <thread>
#include <vector>

namespace seventhslide::render {

/// Compile-time: this build can open a virtual camera (Linux v4l2loopback).
[[nodiscard]] constexpr bool virtual_camera_implemented() noexcept
{
#if defined(__linux__)
    return true;
#else
    return false;
#endif
}

struct VirtualCameraDeviceInfo {
    QString path;
    QString card;
    QString label;
};

[[nodiscard]] bool is_virtual_camera_device(const QString& path);
[[nodiscard]] std::vector<VirtualCameraDeviceInfo> list_virtual_camera_devices();
[[nodiscard]] QString virtual_camera_missing_message();

/// Even width/height, longest side capped at 1920x1080 (webcam-typical).
[[nodiscard]] QSize clamp_camera_size(QSize size);

/**
 * Offscreen SceneFrame -> v4l2loopback device.
 *
 * The base renders on the GUI thread; this sink encodes to YUYV and writes on
 * a worker so a blocked loopback (no consumer) cannot stall the operator UI.
 * The encoded buffer is cached across keepalive ticks.
 *
 * Sound: while live, and given the Audio Mixer (``set_audio``), it offers its
 * output's mix as a PipeWire microphone (``set_microphone_name``), so an app
 * takes the sound with the picture. Where PipeWire is not, pictures only.
 */
class VirtualCameraSink final : public OffscreenSink {
    Q_OBJECT

public:
    explicit VirtualCameraSink(QObject* parent = nullptr);
    ~VirtualCameraSink() override;

    /// What apps call the microphone ("SeventhSlide Main"). Takes effect the
    /// next time the sound starts.
    void set_microphone_name(QString name) { mic_name_ = std::move(name); }
    void set_audio(audio::Mixer* mixer, std::string output_id, bool on) override;
    [[nodiscard]] bool sending_audio() const override { return mic_.running(); }
    /// The microphone's node name in PipeWire; empty while it is not offered.
    [[nodiscard]] std::string microphone_node() const { return mic_.node_name(); }

protected:
    bool on_start(QSize size) override;
    void on_stop() override;
    void deliver(const QImage& image, bool changed) override;
    bool on_resize(QSize size) override;
    [[nodiscard]] QSize negotiate_size(QSize want) const override;

private:
    bool configure_format(QSize size);
    void ensure_writer();
    void stop_writer();
    void writer_loop();
    void fill_black_yuyv();
    /// Offer the microphone when the camera is ``live`` and everything the
    /// sound needs is there; else not.
    void sync_microphone(bool live);

    int fd_ = -1;
    /// The picture as last sent: the writer's while it runs.
    QByteArray yuyv_;

    /// What the writer sends next, packed on its thread.
    LatestFrame frames_;
    std::thread writer_;

    audio::Mixer* mixer_ = nullptr;
    std::string output_id_;
    bool audio_on_ = false;
    QString mic_name_;
    audio::PipeWireSource mic_;
};

}  // namespace seventhslide::render
