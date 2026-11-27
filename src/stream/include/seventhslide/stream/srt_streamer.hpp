#pragma once

// One SRT stream: pictures and sound encoded (H.264 or H.265, and AAC) into
// MPEG-TS and sent to a receiver, or to whichever receiver connects.
//
// A thread of its own does everything that can wait - connecting, waiting
// for a receiver, encoding, sending - so neither the GUI thread (which hands
// in pictures) nor the mixer's clock (which hands in sound) ever does. It
// keeps a steady 30 pictures a second by the system clock, the newest picture
// each time. While connected, a second thread takes the sound that time
// holds from a DriftBuffer in 10 ms blocks, as the buffer is made to be read,
// and each picture is sent with the sound taken since the last, so the two
// stay together however unevenly they arrive. A connection that is lost, or
// a receiver that cannot be reached, is tried again every two seconds until
// stopped; each connection starts its encoders afresh so the receiver has a
// keyframe at once.
// Seam: plain C++20 (FFmpeg behind it).

#include "seventhslide/stream/encoder_catalog.hpp"
#include "seventhslide/stream/stream_settings.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace seventhslide::stream {

enum class StreamState {
    Stopped,
    /// Listener: waiting for a receiver to connect.
    Waiting,
    /// Caller: connecting to the receiver.
    Connecting,
    Sending,
    /// The receiver went or could not be reached; trying again shortly.
    Retrying,
    /// It cannot stream at all (no encoder opens); ``error`` says why.
    Failed,
};

[[nodiscard]] std::string_view state_name(StreamState state);

struct StreamStatus {
    StreamState state = StreamState::Stopped;
    /// The encoder in use, once one has opened.
    EncoderInfo encoder;
    int bitrate_kbps = 0;
    /// Why it is retrying or failed; empty otherwise.
    std::string error;

    [[nodiscard]] bool operator==(const StreamStatus&) const = default;
};

class SrtStreamer {
public:
    /// Told the status changed; called on the streaming thread.
    using StatusListener = std::function<void()>;

    SrtStreamer();
    ~SrtStreamer();
    SrtStreamer(const SrtStreamer&) = delete;
    SrtStreamer& operator=(const SrtStreamer&) = delete;

    /// Set before ``start``; it is called until ``stop`` returns.
    void set_status_listener(StatusListener listener);

    /// Stream at ``width`` x ``height`` (even sides), as ``settings`` says,
    /// in place of what was streaming. False, with ``error`` set and what
    /// was streaming carrying on, when the settings are invalid or
    /// incomplete, or their codec's encoders have been tried on this
    /// computer (``known_encoders``) and none opens. When they have not been
    /// tried yet, what was streaming stops, and the new stream tries them
    /// without this waiting; finding none leaves it ``Failed``. ``stop``
    /// meanwhile does not wait for them either.
    bool start(const StreamSettings& settings, int width, int height, std::string& error);
    /// Stop and wait for the thread; nothing more is sent.
    void stop();
    [[nodiscard]] bool running() const noexcept;

    /// The GUI thread: the picture to send from now on, 8-bit B,G,R,X rows
    /// top first. Copied; scaled when it is not the size started at.
    void submit_picture(const std::uint8_t* pixels, int width, int height, int stride);
    /// The mixer's clock: ``count`` interleaved stereo frames at 48 kHz.
    /// Never blocks for long or allocates.
    void push_audio(const float* frames, std::size_t count);

    [[nodiscard]] StreamStatus status() const;
    /// Pictures and sound frames sent over the current connection (tests).
    [[nodiscard]] std::int64_t pictures_sent() const noexcept;
    [[nodiscard]] std::int64_t audio_frames_sent() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace seventhslide::stream
