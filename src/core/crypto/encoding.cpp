#include "seventhslide/crypto/encoding.hpp"

#include <array>
#include <cstdint>

namespace seventhslide::crypto {
namespace {

constexpr char k_hex[] = "0123456789abcdef";
constexpr std::string_view k_base64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::string_view k_base32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

int base32_value(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a';
    }
    if (c >= '2' && c <= '7') {
        return c - '2' + 26;
    }
    return -1;
}

/// Read ``bits_per_char``-bit values, pack them into bytes, and drop the
/// bits left over at the end (they must be zero, as an encoder leaves them).
template <typename Value>
std::optional<std::string> unpack(std::string_view text, int bits_per_char, Value value,
                                  bool (*skip)(char))
{
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text) {
        if (skip(c)) {
            continue;
        }
        const int v = value(c);
        if (v < 0) {
            return std::nullopt;
        }
        buffer = (buffer << bits_per_char) | static_cast<std::uint32_t>(v);
        bits += bits_per_char;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    if (bits >= bits_per_char || (buffer & ((1U << bits) - 1)) != 0) {
        return std::nullopt;  // a whole character too many, or stray bits
    }
    return out;
}

/// Write the bytes as ``bits_per_char``-bit values, each a character of
/// ``alphabet``, the last padded with zero bits.
std::string pack(std::string_view bytes, int bits_per_char, std::string_view alphabet)
{
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const unsigned char c : bytes) {
        buffer = (buffer << 8) | c;
        bits += 8;
        while (bits >= bits_per_char) {
            bits -= bits_per_char;
            out.push_back(alphabet[(buffer >> bits) & ((1U << bits_per_char) - 1)]);
        }
    }
    if (bits > 0) {
        out.push_back(alphabet[(buffer << (bits_per_char - bits)) & ((1U << bits_per_char) - 1)]);
    }
    return out;
}

}  // namespace

std::string to_hex(std::string_view bytes)
{
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const unsigned char c : bytes) {
        out.push_back(k_hex[c >> 4]);
        out.push_back(k_hex[c & 0xF]);
    }
    return out;
}

std::string to_base64(std::string_view bytes)
{
    std::string out = pack(bytes, 6, k_base64);
    while (out.size() % 4 != 0) {
        out.push_back('=');
    }
    return out;
}

std::optional<std::string> from_base64(std::string_view text)
{
    if (text.size() % 4 != 0) {
        return std::nullopt;
    }
    std::size_t padding = 0;
    while (!text.empty() && text.back() == '=' && padding < 2) {
        text.remove_suffix(1);
        ++padding;
    }
    return unpack(text, 6, base64_value, [](char) { return false; });
}

std::string to_base32(std::string_view bytes)
{
    return pack(bytes, 5, k_base32);
}

std::optional<std::string> from_base32(std::string_view text)
{
    return unpack(text, 5, base32_value, [](char c) { return c == ' ' || c == '-'; });
}

}  // namespace seventhslide::crypto
