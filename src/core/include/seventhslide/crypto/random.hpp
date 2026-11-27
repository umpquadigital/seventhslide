#pragma once

// Random bytes fit for keys and secrets, from the operating system through
// OpenSSL.
//
// Plain C++20 over OpenSSL (no Qt).

#include <cstddef>
#include <string>

namespace seventhslide::crypto {

/// ``n`` random bytes. Throws ``std::runtime_error`` when the system has
/// none to give, which no key may be made without.
[[nodiscard]] std::string random_bytes(std::size_t n);

}  // namespace seventhslide::crypto
