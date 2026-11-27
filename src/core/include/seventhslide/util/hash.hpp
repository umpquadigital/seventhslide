#pragma once

// Stable hashes for naming things.
//
// Plain C++20 (no Qt).

#include <string>
#include <string_view>

namespace seventhslide::util {

/// FNV-1a of ``bytes`` as fixed-width lower-case hex: 8 digits for the 32-bit
/// hash, 16 for the 64-bit one. The same on every run and platform, so it can
/// name files and cache entries. Not for security.
[[nodiscard]] std::string fnv1a32_hex(std::string_view bytes);
[[nodiscard]] std::string fnv1a64_hex(std::string_view bytes);

}  // namespace seventhslide::util
