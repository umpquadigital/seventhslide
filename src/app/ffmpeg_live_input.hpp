#pragma once

// A live picture and sound read through FFmpeg as they arrive: a capture
// device (V4L2, DirectShow, AVFoundation through libavdevice), and FFmpeg's
// own test sources (lavfi) for the tests. Read on a thread of its own; the
// latest picture is kept and the sound pushed to a StreamSource. An input
// that cannot be opened, or that stops, is tried again every two seconds
// until it is back. Reading is paced by the input's own timestamps, so an
// input that runs faster than real time (a test source) does not flood.
// Seam: FFmpeg, Qt Gui (the pictures are QImages).

#include <seventhslide/audio/stream_source.hpp>

#include <QImage>
#include <QSize>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace seventhslide::app {

/// A capture device on this computer, as FFmpeg lists it.
struct CaptureDevice {
    /// What it calls itself ("HD Pro Webcam C920"); a second of the same
    /// name has " (2)" after it, the one in the later port (the same one
    /// each time while both stay where they are).
    std::string name;
    /// What FFmpeg opens: ``/dev/video0``; DirectShow's and AVFoundation's
    /// own names.
    std::string url;
    /// What FFmpeg is told as it opens it: on macOS the frame rate the
    /// device runs at, as avfoundation otherwise asks for 29.97 and will
    /// not open a camera that has no such rate.
    std::map<std::string, std::string> options;
};

/// A device as listed, before two of the same name are told apart.
struct ListedDevice {
    CaptureDevice device;
    /// What tells it from another of its name, the same from one start to
    /// the next: where it is plugged in.
    std::string order;
};

/// Two devices of the same name told apart by where they are, not by the
/// order they were listed in (which may change): the later one by
/// ``order`` gets " (2)", and so on.
[[nodiscard]] std::vector<CaptureDevice> tell_apart(std::vector<ListedDevice> listed);

/// FFmpeg's device input on this platform: ``v4l2``, ``dshow`` or
/// ``avfoundation``.
[[nodiscard]] const char* capture_input_format();
/// The capture devices on this computer now (video ones).
[[nodiscard]] std::vector<CaptureDevice> capture_devices();

class FfmpegLiveInput {
public:
    struct Spec {
        /// FFmpeg's input format (``v4l2``, ``lavfi``); empty to let it guess.
        std::string format;
        std::string url;
        std::map<std::string, std::string> options;
    };
    struct Stats {
        QSize size;
        double frame_rate = 0.0;
        std::uint64_t frames = 0;
    };
    using FrameReady = std::function<void()>;

    /// How long between tries of an input that cannot be read.
    static constexpr int k_retry_ms = 2000;

    /// The sound goes to ``sound`` when given (one that outlives this input,
    /// so a stack's strip can keep it across inputs), else to its own.
    FfmpegLiveInput(Spec spec, FrameReady on_frame, audio::StreamSource* sound = nullptr);
    ~FfmpegLiveInput();
    FfmpegLiveInput(const FfmpegLiveInput&) = delete;
    FfmpegLiveInput& operator=(const FfmpegLiveInput&) = delete;

    /// The latest picture (RGB32); null before one.
    [[nodiscard]] QImage frame() const;
    [[nodiscard]] Stats stats() const;
    /// Why there are no pictures (FFmpeg's words); empty while there are.
    [[nodiscard]] std::string problem() const;
    [[nodiscard]] audio::StreamSource& sound() noexcept;

    /// How long stopping waits for the reader before it lets it go. A device
    /// input can block in FFmpeg minding no interrupt (avfoundation waits
    /// for a frame a camera never sends); waiting on would hang the window.
    static constexpr int k_stop_wait_ms = 1500;

private:
    /// What the reader thread works on, shared with it: a reader that will
    /// not stop is let go holding it, and touches nothing else again.
    struct State;
    std::shared_ptr<State> state_;
    std::thread thread_;
};

} // namespace seventhslide::app
