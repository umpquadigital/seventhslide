#pragma once

// How the link's messages go down a connection: each frame is a 4-byte
// big-endian length, a 1-byte kind (``0`` JSON text, ``1`` raw bytes) and
// the payload. JSON frames are the protocol's messages; a bytes frame is a
// piece of a media file, or of a store file too large to go whole
// (``k_inline_file``), announced by the JSON frame before it.
//
// Seam: Qt Core (QByteArray) and nlohmann/json.

#include <QByteArray>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>

namespace seventhslide::link {

/// A bytes frame never exceeds this.
inline constexpr std::int64_t k_max_bytes_frame = 1024 * 1024;
/// A change file, snapshot's index or part larger than this goes in pieces,
/// in bytes frames (up to ``sync::k_largest_store_file`` in all).
inline constexpr std::int64_t k_inline_file = 1024 * 1024;
/// The longest message: a change file, index or part sent whole, as JSON text
/// (where a byte is six at most, ``\u00XX``), with room for the rest of its
/// message. Every other message is far shorter.
inline constexpr std::int64_t k_max_json_frame = 6 * k_inline_file + 64 * 1024;
/// A message from a computer not yet let in to any library (its hello, an
/// offer) is small; nor does it send media.
inline constexpr std::int64_t k_max_json_frame_unadmitted = 64 * 1024;

struct Frame {
    bool bytes = false;
    QByteArray payload;
};

[[nodiscard]] QByteArray encode_message(const nlohmann::json& message);
[[nodiscard]] QByteArray encode_bytes(const QByteArray& bytes);

/// Reassembles frames from whatever arrives.
class FrameReader {
public:
    void feed(const QByteArray& data);
    /// The longest message and media piece ``next`` takes from now on (the
    /// largest there are, unless set).
    void set_limits(std::int64_t json, std::int64_t bytes) noexcept
    {
        max_json_ = json;
        max_bytes_ = bytes;
    }
    /// The next whole frame, if one has arrived. Throws ``std::runtime_error``
    /// for a frame that cannot be one (longer than the limits, an unknown
    /// kind): the connection is then not worth keeping.
    [[nodiscard]] std::optional<Frame> next();

private:
    /// What has come, from ``read_`` on not yet taken. The bytes before it are
    /// let go of in ``feed`` once they are as many as those after, so taking a
    /// frame moves nothing and the bytes moved stay fewer than those taken.
    QByteArray buffer_;
    qsizetype read_ = 0;
    std::int64_t max_json_ = k_max_json_frame;
    std::int64_t max_bytes_ = k_max_bytes_frame;
};

}  // namespace seventhslide::link
