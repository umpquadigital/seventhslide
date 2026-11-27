#include "seventhslide/util/text_encoding.hpp"

#include "seventhslide/util/strings.hpp"

#include <cstdint>
#include <initializer_list>

namespace seventhslide::util {
namespace {

constexpr char32_t k_replacement = 0xFFFD;

[[nodiscard]] std::string from_utf16(std::string_view bytes, bool big_endian)
{
    std::string out;
    out.reserve(bytes.size());
    const auto unit = [&](std::size_t i) -> char16_t {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = static_cast<unsigned char>(bytes[i + 1]);
        return static_cast<char16_t>(big_endian ? (a << 8) | b : (b << 8) | a);
    };
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const char16_t u = unit(i);
        if (u >= 0xD800 && u <= 0xDBFF && i + 3 < bytes.size()) {
            const char16_t low = unit(i + 2);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                append_utf8(out, 0x10000 + ((char32_t(u) - 0xD800) << 10) + (char32_t(low) - 0xDC00));
                i += 2;
                continue;
            }
        }
        append_utf8(out, (u >= 0xD800 && u <= 0xDFFF) ? k_replacement : char32_t(u));
    }
    if (bytes.size() % 2 != 0) {
        append_utf8(out, k_replacement);
    }
    return out;
}

/// Windows-1252's 0x80-0x9F, which are not Latin-1's control characters.
constexpr char16_t k_cp1252_high[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
};

[[nodiscard]] std::string from_cp1252(std::string_view bytes)
{
    std::string out;
    out.reserve(bytes.size() + bytes.size() / 4);
    for (const char ch : bytes) {
        const auto b = static_cast<unsigned char>(ch);
        if (b >= 0x80 && b < 0xA0) {
            append_utf8(out, k_cp1252_high[b - 0x80]);
        } else {
            append_utf8(out, b);
        }
    }
    return out;
}

[[nodiscard]] bool starts_with_bytes(std::string_view s, std::initializer_list<unsigned char> prefix)
{
    if (s.size() < prefix.size()) {
        return false;
    }
    std::size_t i = 0;
    for (const auto b : prefix) {
        if (static_cast<unsigned char>(s[i++]) != b) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool is_valid_utf8(std::string_view bytes) noexcept
{
    for (std::size_t i = 0; i < bytes.size();) {
        const auto b = static_cast<unsigned char>(bytes[i]);
        if (b < 0x80) {
            ++i;
            continue;
        }
        std::size_t len = 0;
        char32_t cp = 0;
        if ((b & 0xE0) == 0xC0) {
            len = 2;
            cp = b & 0x1F;
        } else if ((b & 0xF0) == 0xE0) {
            len = 3;
            cp = b & 0x0F;
        } else if ((b & 0xF8) == 0xF0) {
            len = 4;
            cp = b & 0x07;
        } else {
            return false;
        }
        if (i + len > bytes.size()) {
            return false;
        }
        for (std::size_t k = 1; k < len; ++k) {
            const auto c = static_cast<unsigned char>(bytes[i + k]);
            if ((c & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (c & 0x3F);
        }
        const char32_t min = len == 2 ? 0x80 : len == 3 ? 0x800 : 0x10000;
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += len;
    }
    return true;
}

std::string decode_text(std::string_view bytes)
{
    if (starts_with_bytes(bytes, {0xEF, 0xBB, 0xBF})) {
        bytes.remove_prefix(3);
        return is_valid_utf8(bytes) ? std::string(bytes) : from_cp1252(bytes);
    }
    if (starts_with_bytes(bytes, {0xFF, 0xFE})) {
        return from_utf16(bytes.substr(2), false);
    }
    if (starts_with_bytes(bytes, {0xFE, 0xFF})) {
        return from_utf16(bytes.substr(2), true);
    }
    return is_valid_utf8(bytes) ? std::string(bytes) : from_cp1252(bytes);
}

}  // namespace seventhslide::util
