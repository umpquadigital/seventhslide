#include "seventhslide/util/hash.hpp"

#include <cstdint>

namespace seventhslide::util {

namespace {

template <typename Word, Word Basis, Word Prime>
[[nodiscard]] std::string fnv1a_hex(std::string_view bytes)
{
    Word hash = Basis;
    for (const char ch : bytes) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= Prime;
    }
    constexpr std::string_view digits = "0123456789abcdef";
    std::string hex(sizeof(Word) * 2, '0');
    for (auto it = hex.rbegin(); it != hex.rend(); ++it) {
        *it = digits[hash & 0xFu];
        hash >>= 4;
    }
    return hex;
}

}  // namespace

std::string fnv1a32_hex(std::string_view bytes)
{
    return fnv1a_hex<std::uint32_t, 2166136261u, 16777619u>(bytes);
}

std::string fnv1a64_hex(std::string_view bytes)
{
    return fnv1a_hex<std::uint64_t, 14695981039346656037ull, 1099511628211ull>(bytes);
}

}  // namespace seventhslide::util
