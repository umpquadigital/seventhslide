#pragma once

// Text files from other programs, as UTF-8.
// Seam: plain C++20 (no Qt).

#include <string>
#include <string_view>

namespace seventhslide::util {

/// True when ``bytes`` are well-formed UTF-8 (no overlongs, no surrogates).
[[nodiscard]] bool is_valid_utf8(std::string_view bytes) noexcept;

/// ``bytes`` as UTF-8, without a byte order mark. A BOM says what they are
/// (UTF-8, UTF-16 LE or BE); otherwise valid UTF-8 is taken as it is, and
/// anything else is read as Windows-1252, as older song files on Windows
/// are written. Never fails: what cannot be decoded becomes U+FFFD.
[[nodiscard]] std::string decode_text(std::string_view bytes);

}  // namespace seventhslide::util
