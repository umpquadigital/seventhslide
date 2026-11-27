#pragma once

// Receiving NDI: the sources on the network, and one source's pictures and
// sound. The runtime is the one the sender loads (ndi_runtime.hpp), never
// linked; a runtime without the find and receive entry points finds nothing
// and receives nothing. Seam: Qt Gui (the pictures are QImages), the NDI
// runtime.

#include <seventhslide/audio/stream_source.hpp>

#include <QImage>
#include <QSize>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace seventhslide::render {

/// The NDI runtime is here and can find and receive sources.
[[nodiscard]] bool ndi_receive_available();

/**
 * The NDI sources on the network now, this computer's own included. The
 * runtime listens in the background once made, so ``sources`` is cheap and
 * may be called as often as wanted; a source just started shows within a
 * second or two.
 */
class NdiFinder {
public:
    NdiFinder();
    ~NdiFinder();
    NdiFinder(const NdiFinder&) = delete;
    NdiFinder& operator=(const NdiFinder&) = delete;

    [[nodiscard]] bool available() const noexcept { return finder_ != nullptr; }
    /// Each source's name, as it names itself: ``HOST (Camera 1)``.
    [[nodiscard]] std::vector<std::string> sources() const;

private:
    void* finder_ = nullptr;
};

/**
 * One NDI source, received on a thread of its own: its latest picture, and
 * its sound into ``sound()`` (any rate, any channels: the first two are kept,
 * one is doubled). The source is asked for by name, so it is received as soon
 * as it appears on the network and again after it goes and comes back.
 */
class NdiReceiver {
public:
    /// Called on the receiving thread after each new picture.
    using FrameReady = std::function<void()>;

    struct Stats {
        QSize size;
        /// As the source declares it; 0 before the first picture.
        double frame_rate = 0.0;
        std::uint64_t frames = 0;
        /// A source is connected (-1: this runtime cannot say).
        int connections = -1;
    };

    /// What is asked of the source: all of it, or the small preview stream
    /// NDI makes for multiviewers (a thumbnail's worth, little network).
    enum class Bandwidth { Full, Preview };

    NdiReceiver(std::string source_name, FrameReady on_frame,
                Bandwidth bandwidth = Bandwidth::Full);
    ~NdiReceiver();
    NdiReceiver(const NdiReceiver&) = delete;
    NdiReceiver& operator=(const NdiReceiver&) = delete;

    [[nodiscard]] bool available() const noexcept { return receiver_ != nullptr; }
    [[nodiscard]] const std::string& source_name() const noexcept { return name_; }
    /// The latest picture (premultiplied ARGB32 or RGB32); null before one.
    [[nodiscard]] QImage frame() const;
    [[nodiscard]] Stats stats() const;
    [[nodiscard]] audio::StreamSource& sound() noexcept { return sound_; }

private:
    void run();
    void take_video(const void* frame);
    void take_audio(const void* frame);

    std::string name_;
    FrameReady on_frame_;
    void* receiver_ = nullptr;
    audio::StreamSource sound_;
    std::vector<float> interleaved_;
    mutable std::mutex mu_;
    QImage frame_;
    Stats stats_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

} // namespace seventhslide::render
