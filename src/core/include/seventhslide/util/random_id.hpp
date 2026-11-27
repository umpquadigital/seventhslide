#pragma once

// Random identifiers for locally created records (stacks, themes, assets).
// Plain C++20 (no Qt).

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>

namespace seventhslide::util {

/// ``words`` 64-bit random values as lower-case hex, 16 digits each.
[[nodiscard]] inline std::string random_hex_id(std::size_t words)
{
    static constexpr char k_digits[] = "0123456789abcdef";
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::string out;
    out.reserve(words * 16);
    for (std::size_t w = 0; w < words; ++w) {
        std::uint64_t v = rng();
        for (int shift = 60; shift >= 0; shift -= 4) {
            out.push_back(k_digits[(v >> shift) & 0xF]);
        }
    }
    return out;
}

}  // namespace seventhslide::util
