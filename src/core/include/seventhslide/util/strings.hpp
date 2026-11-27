#pragma once

// ASCII string helpers shared across the core.
//
// Locale-independent on purpose: <cctype> follows the process locale, which
// Qt sets from the environment, so ``std::tolower('I')`` differs under a
// Turkish locale and single-byte locales rewrite UTF-8 continuation bytes.
// Verse labels, style keys, and tag names are ASCII, and text passes through
// untouched above 0x7F.
//
// Plain C++20 (no Qt).

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::util {

/// Space, tab, newline, carriage return, form feed, vertical tab.
[[nodiscard]] constexpr bool is_space(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

[[nodiscard]] constexpr bool is_digit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

[[nodiscard]] constexpr bool is_lower(char c) noexcept
{
    return c >= 'a' && c <= 'z';
}

[[nodiscard]] constexpr bool is_upper(char c) noexcept
{
    return c >= 'A' && c <= 'Z';
}

[[nodiscard]] constexpr bool is_alpha(char c) noexcept
{
    return is_lower(c) || is_upper(c);
}

[[nodiscard]] constexpr bool is_alnum(char c) noexcept
{
    return is_alpha(c) || is_digit(c);
}

[[nodiscard]] constexpr char to_lower(char c) noexcept
{
    return is_upper(c) ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr char to_upper(char c) noexcept
{
    return is_lower(c) ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] constexpr std::string_view trim_view(std::string_view s) noexcept
{
    std::size_t b = 0;
    while (b < s.size() && is_space(s[b])) {
        ++b;
    }
    std::size_t e = s.size();
    while (e > b && is_space(s[e - 1])) {
        --e;
    }
    return s.substr(b, e - b);
}

[[nodiscard]] inline std::string trim(std::string_view s)
{
    return std::string(trim_view(s));
}

[[nodiscard]] inline std::string to_lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) {
        c = to_lower(c);
    }
    return out;
}

[[nodiscard]] inline std::string to_upper(std::string_view s)
{
    std::string out(s);
    for (char& c : out) {
        c = to_upper(c);
    }
    return out;
}

[[nodiscard]] constexpr bool iequals(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (to_lower(a[i]) != to_lower(b[i])) {
            return false;
        }
    }
    return true;
}

/// Case-insensitive substring test; an empty needle matches.
[[nodiscard]] constexpr bool contains_ci(std::string_view hay, std::string_view needle) noexcept
{
    if (needle.size() > hay.size()) {
        return false;
    }
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        if (iequals(hay.substr(i, needle.size()), needle)) {
            return true;
        }
    }
    return false;
}

/// Case-insensitive prefix test.
[[nodiscard]] constexpr bool starts_with_ci(std::string_view s, std::string_view prefix) noexcept
{
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

/// A ``{...}`` in template text, braces included: ``[begin, end)``.
struct BraceSpan {
    std::size_t begin = 0;  ///< the '{'
    std::size_t end = 0;    ///< one past the '}'

    [[nodiscard]] constexpr std::string_view inner(std::string_view s) const noexcept
    {
        return s.substr(begin + 1, end - begin - 2);
    }
};

/// The first brace span at or after ``from`` that holds no '{' or newline. A
/// '{' that does not close cleanly stays literal text and the scan resumes at
/// the next '{'. The one rule both template languages (song overlay tokens and
/// Freeform fields) use to find their tokens.
[[nodiscard]] constexpr std::optional<BraceSpan> next_brace_span(std::string_view s,
                                                                 std::size_t from) noexcept
{
    for (auto open = s.find('{', from); open != std::string_view::npos;
         open = s.find('{', open + 1)) {
        const auto close = s.find_first_of("{}\n", open + 1);
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        if (s[close] == '}') {
            return BraceSpan{open, close + 1};
        }
    }
    return std::nullopt;
}

/// Runs of non-whitespace, in order. Views into ``s``.
[[nodiscard]] inline std::vector<std::string_view> split_ws(std::string_view s)
{
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && is_space(s[i])) {
            ++i;
        }
        const std::size_t start = i;
        while (i < s.size() && !is_space(s[i])) {
            ++i;
        }
        if (i > start) {
            out.push_back(s.substr(start, i - start));
        }
    }
    return out;
}

/// The words of ``s`` joined by single spaces: trimmed, runs collapsed.
[[nodiscard]] inline std::string collapse_ws(std::string_view s)
{
    std::string out;
    for (const auto word : split_ws(s)) {
        if (!out.empty()) {
            out.push_back(' ');
        }
        out.append(word);
    }
    return out;
}

/// ``split_ws`` as owned strings.
[[nodiscard]] inline std::vector<std::string> split_words(std::string_view s)
{
    const auto views = split_ws(s);
    return {views.begin(), views.end()};
}

/// The integer ``s`` starts with, read the way ``std::stoi`` reads it (leading
/// whitespace, an optional sign, then digits; anything after is ignored), or
/// nullopt when there are no digits or the value does not fit an ``int``.
/// Never throws, so untrusted text (typed references, file attributes, stored
/// ids) needs no try/catch.
[[nodiscard]] inline std::optional<int> parse_leading_int(std::string_view s) noexcept
{
    s = trim_view(s);
    bool negative = false;
    if (!s.empty() && (s.front() == '+' || s.front() == '-')) {
        negative = s.front() == '-';
        s.remove_prefix(1);
    }
    long long value = 0;
    bool any = false;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            break;
        }
        any = true;
        value = value * 10 + (c - '0');
        if (value > 2147483648LL) {
            return std::nullopt;
        }
    }
    if (!any) {
        return std::nullopt;
    }
    value = negative ? -value : value;
    if (value > 2147483647LL || value < -2147483648LL) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

/// Code points in valid UTF-8 (bytes that are not continuation bytes).
[[nodiscard]] constexpr std::size_t utf8_length(std::string_view s) noexcept
{
    std::size_t n = 0;
    for (const char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++n;
        }
    }
    return n;
}

/// Bytes in the UTF-8 sequence that starts with ``lead``; 1 for a byte that
/// starts none (a stray continuation byte is stepped over alone).
[[nodiscard]] constexpr std::size_t utf8_sequence_length(unsigned char lead) noexcept
{
    if (lead < 0x80) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

/// ``cp`` added to ``out`` as UTF-8; a surrogate, or a value past U+10FFFF,
/// as U+FFFD.
inline void append_utf8(std::string& out, char32_t cp)
{
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        cp = 0xFFFD;
    }
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// The longest start of ``s`` that fits in ``max_bytes`` without cutting a
/// UTF-8 sequence in half.
[[nodiscard]] constexpr std::string_view utf8_prefix(std::string_view s,
                                                     std::size_t max_bytes) noexcept
{
    if (s.size() <= max_bytes) {
        return s;
    }
    std::size_t cut = max_bytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return s.substr(0, cut);
}

/// ``text`` with ``&``, ``<`` and ``>`` as entities, and both quotes as well
/// when ``quotes`` (for an attribute value).
[[nodiscard]] inline std::string xml_escape(std::string_view text, bool quotes = false)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += quotes ? "&quot;" : "\"";
            break;
        case '\'':
            out += quotes ? "&apos;" : "'";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

}  // namespace seventhslide::util
