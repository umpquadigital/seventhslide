#include "seventhslide/link/frames.hpp"

#include <stdexcept>
#include <string>

namespace seventhslide::link {
namespace {

constexpr int k_header = 5;

QByteArray framed(char kind, const QByteArray& payload)
{
    const auto n = static_cast<std::uint32_t>(payload.size() + 1);
    QByteArray out;
    out.reserve(k_header + payload.size());
    out.append(static_cast<char>(n >> 24));
    out.append(static_cast<char>(n >> 16));
    out.append(static_cast<char>(n >> 8));
    out.append(static_cast<char>(n));
    out.append(kind);
    out.append(payload);
    return out;
}

}  // namespace

QByteArray encode_message(const nlohmann::json& message)
{
    return framed(0, QByteArray::fromStdString(message.dump()));
}

QByteArray encode_bytes(const QByteArray& bytes)
{
    if (bytes.size() > k_max_bytes_frame) {
        throw std::invalid_argument("A bytes frame holds at most 1 MiB");
    }
    return framed(1, bytes);
}

void FrameReader::feed(const QByteArray& data)
{
    if (read_ > 0 && read_ >= buffer_.size() - read_) {
        buffer_.remove(0, read_);
        read_ = 0;
    }
    buffer_.append(data);
}

std::optional<Frame> FrameReader::next()
{
    const qsizetype left = buffer_.size() - read_;
    if (left < k_header) {
        return std::nullopt;
    }
    const auto* b = reinterpret_cast<const unsigned char*>(buffer_.constData() + read_);
    const std::int64_t n = (std::int64_t{b[0]} << 24) | (std::int64_t{b[1]} << 16)
                           | (std::int64_t{b[2]} << 8) | std::int64_t{b[3]};
    const auto kind = static_cast<char>(b[4]);
    if (n < 1 || kind < 0 || kind > 1 || n - 1 > (kind == 1 ? max_bytes_ : max_json_)) {
        throw std::runtime_error("The other computer sent something that is not a sync message");
    }
    if (left < k_header - 1 + n) {
        return std::nullopt;
    }
    Frame frame{kind == 1, buffer_.mid(read_ + k_header, static_cast<qsizetype>(n - 1))};
    read_ += static_cast<qsizetype>(k_header - 1 + n);
    if (read_ == buffer_.size()) {
        buffer_.resize(0);
        read_ = 0;
    }
    return frame;
}

}  // namespace seventhslide::link
