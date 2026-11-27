#pragma once

// CRC-32 (IEEE 802.3, as zlib and every ZIP entry compute it), over bytes
// given at once or a piece at a time.
//
// Plain C++20 (no Qt).

#include <cstdint>
#include <string_view>

namespace seventhslide::util {

class Crc32 {
public:
    /// Take in the next ``bytes``.
    void update(std::string_view bytes) noexcept;
    /// The CRC of everything taken in so far.
    [[nodiscard]] std::uint32_t value() const noexcept { return state_ ^ 0xFFFFFFFFu; }

private:
    std::uint32_t state_ = 0xFFFFFFFFu;
};

/// The CRC-32 of ``bytes``.
[[nodiscard]] std::uint32_t crc32(std::string_view bytes) noexcept;

}  // namespace seventhslide::util
