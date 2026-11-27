#pragma once

// Bytes as text, for keys, signatures and codes kept in JSON or shown to
// people: base64 (RFC 4648, padded) and base32 (RFC 4648, no padding), each
// both ways, and hex, one way. Decoding refuses anything that is not exactly
// the encoding, and returns ``std::nullopt``.
//
// Plain C++20 (no Qt, no OpenSSL).

#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::crypto {

/// Lower-case hex, two digits a byte.
[[nodiscard]] std::string to_hex(std::string_view bytes);

[[nodiscard]] std::string to_base64(std::string_view bytes);
/// The standard alphabet with padding, nothing else (no line breaks).
[[nodiscard]] std::optional<std::string> from_base64(std::string_view text);

/// Upper-case letters and digits 2-7, no padding: for codes people type.
[[nodiscard]] std::string to_base32(std::string_view bytes);
/// Either case; spaces and dashes between groups are allowed and ignored.
[[nodiscard]] std::optional<std::string> from_base32(std::string_view text);

}  // namespace seventhslide::crypto
