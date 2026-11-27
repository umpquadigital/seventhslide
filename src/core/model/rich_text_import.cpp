// Rich text: song-file import. ``<br>`` to newlines, HTML entities decoded,
// and HTML formatting elements mapped to the markup's tags.

#include <seventhslide/model/rich_text.hpp>

#include "rich_text_internal.hpp"

#include <seventhslide/util/strings.hpp>

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace seventhslide::model {
namespace {

[[nodiscard]] std::optional<char> html_entity_char(std::string_view name)
{
    if (name == "amp") {
        return '&';
    }
    if (name == "lt") {
        return '<';
    }
    if (name == "gt") {
        return '>';
    }
    if (name == "quot") {
        return '"';
    }
    if (name == "apos") {
        return '\'';
    }
    return std::nullopt;
}

/// The character a numeric entity body (``#8217``, ``#x2019``) names, if it
/// is one a text can hold: not NUL, not a surrogate, at most U+10FFFF.
[[nodiscard]] std::optional<char32_t> numeric_entity(std::string_view body)
{
    if (body.size() < 2 || body[0] != '#') {
        return std::nullopt;
    }
    int base = 10;
    std::string_view digits = body.substr(1);
    if (digits[0] == 'x' || digits[0] == 'X') {
        base = 16;
        digits = digits.substr(1);
    }
    std::uint32_t code = 0;
    const auto* end = digits.data() + digits.size();
    const auto [ptr, ec] = std::from_chars(digits.data(), end, code, base);
    if (digits.empty() || ec != std::errc{} || ptr != end || code == 0 || code > 0x10FFFF
        || (code >= 0xD800 && code <= 0xDFFF)) {
        return std::nullopt;
    }
    return static_cast<char32_t>(code);
}

/// HTML entities left in imported text: the five XML names and numeric
/// references (as UTF-8). The XML parser has already decoded one level, so
/// these are what a double-escaped file (``&amp;#8217;``) leaves behind.
[[nodiscard]] std::string html_unescape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }
        const std::size_t semi = text.find(';', i + 1);
        if (semi == std::string_view::npos || semi - i > 32) {
            out.push_back('&');
            continue;
        }
        const std::string_view body = text.substr(i + 1, semi - i - 1);
        if (const auto code = numeric_entity(body)) {
            util::append_utf8(out, *code);
            i = semi;
            continue;
        }
        if (const auto ch = html_entity_char(body)) {
            out.push_back(*ch);
            i = semi;
            continue;
        }
        out.push_back('&');
    }
    return out;
}

[[nodiscard]] std::string br_to_newlines(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '<') {
            const std::size_t end = text.find('>', i + 1);
            if (end == std::string_view::npos) {
                out.push_back(text[i]);
                ++i;
                continue;
            }
            std::string_view tok = text.substr(i, end - i + 1);
            // Match ``<(ns:)?br\s*/?>`` case-insensitive.
            std::string lower = util::to_lower(tok);
            std::string_view body = lower;
            if (body.size() >= 2 && body.front() == '<' && body.back() == '>') {
                body.remove_prefix(1);
                body.remove_suffix(1);
                while (!body.empty() && util::is_space(body.front())) {
                    body.remove_prefix(1);
                }
                // Optional XML prefix ``foo:``
                const auto colon = body.find(':');
                if (colon != std::string_view::npos
                    && colon + 1 < body.size()
                    && body.find_first_of(" \t/", 0) > colon) {
                    body.remove_prefix(colon + 1);
                }
                if (util::starts_with_ci(body, "br")) {
                    auto rest = body.substr(2);
                    while (!rest.empty()
                           && (util::is_space(rest.front()) || rest.front() == '/')) {
                        rest.remove_prefix(1);
                    }
                    if (rest.empty()) {
                        out.push_back('\n');
                        i = end + 1;
                        continue;
                    }
                }
            }
            out.append(tok);
            i = end + 1;
            continue;
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

[[nodiscard]] constexpr bool is_ascii_alpha(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

[[nodiscard]] constexpr bool is_attr_name_char(char c) noexcept
{
    return is_ascii_alpha(c) || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':';
}

/// A formatting element as an imported song file may spell it, with
/// attributes (``<b class="x">``, ``</strong >``, ``<i/>``).
struct HtmlStyleTag {
    std::string name;
    bool close = false;
    std::size_t length = 0;
};

/// ``s`` begins with '<'. Only formatting elements qualify, and every
/// attribute must be ``name=value``, so text such as ``< b and c >`` or
/// ``<b and c>`` never matches.
[[nodiscard]] std::optional<HtmlStyleTag> match_html_style_tag(std::string_view s)
{
    std::size_t i = 1;
    const bool close = i < s.size() && s[i] == '/';
    if (close) {
        ++i;
    }
    const std::size_t name_start = i;
    while (i < s.size() && is_ascii_alpha(s[i])) {
        ++i;
    }
    std::string name = canonical_style_name(s.substr(name_start, i - name_start));
    if (name.empty()) {
        return std::nullopt;
    }
    const auto skip_spaces = [&] {
        while (i < s.size() && util::is_space(s[i]) && s[i] != '\n') {
            ++i;
        }
    };
    for (;;) {
        const std::size_t before = i;
        skip_spaces();
        if (i >= s.size()) {
            return std::nullopt;
        }
        if (s[i] == '>') {
            return HtmlStyleTag{std::move(name), close, i + 1};
        }
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '>') {
            return HtmlStyleTag{std::move(name), close, i + 2};
        }
        if (i == before) {
            return std::nullopt;  // an attribute must follow whitespace
        }
        const std::size_t attr_start = i;
        while (i < s.size() && is_attr_name_char(s[i])) {
            ++i;
        }
        if (i == attr_start) {
            return std::nullopt;
        }
        skip_spaces();
        if (i >= s.size() || s[i] != '=') {
            return std::nullopt;
        }
        ++i;
        skip_spaces();
        if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
            const auto end = s.find(s[i], i + 1);
            if (end == std::string_view::npos) {
                return std::nullopt;
            }
            i = end + 1;
        } else {
            const std::size_t value_start = i;
            while (i < s.size() && !util::is_space(s[i]) && s[i] != '>') {
                ++i;
            }
            if (i == value_start) {
                return std::nullopt;
            }
        }
    }
}

/// The canonical spelling of ``tag``.
[[nodiscard]] std::string tag_markup(const MarkupTag& tag)
{
    if (tag.close) {
        return "</" + tag.name + ">";
    }
    if (tag.name == "size" || tag.name == "rise" || tag.name == "gap") {
        return "<" + tag.name + "=" + std::to_string(tag.number) + ">";
    }
    if (tag.name == "color" || tag.name == "font" || tag.name == "p") {
        return "<" + tag.name + "=" + tag.value + ">";
    }
    return "<" + tag.name + ">";
}

} // namespace

std::string normalize_imported_markup(std::string_view text)
{
    const std::string decoded = html_unescape(br_to_newlines(text));
    std::string out;
    out.reserve(decoded.size());
    const std::string_view s = decoded;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '<') {
            const std::string_view rest = s.substr(i);
            if (const auto tag = match_markup_tag(rest)) {
                out += tag_markup(*tag);
                i += tag->length;
                continue;
            }
            if (const auto html = match_html_style_tag(rest)) {
                out += html->close ? "</" + html->name + ">" : "<" + html->name + ">";
                i += html->length;
                continue;
            }
        } else if (s[i] == '&' && markup_entity(s.substr(i))) {
            // Decoded text that happens to spell an entity stays literal.
            out += "&amp;";
            ++i;
            continue;
        }
        out.push_back(s[i]);
        ++i;
    }
    return out;
}

} // namespace seventhslide::model
